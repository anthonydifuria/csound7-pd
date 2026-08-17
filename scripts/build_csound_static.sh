#!/usr/bin/env bash
#
# Static build of Csound 7: core + libsndfile + libsamplerate, no realtime
# audio/MIDI backends, no OSC/csbeats/DSSI, no opcode plugins. Everything
# vendored and built from source as static libraries, so the final external
# has zero dependency on any system-installed library.
#
# Supports macOS (universal arm64+x86_64) and Linux (single-arch, whatever
# the host machine is). Windows is NOT covered here - its toolchain (MSVC,
# .lib instead of .a, no lipo, no Homebrew/apt) is different enough that
# force-fitting it into this same bash script isn't realistic; that's a
# separate build process (see the project README once that lands).
#
# --- macOS branch: rewritten after a real build attempt on the user's
# machine ---
# The first version of this script relied on `-DUSE_STATIC_DEPS=ON` to get
# a static libsndfile/libsamplerate, but the actual CMake configure log
# showed Csound was picking up Homebrew's *dynamic* libsndfile.dylib and
# libsamplerate.dylib instead (find_library() found them on the system
# before our flag could do anything about it). That defeats the whole
# "no separate install needed" goal. Fix: we now build libsndfile and
# libsamplerate ourselves as static libraries first, and pre-seed CMake's
# SndFile_LIBRARY / SndFile_INCLUDE_DIR / SampleRate_LIBRARY /
# SampleRate_INCLUDE_DIR cache variables (confirmed exact names by reading
# csound/csound's cmake/Modules/FindSndFile.cmake and FindSampleRate.cmake)
# so Csound's own find_library() calls never touch Homebrew at all. Same
# pre-seeding mechanism reused for the Linux branch below - it's plain
# CMake, nothing macOS-specific about it.
#
# The first attempt also failed outright at the bison step: macOS ships
# bison 2.3 (Apple froze it there for licensing reasons), and Csound's
# grammar needs a modern bison. Fixed below by requiring Homebrew's bison.
#
# The first attempt also auto-enabled PortAudio/PortMidi/JACK/CoreMIDI/
# AudioUnit realtime backends and the OSC opcodes and the csbeats frontend
# just because Homebrew happened to have those libraries installed — none
# of that is wanted here (we drive Csound purely through the API), so they
# are now explicitly disabled.
#
# --- Linux branch: added and verified with a real build in a Linux sandbox
# (Ubuntu 22.04, aarch64) ---
# Two real bugs found and fixed by actually running this, not just reading
# Csound's CMakeLists.txt:
#   1. Csound's InOut/CMakeLists.txt has `option(USE_ALSA ... ON)` and, if
#      that's on, does `find_package(ALSA MODULE REQUIRED)` on Linux - the
#      REQUIRED means CMake's configure step hard-fails if ALSA's dev
#      headers aren't installed, even though FAIL_MISSING=OFF is already
#      set (that flag doesn't cover this one, it's a separate REQUIRED
#      inside a subdirectory). Since we don't want any realtime backend
#      anyway (same reasoning as disabling PortAudio/JACK/etc. above),
#      -DUSE_ALSA=OFF sidesteps this cleanly rather than needing ALSA dev
#      headers installed on the build machine.
#   2. flex, once actually invoked (not just version-checked), fails with
#      "fatal internal error, exec of /usr/bin/m4 failed" if GNU m4 isn't
#      at that exact hardcoded path - even if a perfectly good m4 is
#      elsewhere on PATH. flex's own error message points at the fix: set
#      the M4 environment variable to m4's real path. Handled below.
# Also: Csound's own CMakeLists.txt names its static library differently
# depending on OS - "CsoundLib64" (-> libCsoundLib64.a) only on Apple,
# plain "csound64" (-> libcsound64.a) everywhere else. This script doesn't
# need to care (it just reports the real file names at the end), but
# source/csound7_pd/CMakeLists.txt does, and is already updated for it.

set -euo pipefail

CSOUND_REPO="https://github.com/csound/csound.git"
CSOUND_BRANCH="develop"
SNDFILE_REPO="https://github.com/libsndfile/libsndfile.git"
SAMPLERATE_REPO="https://github.com/libsndfile/libsamplerate.git"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_ROOT="$PROJECT_ROOT/build"
INSTALL_ROOT="$BUILD_ROOT/csound-install"

OS_NAME="$(uname -s)"

