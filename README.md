# csound7~ — Csound 7 embedded as a monolithic, statically-linked Pure Data / plugdata external

A Pd external (`csound7~`) that embeds the Csound 7 engine **statically**
inside the binary: no separate Csound install needed on the end user's
machine, no risk of version mismatch. This is the Pure Data / plugdata
sibling of the Max/MSP external in `../MAX/` — same Csound engine, same
core logic (option-forcing order, ring buffer, MIDI queue), ported to
Pd's own API (`m_pd.h` / `g_canvas.h`) instead of the Max SDK.

This is an **aggressive first draft**, generated without being able to
compile or test the code locally (built in a Linux sandbox, no Pd/plugdata
available there). Expect 1-2 rounds of fixes after the first real build —
that's normal for a project of this complexity. Every place where an
assumption had to be made without being able to verify it is flagged with
`// VERIFY:` in `source/csound7_pd/csound7_pd.c`, or noted below.

## License

This project's own code is LGPL 2.1 (or later) — see `LICENSE`, chosen
to match Csound and libsndfile, both also LGPL, which this project
statically links (same choice as `../MAX/`, kept identical on purpose).
See `THIRD-PARTY-LICENSES.md` for the full picture: exact licenses for
Csound, libsndfile, libsamplerate (BSD-2-Clause), and Pd's own headers
(Standard Improved BSD License, Miller Puckette), where each one's source
comes from, and how the LGPL's static-linking conditions are met (short
version: keep the source and build scripts — this whole folder —
distributed alongside any compiled binary).

See `ACKNOWLEDGMENTS.md` for credit to the Csound and Pd communities and
the prior `csoundapi~` (Pure Data) and `csound~` (Max/MSP) projects this
one follows in the footsteps of.

## What it does

- **Universal binary** (arm64 + x86_64) on macOS, buildable from a single
  Apple Silicon Mac via cross-compile + `lipo` — same approach as `../MAX/`.
  The CMake setup also targets `.pd_linux` and `.dll` output names for
  Linux/Windows, but only the macOS path has been exercised at all so far.
- **Embedded Csound**: core opcodes + libsndfile + libsamplerate only, all
  static. No Csound audio/MIDI/GUI/scripting backends — Pd feeds audio
  through the API (`csoundPerformKsmps`), not through a device.
- **Audio inlets/outlets**: their count is derived automatically from
  `nchnls`/`nchnls_i` read from the `.csd`/`.orc` file passed as the first
  creation argument, exactly like the Max side. Inlet 0 always carries
  audio channel 1 *and* every control message (Pd's `CLASS_MAINSIGNALIN`
  convention); any further input channels (2..N) get their own
  signal-only inlets to its right. Outlets: one signal outlet per output
  channel, then one extra "anything" outlet on the far right for control
  output (channel dumps, see below).
- **File path**: the `.csd`/`.orc` argument (creation, `read`, `compile`)
  can be a relative path — it's resolved first against the patch's own
  folder (`canvas_getdir()`), then through Pd's own search path
  (`open_via_path`), the same two-step logic as Max's file resolution.
  Pd's file dialogs already return plain POSIX paths, so no path-cleanup
  step is needed here (unlike the Max side, which has to handle a
  Mac-native hybrid path format — see `../MAX/README.md`).
- **Control messages**: on the **leftmost inlet** (inlet 0) — a deliberate
  difference from the Max version, which uses a dedicated rightmost
  control inlet. Putting control on inlet 0 (which also carries channel 1
  audio) is itself completely idiomatic Pd; a proxy-inlet trick for a Max-
  style dedicated control inlet was judged extra risk on a first, untested
  pass. See the message list below.
- **MIDI**: via "host MIDI IO" (`csoundSetHostMIDIIO` + a read callback),
  identical mechanism to the Max side — Csound "pulls" MIDI bytes from an
  internal queue that gets filled from Pd messages, not
  `csoundPushMidiMessage` (which doesn't exist in the Csound 7 API).
- **Live coding**: `csoundCompileOrc` at runtime, explicit only (never
  automatic), triggered by a `compile` message.
- **Sample rate**: automatic sync with Pd, automatic reset+recompile if it
  changes while running.
- **Ksmps**: controlled by the `.csd`/`.orc` itself by default, exactly
  like running Csound standalone — an internal ring buffer decouples
  Csound's blocks from Pd's block size. Only forced from the Pd side if
  you explicitly ask for it (creation arg or `ksmps` message, see below).
- **Csound tables <-> Pd arrays**: bidirectional bridging, Pd's equivalent
  of the Max side's `buffer~` <-> table bridge, using Pd's `t_garray` /
  `g_canvas.h` API instead of `ext_buffer.h`.
- **Dynamic plugins**: a `plugins/` folder next to the external, loaded
  with `csoundLoadPlugins()` on every startup — same idea as the Max
  side. Drop extra opcode binaries (`.dylib`/`.so`/`.dll`) in there (e.g.
  faust, fluidsynth) with no need to rebuild anything. Finding the
  external's own folder uses Pd's `class_gethelpdir()` — confirmed (not
  guessed) by reading Pd's own `m_class.c` source directly: Pd's loader
  sets a class's "help directory" to the folder the `.pd_darwin`/
  `.pd_linux`/`.dll` file was loaded from, right before running this
  file's setup function, so it doubles as a reliable way to find "where
  am I" (see the comment above `csound7_load_plugins_dir()` in the
  source, and `plugins/README.md`).

