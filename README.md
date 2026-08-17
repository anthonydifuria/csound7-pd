# csound7~ — Csound 7 embedded as a monolithic, statically-linked Pure Data / plugdata external

*by Anthony Di Furia — anthonydifuria.sound@gmail.com*

A Pd external (`csound7~`) that embeds the Csound 7 engine **statically**
inside the binary: no separate Csound install needed on the end user's
machine, no risk of version mismatch. This is the Pure Data / plugdata
sibling of the `csound7-max` repo — same Csound engine, same core logic
(option-forcing order, ring buffer, MIDI queue), ported to Pd's own API
(`m_pd.h` / `g_canvas.h`) instead of the Max SDK.

**For the full manual** (message protocol, what's forced by Pd vs. left
to the `.csd`) **see `MANUAL.md`.** This file covers building and
installing the external. Runnable examples are in `examples/`.

## License

This project's own code is LGPL 2.1 (or later) — see `LICENSE`, chosen
to match Csound and libsndfile, both also LGPL, which this project
statically links (same choice as the `csound7-max` repo, kept identical
on purpose).
See `THIRD-PARTY-LICENSES.md` for the full picture: exact licenses for
Csound, libsndfile, libsamplerate (BSD-2-Clause), and Pd's own headers
(Standard Improved BSD License, Miller Puckette), where each one's source
comes from, and how the LGPL's static-linking conditions are met (short
version: keep the source and build scripts — this whole folder —
distributed alongside any compiled binary).

See `ACKNOWLEDGMENTS.md` for credit to the Csound and Pd communities and
the prior `csoundapi~` (Pure Data) and `csound~` (Max/MSP) projects this
one follows in the footsteps of.

## Prerequisites to build

1. Xcode (Command Line Tools at least): `xcode-select --install`
2. CMake >= 3.25: `brew install cmake`
3. Git

You don't need to download Pd's source by hand: the real `pure-data`
repository is fetched automatically (shallow clone, headers only) via
CMake's `FetchContent` at configure time — you just need an internet
connection the first time.

## Build steps

### 1. Static build of Csound 7 (universal)

```bash
cd scripts
./build_csound_static.sh
```