# ---------------------------------------------------------------------
# helper: clone a repo once (shared by both OS branches)
# ---------------------------------------------------------------------
clone_once() {
  local repo="$1" branch="$2" dest="$3"
  if [ ! -d "$dest/.git" ]; then
    echo "==> cloning $repo ($branch)"
    git clone --branch "$branch" --depth 1 "$repo" "$dest"
  fi
}

if [[ "$OS_NAME" == "Darwin" ]]; then
# =======================================================================
# macOS branch - universal (arm64 + x86_64) build via lipo
# =======================================================================

ARCHS=(arm64 x86_64)

# ---------------------------------------------------------------------
# 0. toolchain check: need a modern bison (Homebrew), not Apple's ancient
#    2.3. flex is pinned to Homebrew's too, just for consistency.
# ---------------------------------------------------------------------
if ! command -v brew >/dev/null 2>&1; then
  echo "ERROR: Homebrew not found. Install it from https://brew.sh first."
  exit 1
fi

for pkg in bison flex; do
  if ! brew list "$pkg" >/dev/null 2>&1; then
    echo "==> installing $pkg via Homebrew (macOS's own bison is too old for Csound's grammar)"
    brew install "$pkg"
  fi
done

BISON_BIN="$(brew --prefix bison)/bin/bison"
FLEX_BIN="$(brew --prefix flex)/bin/flex"

if [ ! -x "$BISON_BIN" ]; then
  echo "ERROR: expected bison at $BISON_BIN, not found. Check 'brew info bison'."
  exit 1
fi

echo "==> using bison: $($BISON_BIN --version | head -1)"
echo "==> using flex:   $($FLEX_BIN --version | head -1)"

NPROC="$(sysctl -n hw.ncpu)"

# ---------------------------------------------------------------------
# 1. libsndfile, static, core formats only (no FLAC/Vorbis/Opus/MP3, so we
#    don't also have to vendor and statically link ogg/vorbis/opus/flac/
#    lame/mpg123 ourselves). Covers WAV/AIFF/AU/CAF and friends, which is
#    what diskin/soundin/fout need for the common case.
# ---------------------------------------------------------------------
SNDFILE_SRC="$BUILD_ROOT/sndfile-static/src"
clone_once "$SNDFILE_REPO" "master" "$SNDFILE_SRC"

build_sndfile_arch() {
  local arch="$1"
  local build_dir="$BUILD_ROOT/sndfile-static/build-$arch"
  local install_dir="$INSTALL_ROOT/$arch-deps/sndfile"
  echo "==> building static libsndfile for $arch"
  cmake -S "$SNDFILE_SRC" -B "$build_dir" -G "Unix Makefiles" \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$install_dir" \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_PROGRAMS=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTING=OFF \
    -DENABLE_EXTERNAL_LIBS=OFF \
    -DENABLE_MPEG=OFF \
    -DENABLE_CPACK=OFF
  cmake --build "$build_dir" --config Release -j"$NPROC"
  cmake --install "$build_dir" --config Release
}

# ---------------------------------------------------------------------
# 2. libsamplerate, static
# ---------------------------------------------------------------------
SAMPLERATE_SRC="$BUILD_ROOT/samplerate-static/src"
clone_once "$SAMPLERATE_REPO" "master" "$SAMPLERATE_SRC"

build_samplerate_arch() {
  local arch="$1"
  local build_dir="$BUILD_ROOT/samplerate-static/build-$arch"
  local install_dir="$INSTALL_ROOT/$arch-deps/samplerate"
  echo "==> building static libsamplerate for $arch"
  cmake -S "$SAMPLERATE_SRC" -B "$build_dir" -G "Unix Makefiles" \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$install_dir" \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTING=OFF \
    -DLIBSAMPLERATE_EXAMPLES=OFF \
    -DLIBSAMPLERATE_INSTALL=ON
  cmake --build "$build_dir" --config Release -j"$NPROC"
  cmake --install "$build_dir" --config Release
}

# ---------------------------------------------------------------------
# 3. Csound 7, static, pointed at our own static sndfile/samplerate
# ---------------------------------------------------------------------
CSOUND_SRC="$BUILD_ROOT/csound-static/src"
clone_once "$CSOUND_REPO" "$CSOUND_BRANCH" "$CSOUND_SRC"