## Control message protocol (leftmost inlet)

Send these messages to the leftmost inlet (inlet 0), which doubles as
audio channel 1's signal inlet:

- `<name> <value>` → sets the Csound control channel called `<name>` to
  `<value>` (`csoundSetControlChannel`). Just a plain message like
  `amp 0.3` or `freq $1`. Read it inside Csound with
  `kval invalue "name"` or `kval chnget "name"`. (Any message whose first
  word isn't one of the reserved ones below is treated this way, with the
  message's own name as the channel name.)
- **To receive values from Csound back into Pd** you must use the
  `outvalue "name", kval` opcode (not plain `chnset`) — it's the only one
  that triggers a push notification to the host.
- `event i 1 0 -1 440 0.5` → turns on (held note, negative duration) an
  instance of instrument 1 with those parameters (`csoundEventString`).
- `event i -1 0 0` → turns off the held instance of instrument 1 (standard
  Csound convention: same instrument number, negated).
- `start` → resumes the whole performance (if paused with `stop`).
- `stop` → pauses the whole performance in place: outputs silence, no
  k-cycles advance until `start`. Not the same as `reset`.
- `channels` → the external dumps a `channel <name> <type>` message out
  the rightmost (control) outlet for every channel currently alive in
  Csound.
- `read <path>` → sets the orchestra file used by `compile` (relative
  paths resolved as described above).
- `compile` → recompiles by reading the file set with `read` (or the one
  passed as a creation argument). Never automatic, only on command.
- `compile <path>` → sets the path and recompiles in one shot.
- `buf2tab <array name> <table number>` → copies samples from a Pd array
  into a Csound f-table (created/resized as needed).
- `tab2buf <table number> <array name>` → copies the other way, from the
  Csound table into the array. **Caveat**: unlike `buf2tab`, this does
  **not** resize the target array — make it at least as long as the table
  first (e.g. via an `array size` message), or only the first N points get
  copied (see the `// VERIFY` note in the source, the one spot the array-
  resize call itself wasn't confirmed).
- `diskinfile <channel name> <path>` → sets a Csound STRING channel to a
  file path, for opcodes like `diskin2` driven from a Pd-side file
  chooser. See `5_diskin_player.csd` / `6_wav_table_player.csd`.
- `midi <status> <data1> <data2>` (3 numbers) → queues a raw MIDI message
  for Csound's `notein`/`massign` etc. Feed it from `midiin` → your own
  reformatting into that 3-number shape upstream.
- `reset` → full `csoundReset`, then recompiles the current file from
  scratch.
- `verbose <0-231>` → Csound's console message level (`csoundSetMessageLevel`),
  default `0` = quiet. Real compile errors and warnings always print
  regardless of this setting.
- `ksmps <N>` → forces a specific ksmps and triggers a full reset+recompile
  (see below).

## What's controlled by the `.csd` vs. what's forced by Pd

Same structural reasoning as the Max side (`../MAX/README.md`), reproduced
here since it applies identically:

- **sr** — always forced to match Pd's current sample rate, with automatic
  reset+recompile if it changes.
- **nchnls / nchnls_i** — always forced, but the values come *from* the
  `.csd` itself, parsed out before the object's inlets/outlets are even
  created (Pd's I/O count is also fixed at creation time).
- **`-n` (nosound), `-+rtaudio=null`, `-+rtmidi=null`, `-M0`** — always
  forced, applied *after* your own `<CsOptions>`, since these are what
  make host-driven audio/MIDI via the API possible at all.
- **ksmps** — your file's own `ksmps = N` line decides by default. Only
  overridden by a ksmps creation arg or the `ksmps` message.
- Everything inside `<CsInstruments>`/`<CsScore>` is entirely yours.

`<CsOptions>` works, but — same ordering constraint as the Max side —
`csoundStart()` runs before `csoundCompileCSD()` on purpose, so the
external reads `<CsOptions>` itself as plain text and applies each flag
via the API before `csoundStart()`, rather than relying on
`csoundCompileCSD()`'s own (too-late) handling of it.

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

Byte-identical script to `../MAX/scripts/build_csound_static.sh` (kept as
a separate copy on purpose, so `PD/` can move to its own repo later
without depending on `MAX/`). Clones `csound/csound` (`develop` branch —
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
runs automatically in CI on every push that touches `PD/` (see
`../.github/workflows/pd-linux-build.yml`), building the real x86_64
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

Implemented, but **unverified** - written without access to a Windows
machine, meant to be proven for real by `../.github/workflows/
windows-build.yml`'s first actual run (or by you, on a real Windows box).
Same "aggressive first draft, expect a fix-up round" situation the macOS
side was in before its first real compile - the difference here is there's
no way for me to close that loop myself the way I did for Linux (no
Windows machine available), so this genuinely needs your first CI run's
logs before it's proven, not just a formality.

What's actually implemented:

- `scripts/build_csound_static.ps1` - PowerShell counterpart to
  `build_csound_static.sh`, using the Visual Studio CMake generator and
  `winflexbison` (the standard Windows port of GNU bison/flex) instead of
  Homebrew/apt.
- The MSVC-doesn't-allow-unresolved-symbols-in-a-DLL problem (macOS and
  Linux both tolerate this by default or with one flag - `-undefined
  dynamic_lookup` / nothing at all respectively; MSVC refuses outright)
  is solved via `source/csound7_pd/pd_stub.def`: a hand-written list of
  exactly the Pd API symbols `csound7_pd.c` actually calls (confirmed by
  grepping the source, not guessed), which `source/csound7_pd/
  CMakeLists.txt` turns into a minimal stand-in `pd.lib` at build time
  (`lib.exe /DEF:...`, via CMake's own `CMAKE_AR`). This is the same
  standard Windows technique used any time you need to link against
  symbols a host process will provide at runtime but don't have (or don't
  want to depend on downloading) the real DLL at build time - real
  resolution still only happens later, when Pd itself loads this DLL, same
  as the other two platforms. Confirmed via `pure-data/pd.cmake` (the
  official-ish Pd CMake build tooling) that a real `pd.lib` import library
  is genuinely the standard mechanism here, not a shortcut unique to this
  project - Pd just doesn't ship one anywhere a plain `git clone` (what
  this project's `FetchContent` already uses for headers) can reach, since
  Pd has no GitHub Releases at all, only a separate site
  ([puredata.info](https://puredata.info)) for actual binary downloads,
  which would make CI depend on an external, version-specific download.
  The stand-in sidesteps that dependency entirely.
- Static Csound/libsndfile/libsamplerate library file names on Windows are
  looked up with `find_library()` trying both the with-and-without-"lib"-
  prefix spellings, rather than a single hardcoded guess - `.lib` naming
  conventions differ from `.a` in ways not confirmed here (see the
  CMakeLists.txt comment on this).

`../.github/workflows/windows-build.yml` runs this on a `windows-latest`
GitHub Actions runner (as a separate job from the Max side) on every push
that touches `PD/`. If it fails on its first run, that's expected and
useful - the logs are exactly what's needed to fix whichever of the
UNVERIFIED spots (flagged inline in `build_csound_static.ps1` and
`pd_stub.def`) turns out to be wrong.

## Files in this repo

- `CMakeLists.txt` — top level, fetches Pd's headers and bridges into
  `source/csound7_pd`.
- `source/csound7_pd/` — the external's source code
  (`csound7_pd.c`, `CMakeLists.txt`).
- `scripts/build_csound_static.sh` — static universal Csound build,
  identical to the Max side's copy.
- `examples/1_sine_two_controls.csd` / `.pd` — one oscillator, two live
  controls (`freq`, `amp`).
- `examples/2_audio_in_out.csd` / `.pd` — audio in -> audio out, with a
  `gain` control.
- `examples/3_buffer_player.csd` / `.pd` — Pd array -> table sample player
  (`buf2tab`, then `phasor`+`tablei`), with `rate`/`amp` controls.
- `examples/4_midi_synth.csd` / `.pd` — one-oscillator MIDI synth with an
  envelope, driven by `midi <status> <data1> <data2>` messages.
- `examples/5_diskin_player.csd` / `.pd` — file-driven playback via
  `diskinfile` + `diskin2`.
- `examples/6_wav_table_player.csd` / `.pd` — table-based sample playback
  loaded from a `.wav` file.
- `plugins/` — empty folder (with README) where you drop extra opcode
  binaries loaded dynamically.
- `externals/csound7~.pd_darwin` / `externals/csound7~.pd_linux` — the
  built externals (not tracked in git, see `.gitignore` — these are
  rebuilt from source, not shipped in the repo itself).
- `../.github/workflows/pd-linux-build.yml` — CI: builds and sanity-checks
  the Linux external on every push that touches `PD/`.

These are untested first drafts, same as everything else in this
project's first pass — expect to send back the real compile/console
output if any opcode name or rate mismatch trips something up.

## References

Sources actually fetched and read directly (not from memory) while
building this:

- [`csound/csound`, `develop` branch, `include/csound.h`](https://raw.githubusercontent.com/csound/csound/develop/include/csound.h)
  — the real Csound 7 C API, same references used on the Max side (see
  `../MAX/README.md` for the detailed list — this port reuses that same
  Csound-side logic unchanged, only the host-glue code differs).
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