Same script as the `csound7-max` repo's own
`scripts/build_csound_static.sh` (kept as a separate copy on purpose, so
each repo stays self-contained). Clones `csound/csound` (`develop` branch —
if a stable `7.x` tag exists by the time you read this, pin to that
instead), builds libsndfile and libsamplerate from source as static
libraries first (needed to stop Csound's own `find_library()` from
picking up Homebrew's dynamic versions instead), then builds Csound
itself twice (arm64/x86_64) and merges with `lipo`. Output in
`build/csound-install/universal/{lib,include}`. If you already built this
for the Max side, you can point here at that same output instead of
rebuilding, via `-DCSOUND_STATIC_ROOT=...` (see step 2).

### 2. Build the external

```bash
mkdir -p build/external
cd build/external
cmake -G "Unix Makefiles" ../..
cmake --build . --config Release
```

Add `-DCSOUND_STATIC_ROOT=/path/to/existing/universal/build` if reusing
the Max side's already-built Csound instead of rebuilding it here.

If you'd rather see errors in an IDE:

```bash
cmake -G Xcode ../..
cmake --build . --config Release
```

The compiled external comes out as `csound7~.pd_darwin` (universal
arm64+x86_64) directly in `PD/externals/`. On Apple Silicon you might need
to ad-hoc sign it if Pd/plugdata complains:

```bash
codesign --force --deep -s - csound7~.pd_darwin
```

### 3. Installation

Copy (or symlink) `csound7~.pd_darwin` and the `examples/` folder into
wherever your Pd/plugdata install expects externals — e.g. a folder added
to Pd's search path in Preferences, or right next to the patches that use
it (Pd's own patch-relative search already covers that case, see the file
path resolution logic above).

## Linux

Verified with a real build (not just assumed from reading the CMake files)
in a Linux sandbox: static Csound configured and built clean, the external
itself configured and linked clean, and the resulting `csound7~.pd_linux`
checked with `nm`/`file` - the setup symbol Pd's loader looks for
(`csound7_tilde_setup`) is exported, Csound's own API functions are
genuinely linked in statically rather than left as external references,
and every remaining undefined symbol in the binary is a legitimate Pd API
call (`gensym`, `class_new`, `atom_getfloat`, `post`, `canvas_getdir`,
`garray_*`, `outlet_*`, `dsp_addv`, `open_via_path`, ...) that Pd's own
process resolves at load time - nothing unexpected missing. This also
runs automatically in CI on every push (see
`.github/workflows/linux-build.yml`), building the real x86_64
target most Linux/plugdata users need (the sandbox verification above was
on aarch64, since that's what was available for local testing - same
build steps either way).

Two real, non-obvious things the build needs, both already handled by
`scripts/build_csound_static.sh`'s Linux branch:

- Csound's own CMakeLists.txt defaults `USE_ALSA` to `ON`, which on Linux
  triggers a `REQUIRED` search for ALSA's dev headers - configure hard-fails
  without them installed, even though we don't want any realtime backend
  anyway (same reasoning as PortAudio/JACK being disabled everywhere). The
  script passes `-DUSE_ALSA=OFF` to sidestep this.
- `flex`, once actually invoked (not just version-checked), needs GNU `m4`
  at a hardcoded path (`/usr/bin/m4`) and fails with a cryptic "fatal
  internal error" if it's not exactly there, even with a perfectly good
  `m4` elsewhere on `PATH`. The script points flex at the real one via the
  `M4` environment variable if needed.

Also worth knowing: Csound's own static library is **not** named the same
thing on every OS. `CMakeLists.txt`'s own `CSOUNDLIB` variable is only
`"CsoundLib64"` (→ `libCsoundLib64.a`) on Apple; everywhere else, including
Linux, it's plain `"csound64"` (→ `libcsound64.a`). Already handled in
`source/csound7_pd/CMakeLists.txt` (`if(APPLE) ... elseif(UNIX) ...`) -
worth knowing if you ever see a "library not found" warning that mentions
the wrong one of these two names.

Toolchain: any reasonably modern `bison`/`flex`/`cmake`/`m4` work (unlike
macOS, Linux distros don't freeze bison at an ancient version, so there's
no Homebrew-equivalent detour needed) - `sudo apt install bison flex m4
cmake build-essential git` covers it on Debian/Ubuntu. If you can't use a
package manager at all (no root), the sandbox verification above was done
entirely via prebuilt binaries from PyPI instead - `pip install --user
bison-bin flex-bin cmeel-m4` gets you real, working binaries for all
three with zero system packages touched (make sure `~/.local/bin` is on
`PATH` afterward).

Build steps are otherwise identical to the macOS ones above, just without
the universal-binary/`lipo` part (Linux only builds for the host's own
architecture) and without needing Xcode/Homebrew - run
`scripts/build_csound_static.sh` (its Linux branch activates automatically
based on `uname`), then the same `mkdir build/external && cmake ... &&
cmake --build ...` from step 2. Output is `csound7~.pd_linux`, directly
usable in Pd or plugdata on Linux - no codesigning step needed (that's a
macOS-only concept).

## Windows

Builds successfully via `scripts/build_csound_static.ps1` (PowerShell,
MSVC toolchain) and `.github/workflows/windows-build.yml` (GitHub Actions
`windows-latest` runner) - verified with real CI runs. This side needed
more real fixes than the Max one, since Pd itself doesn't ship a Windows
import library the way the Max SDK ships prebuilt `.lib` files:

- The MSVC-doesn't-allow-unresolved-symbols-in-a-DLL problem (macOS and
  Linux both tolerate this by default or with one flag - `-undefined
  dynamic_lookup` / nothing at all respectively; MSVC refuses outright)
  is solved via `source/csound7_pd/pd_stub.def`: a hand-written list of
  exactly the Pd API symbols `csound7_pd.c` actually calls, which
  `source/csound7_pd/CMakeLists.txt` turns into a minimal stand-in
  `pd.lib` at build time (`lib.exe /DEF:...`, via CMake's own
  `CMAKE_AR`). Real symbol resolution still only happens later, when Pd
  itself loads this DLL, same as the other two platforms. Confirmed via
  `pure-data/pd.cmake` (the official-ish Pd CMake build tooling) that a
  real `pd.lib` import library is genuinely the standard mechanism here -
  Pd has no GitHub Releases, only a separate site
  ([puredata.info](https://puredata.info)) for binary downloads, which
  would make CI depend on an external, version-specific download. The
  stand-in sidesteps that entirely.
- CMake couldn't auto-detect Visual Studio on the runner - switched to
  the Ninja generator with `ilammy/msvc-dev-cmd@v1` setting up the
  compiler on `PATH` explicitly instead.
- Csound's own CMakeLists.txt hard-fails on MSVC without a `dirent.h`
  (a POSIX header MSVC doesn't ship) - fixed by fetching the well-known
  `tronkko/dirent` single-header shim and pointing the compiler at it
  (both via `CMAKE_INCLUDE_PATH`, for CMake's own check, and via the
  `%INCLUDE%` environment variable, for the actual compile).
- `csound7_pd.c` used `pthread_mutex_t`/`strcasecmp`/`close()` directly -
  none of them exist on MSVC. Shimmed locally with a Win32
  `CRITICAL_SECTION`-backed mutex, `_stricmp`, and `_close`, active only
  `#ifdef _WIN32`.
- The final link failed on two fronts: `pd_stub.def` was missing the
  `class_gethelpdir` symbol (added after the plugin-loading feature was
  implemented, forgot to add it there too), and Csound's static lib
  bundles a UDP server/console feature that needs `ws2_32.lib`
  (Winsock), not linked by default.
- Static Csound/libsndfile/libsamplerate library file names on Windows
  are looked up with `find_library()` trying both the
  with-and-without-"lib"-prefix spellings, rather than a single
  hardcoded guess.

## Files in this repo

- `MANUAL.md` — full reference: message protocol, `.csd` vs. Pd-forced
  parameters.
- `CMakeLists.txt` — top level, fetches Pd's headers and bridges into
  `source/csound7_pd`.
- `source/csound7_pd/` — the external's source code
  (`csound7_pd.c`, `CMakeLists.txt`, `pd_stub.def` for Windows).
- `scripts/build_csound_static.sh` / `.ps1` — static Csound build
  (macOS+Linux / Windows).
- `examples/` — six runnable example patches + matching `.csd` files:
  a two-control oscillator, audio in/out with gain, an array/table
  player, a MIDI synth, disk-streamed playback, and table-based WAV
  playback.
- `plugins/` — empty folder (with README) where you drop extra opcode
  binaries loaded dynamically.
- `externals/` — where the compiled external lands after a build
  (`csound7~.pd_darwin` / `.pd_linux` / `.dll`) - not tracked in git, see
  `.gitignore`, rebuilt from source.
- `.github/workflows/` — CI: builds macOS, Linux, and Windows on every
  push.

## References

Sources actually fetched and read directly (not from memory) while
building this:

- [`csound/csound`, `develop` branch, `include/csound.h`](https://raw.githubusercontent.com/csound/csound/develop/include/csound.h)
  — the real Csound 7 C API, same references used on the Max side (see
  the `csound7-max` repo's own README for the detailed list — this port
  reuses that same Csound-side logic unchanged, only the host-glue code
  differs).
- [`pure-data/pure-data`, `master` branch, `src/m_pd.h` and `src/g_canvas.h`](https://github.com/pure-data/pure-data)
  — Pd's own external API, `CLASS_MAINSIGNALIN`, `dsp_addv`, `t_garray`/
  `garray_getfloatwords`/`garray_redraw`, `canvas_getdir`, `open_via_path`
  — all long-established, documented patterns third-party Pd externals
  (including `csoundapi~` itself, see `ACKNOWLEDGMENTS.md`) rely on.
- [`pure-data/pure-data`, `master` branch, `src/m_class.c`](https://github.com/pure-data/pure-data/blob/master/src/m_class.c)
  — fetched specifically to confirm `class_gethelpdir()`'s real behavior
  before relying on it for plugin-folder discovery (see
  `csound7_load_plugins_dir()`): `class_donew()` sets a class's
  `c_externdir` from the loader-set `class_extern_dir` symbol at
  `class_new()` time, which Pd's own loader points at the loaded
  external's own directory beforehand — this resolved what used to be a
  `// VERIFY:` spot in this file. The one remaining `// VERIFY:` in the
  source is the exact array-resize call in `tab2buf`, not yet confirmed
  against a real build/run.