build_csound_arch() {
  local arch="$1"
  local build_dir="$BUILD_ROOT/csound-static/build-$arch"
  local install_dir="$INSTALL_ROOT/$arch"
  local sndfile_dir="$INSTALL_ROOT/$arch-deps/sndfile"
  local samplerate_dir="$INSTALL_ROOT/$arch-deps/samplerate"

  # Direct paths (confirmed from a real build log: libsndfile installs to
  # lib/libsndfile.a, libsamplerate to lib/libsamplerate.a on macOS).
  # NOTE: an earlier version of this used `find ... | head -1` here, which
  # silently killed the whole script under `set -euo pipefail` (find exits
  # non-zero when one of its search roots doesn't exist / gets SIGPIPE from
  # head closing early, and pipefail propagates that into `set -e` with no
  # error message printed) — that's why the script stopped dead right after
  # installing libsamplerate with no error shown. Plain path checks instead.
  local sndfile_lib="$sndfile_dir/lib/libsndfile.a"
  local samplerate_lib="$samplerate_dir/lib/libsamplerate.a"

  if [ ! -f "$sndfile_lib" ]; then
    echo "ERROR: expected static libsndfile at $sndfile_lib, not found."
    echo "Contents of $sndfile_dir/lib:"
    ls -la "$sndfile_dir/lib" 2>/dev/null || echo "  (directory does not exist)"
    exit 1
  fi
  if [ ! -f "$samplerate_lib" ]; then
    echo "ERROR: expected static libsamplerate at $samplerate_lib, not found."
    echo "Contents of $samplerate_dir/lib:"
    ls -la "$samplerate_dir/lib" 2>/dev/null || echo "  (directory does not exist)"
    exit 1
  fi

  echo "==> building static Csound for $arch"
  echo "    using SndFile:    $sndfile_lib"
  echo "    using SampleRate: $samplerate_lib"

  cmake -S "$CSOUND_SRC" -B "$build_dir" -G "Unix Makefiles" \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.14 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$install_dir" \
    -DBISON_EXECUTABLE="$BISON_BIN" \
    -DFLEX_EXECUTABLE="$FLEX_BIN" \
    -DSndFile_LIBRARY="$sndfile_lib" \
    -DSndFile_INCLUDE_DIR="$sndfile_dir/include" \
    -DSampleRate_LIBRARY="$samplerate_lib" \
    -DSampleRate_INCLUDE_DIR="$samplerate_dir/include" \
    -DBUILD_STATIC_LIBRARY=ON \
    -DBUILD_PLUGINS=OFF \
    -DUSE_LIBSNDFILE=ON \
    -DUSE_LIBSAMPLERATE=ON \
    -DUSE_CURL=OFF \
    -DUSE_GETTEXT=OFF \
    -DBUILD_UTILITIES=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_INSTALLER=OFF \
    -DBUILD_DOCS=OFF \
    -DUSE_DOUBLE=ON \
    -DFAIL_MISSING=OFF \
    -DUSE_PORTAUDIO=OFF \
    -DUSE_PORTMIDI=OFF \
    -DUSE_JACK=OFF \
    -DUSE_COREMIDI=OFF \
    -DUSE_AUDIOUNIT=OFF \
    -DUSE_IPMIDI=OFF \
    -DUSE_PULSEAUDIO=OFF \
    -DUSE_PIPEWIRE=OFF \
    -DBUILD_OSC_OPCODES=OFF \
    -DBUILD_CSBEATS=OFF \
    -DBUILD_DSSI_OPCODES=OFF

  cmake --build "$build_dir" --config Release -j"$NPROC"
  cmake --install "$build_dir" --config Release
}

for arch in "${ARCHS[@]}"; do
  build_sndfile_arch "$arch"
  build_samplerate_arch "$arch"
  build_csound_arch "$arch"
done

# ---------------------------------------------------------------------
# 4. merge everything into a universal build (lipo)
# ---------------------------------------------------------------------
echo "==> merging into a universal build (lipo)"
UNIVERSAL_LIB_DIR="$INSTALL_ROOT/universal/lib"
mkdir -p "$UNIVERSAL_LIB_DIR"

