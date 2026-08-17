# csound7~ manual (Pure Data / plugdata)

Full reference for how `csound7~` behaves and how to talk to it. For build
instructions and repo layout, see `README.md`. For runnable examples, see
`examples/`.

## What it does

- **Universal binary** (arm64 + x86_64) on macOS, plus native builds for
  Linux (`.pd_linux`) and Windows (`.dll`) - same Csound engine and core
  logic (option-forcing order, ring buffer, MIDI queue) as the Max/MSP
  sibling, ported to Pd's own API (`m_pd.h` / `g_canvas.h`).
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
  Mac-native hybrid path format).
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
  file path, for opcodes like `soundin`/`diskin`/GEN01 driven from a
  Pd-side file chooser. See `5_diskin_player.csd` / `6_wav_table_player.csd`.
- `midi <status> <data1> <data2>` (3 numbers) → queues a raw MIDI message
  for Csound's `notein`/`massign` etc. Feed it from `notein` → your own
  reformatting into that 3-number shape upstream.
- `reset` → full `csoundReset`, then recompiles the current file from
  scratch - i.e. turns off the whole engine and restarts it from zero.
- `verbose <0-231>` → Csound's console message level (`csoundSetMessageLevel`),
  default `0` = quiet. Real compile errors and warnings always print
  regardless of this setting.
- `ksmps <N>` → forces a specific ksmps and triggers a full reset+recompile
  (see below).

## What's controlled by the `.csd` vs. what's forced by Pd

Same structural reasoning as the Max side, reproduced here since it
applies identically:

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

## Forcing ksmps (`ksmps` message)

By default ksmps comes from the `.csd` itself (see above). If you want Pd
to force a specific value instead, two equivalent ways:

- The second creation arg, numeric: `csound7~ test.csd 32` (32 = ksmps)
- A runtime message to the control inlet: `ksmps 32`

Changing it while running triggers a full reset+recompile (ksmps is only
settled at `csoundStart()` time, same reason an automatic sr change does
too).
