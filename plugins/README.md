# plugins/

Folder for extra Csound opcodes loaded **dynamically** at runtime
(`csoundLoadPlugins()`, called by the external as soon as it creates the
Csound instance — see `csound7_load_plugins_dir()` in `csound7_pd.c`).
Same mechanism as the Max/MSP side of this repo (`../../MAX/plugins/`),
just found via Pd's own API instead of the Max SDK's bundle-path calls
(`class_gethelpdir()` — see the long comment above the function for how
that's confirmed to return this external's own folder).

The external's static core does NOT include faust, fluidsynth, STK, OSC,
etc. — if you need them, drop the plugin binary compiled for Csound 7 in
here (`.dylib` on macOS, matching architecture/ABI of your build — if you
built universal you need either a universal plugin or at least the right
one for your machine; `.so` on Linux, `.dll` on Windows), and it will be
loaded automatically on the next startup, with no rebuild required.

Handy source of precompiled binaries for many extra opcodes:
https://github.com/csound-plugins/csound-plugins

## Dynamic loading verification test

Still needed, here, a patch/procedure that:

1. drops a known, simple plugin binary into this folder,
2. restarts/recreates the `csound7~` object (or reloads the patch),
3. verifies from the Pd/plugdata console (`csoundSetMessageCallback` —
   already wired to Pd's `post()`, see `cs7_msg_callback` in the code)
   that the plugin loaded without errors,
4. calls one of the new opcodes from a test orchestra and confirms it
   makes sound.

This piece isn't tested yet — the actual test with a real plugin binary is
left for the next round once the base build works. The lookup logic itself
(`class_gethelpdir()`) was confirmed by reading Pd's own `m_class.c`
source directly, unlike the Max side's equivalent, which is still marked
as an assumption pending a real build (see `../../MAX/plugins/README.md`).