lipo_merge_dir() {
  local label="$1" dir_a="$2" dir_b="$3"
  if [ ! -d "$dir_a" ]; then
    echo "ERROR: $dir_a does not exist, the arm64 build for $label did not install anything there."
    exit 1
  fi
  for LIBFILE in $(cd "$dir_a" && ls *.a 2>/dev/null); do
    if [ -f "$dir_b/$LIBFILE" ]; then
      lipo -create "$dir_a/$LIBFILE" "$dir_b/$LIBFILE" -output "$UNIVERSAL_LIB_DIR/$LIBFILE"
      echo "   universal ($label): $LIBFILE"
    else
      echo "   WARNING: $LIBFILE ($label) only present for arm64, skipping (please check)"
    fi
  done
}

lipo_merge_dir "csound"     "$INSTALL_ROOT/arm64/lib"              "$INSTALL_ROOT/x86_64/lib"
lipo_merge_dir "sndfile"    "$INSTALL_ROOT/arm64-deps/sndfile/lib"    "$INSTALL_ROOT/x86_64-deps/sndfile/lib"
lipo_merge_dir "samplerate" "$INSTALL_ROOT/arm64-deps/samplerate/lib" "$INSTALL_ROOT/x86_64-deps/samplerate/lib"

mkdir -p "$INSTALL_ROOT/universal/include"

# Csound's own CMake install does NOT put headers under our
# CMAKE_INSTALL_PREFIX/include on macOS — it packages itself as a real
# CsoundLib64.framework and installs headers there instead (confirmed from
# a real build log: "Installing: .../CsoundLib64.framework/.../Headers/csound.h").
# The static .a itself DOES land in our own prefix (lib/libCsoundLib64.a,
# already handled above), just not the headers. Grab them from the
# framework's stable "Headers" symlink (Headers -> Versions/Current/Headers),
# so we don't have to hardcode the Csound version number (7.0 today, might
# change).
CSOUND_FRAMEWORK_HEADERS="$HOME/Library/Frameworks/CsoundLib64.framework/Headers"
if [ ! -d "$CSOUND_FRAMEWORK_HEADERS" ]; then
  echo "ERROR: Csound headers not found at $CSOUND_FRAMEWORK_HEADERS"
  echo "Csound's CMake install must have changed where it puts headers on your system — look for csound.h under ~/Library/Frameworks and adjust this path."
  exit 1
fi
cp -R "$CSOUND_FRAMEWORK_HEADERS/." "$INSTALL_ROOT/universal/include/"
cp -R "$INSTALL_ROOT/arm64-deps/sndfile/include/." "$INSTALL_ROOT/universal/include/"
cp -R "$INSTALL_ROOT/arm64-deps/samplerate/include/." "$INSTALL_ROOT/universal/include/"

echo ""
echo "Done. Universal static libraries in: $UNIVERSAL_LIB_DIR"
echo "Headers in: $INSTALL_ROOT/universal/include"
echo ""
echo "Check the real .a file names with:"
echo "  ls $UNIVERSAL_LIB_DIR"
echo "and update source/csound7_tilde/CMakeLists.txt if they don't match"
echo "libCsoundLib64.a / libsndfile.a / libsamplerate.a"

elif [[ "$OS_NAME" == "Linux" ]]; then
# =======================================================================
# Linux branch - single-arch build (whatever the host machine is)
# =======================================================================

HOST_ARCH="$(uname -m)"
LINUX_INSTALL="$INSTALL_ROOT/linux-$HOST_ARCH"

# ---------------------------------------------------------------------
# 0. toolchain check: standard distro bison/flex are already modern enough
#    (unlike macOS, which freezes an ancient bison for licensing reasons) -
#    just need them present. If you can't use your package manager (e.g.
#    sudo not available), `pip install bison-bin flex-bin cmeel-m4` gets
#    you real prebuilt binaries for all three with zero root needed - this
#    is exactly how this script's Linux branch was first verified, in a
#    minimal container with no apt access at all.
# ---------------------------------------------------------------------
if ! command -v bison >/dev/null 2>&1; then
  echo "ERROR: bison not found on PATH."
  echo "Install it (e.g. 'sudo apt install bison flex'), or if you can't use"
  echo "a package manager: 'pip install --user bison-bin flex-bin cmeel-m4'"
  echo "and make sure ~/.local/bin is on PATH."
  exit 1
fi
if ! command -v flex >/dev/null 2>&1; then
  echo "ERROR: flex not found on PATH. See the bison message above for options."
  exit 1
fi

BISON_BIN="$(command -v bison)"
FLEX_BIN="$(command -v flex)"
echo "==> using bison: $($BISON_BIN --version | head -1)"
echo "==> using flex:   $($FLEX_BIN --version | head -1)"

# flex needs GNU m4 at RUNTIME (not build time) to expand its generated
# scanner - confirmed with a real build: flex hard-fails with "fatal
# internal error, exec of /usr/bin/m4 failed" if that exact hardcoded path
# doesn't exist, even when a perfectly good m4 is elsewhere on PATH. flex's
# own error message says to set the M4 environment variable instead, which
# is what we do here if needed.
if [ ! -x "/usr/bin/m4" ]; then
  if command -v m4 >/dev/null 2>&1; then
    export M4="$(command -v m4)"
    echo "==> no m4 at /usr/bin/m4, pointing flex at $M4 via the M4 environment variable"
  else
    echo "ERROR: m4 not found (flex needs it at runtime, not just build time)."
    echo "Install it (e.g. 'sudo apt install m4'), or 'pip install --user cmeel-m4'."
    exit 1
  fi
fi

NPROC="$(nproc)"

# ---------------------------------------------------------------------
# 1. libsndfile, static - same reasoning/flags as the macOS branch, this
#    part of the build is genuinely OS-agnostic CMake.
# ---------------------------------------------------------------------
SNDFILE_SRC="$BUILD_ROOT/sndfile-static/src"
clone_once "$SNDFILE_REPO" "master" "$SNDFILE_SRC"
SNDFILE_BUILD="$BUILD_ROOT/sndfile-static/build-linux-$HOST_ARCH"
SNDFILE_INSTALL="$INSTALL_ROOT/linux-$HOST_ARCH-deps/sndfile"

echo "==> building static libsndfile for Linux ($HOST_ARCH)"
cmake -S "$SNDFILE_SRC" -B "$SNDFILE_BUILD" -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$SNDFILE_INSTALL" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_PROGRAMS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_TESTING=OFF \
  -DENABLE_EXTERNAL_LIBS=OFF \
  -DENABLE_MPEG=OFF \
  -DENABLE_CPACK=OFF
cmake --build "$SNDFILE_BUILD" --config Release -j"$NPROC"
cmake --install "$SNDFILE_BUILD" --config Release

# ---------------------------------------------------------------------
# 2. libsamplerate, static
# ---------------------------------------------------------------------
SAMPLERATE_SRC="$BUILD_ROOT/samplerate-static/src"
clone_once "$SAMPLERATE_REPO" "master" "$SAMPLERATE_SRC"
SAMPLERATE_BUILD="$BUILD_ROOT/samplerate-static/build-linux-$HOST_ARCH"
SAMPLERATE_INSTALL="$INSTALL_ROOT/linux-$HOST_ARCH-deps/samplerate"

echo "==> building static libsamplerate for Linux ($HOST_ARCH)"
cmake -S "$SAMPLERATE_SRC" -B "$SAMPLERATE_BUILD" -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$SAMPLERATE_INSTALL" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DLIBSAMPLERATE_EXAMPLES=OFF \
  -DLIBSAMPLERATE_INSTALL=ON
cmake --build "$SAMPLERATE_BUILD" --config Release -j"$NPROC"
cmake --install "$SAMPLERATE_BUILD" --config Release

SNDFILE_LIB="$SNDFILE_INSTALL/lib/libsndfile.a"
SAMPLERATE_LIB="$SAMPLERATE_INSTALL/lib/libsamplerate.a"

if [ ! -f "$SNDFILE_LIB" ]; then
  echo "ERROR: expected static libsndfile at $SNDFILE_LIB, not found."
  ls -la "$SNDFILE_INSTALL/lib" 2>/dev/null || echo "  (directory does not exist)"
  exit 1
fi
if [ ! -f "$SAMPLERATE_LIB" ]; then
  echo "ERROR: expected static libsamplerate at $SAMPLERATE_LIB, not found."
  ls -la "$SAMPLERATE_INSTALL/lib" 2>/dev/null || echo "  (directory does not exist)"
  exit 1
fi

# ---------------------------------------------------------------------
# 3. Csound 7, static, pointed at our own static sndfile/samplerate.
#    -DUSE_ALSA=OFF is Linux-specific and load-bearing: Csound's own
#    InOut/CMakeLists.txt does a REQUIRED find_package(ALSA) if USE_ALSA
#    is on (the default), which hard-fails configure without ALSA dev
#    headers installed - confirmed by hitting exactly that failure on a
#    real build. We don't want any realtime backend anyway (same as
#    disabling PortAudio/JACK/etc. above), so this is a clean fix, not a
#    workaround. -DINSTALL_PYTHON_INTERFACE=OFF because Csound's CMake
#    auto-enables it whenever python3 is found (irrelevant to us, and
#    only reachable on non-Apple builds - macOS's own CMakeLists.txt
#    disables it outright regardless of this flag).
# ---------------------------------------------------------------------
CSOUND_SRC="$BUILD_ROOT/csound-static/src"
clone_once "$CSOUND_REPO" "$CSOUND_BRANCH" "$CSOUND_SRC"
CSOUND_BUILD="$BUILD_ROOT/csound-static/build-linux-$HOST_ARCH"

echo "==> building static Csound for Linux ($HOST_ARCH)"
echo "    using SndFile:    $SNDFILE_LIB"
echo "    using SampleRate: $SAMPLERATE_LIB"

cmake -S "$CSOUND_SRC" -B "$CSOUND_BUILD" -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$LINUX_INSTALL" \
  -DBISON_EXECUTABLE="$BISON_BIN" \
  -DFLEX_EXECUTABLE="$FLEX_BIN" \
  -DSndFile_LIBRARY="$SNDFILE_LIB" \
  -DSndFile_INCLUDE_DIR="$SNDFILE_INSTALL/include" \
  -DSampleRate_LIBRARY="$SAMPLERATE_LIB" \
  -DSampleRate_INCLUDE_DIR="$SAMPLERATE_INSTALL/include" \
  -DBUILD_STATIC_LIBRARY=ON \
  -DBUILD_PLUGINS=OFF \
  -DUSE_LIBSNDFILE=ON \
  -DUSE_LIBSAMPLERATE=ON \
  -DUSE_CURL=OFF \
  -DUSE_GETTEXT=OFF \
  -DBUILD_UTILITIES=OFF \
  -DBUILD_TESTS=OFF \
  -DBUILD_INSTALLER=OFF \
  -DBUILD_DOCS=OFF \
  -DUSE_DOUBLE=ON \
  -DFAIL_MISSING=OFF \
  -DUSE_PORTAUDIO=OFF \
  -DUSE_PORTMIDI=OFF \
  -DUSE_JACK=OFF \
  -DUSE_COREMIDI=OFF \
  -DUSE_AUDIOUNIT=OFF \
  -DUSE_IPMIDI=OFF \
  -DUSE_PULSEAUDIO=OFF \
  -DUSE_PIPEWIRE=OFF \
  -DUSE_ALSA=OFF \
  -DBUILD_OSC_OPCODES=OFF \
  -DBUILD_CSBEATS=OFF \
  -DBUILD_DSSI_OPCODES=OFF \
  -DINSTALL_PYTHON_INTERFACE=OFF

cmake --build "$CSOUND_BUILD" --config Release -j"$NPROC"
cmake --install "$CSOUND_BUILD" --config Release

# Unlike macOS, Csound's own install already puts everything where a normal
# CMake project would (lib/libcsound64.a, include/csound/*.h) - no
# framework-header detour needed here. Just bring the static sndfile/
# samplerate libs+headers into that same install root, so
# CSOUND_STATIC_ROOT can point at one single self-contained directory,
# same idea as the macOS "universal" one.
mkdir -p "$LINUX_INSTALL/lib" "$LINUX_INSTALL/include"
cp "$SNDFILE_LIB" "$LINUX_INSTALL/lib/"
cp "$SAMPLERATE_LIB" "$LINUX_INSTALL/lib/"
cp -R "$SNDFILE_INSTALL/include/." "$LINUX_INSTALL/include/"
cp -R "$SAMPLERATE_INSTALL/include/." "$LINUX_INSTALL/include/"

echo ""
echo "Done. Static libraries in: $LINUX_INSTALL/lib"
echo "Headers in: $LINUX_INSTALL/include"
echo ""
echo "Real lib file names:"
ls "$LINUX_INSTALL/lib"
echo ""
echo "Csound's own static lib is named libcsound64.a here (lowercase, no"
echo "'CsoundLib64' - that spelling is Apple-only), already wired up in"
echo "source/csound7_pd/CMakeLists.txt's Linux branch."

else
  echo "ERROR: unsupported OS '$OS_NAME' - this script supports macOS and Linux only."
  echo "Windows uses a separate build process (different toolchain entirely - see the README)."
  exit 1
fi
