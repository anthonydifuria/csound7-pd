# Third-party licenses

`csound7~` for Pure Data / plugdata statically links against several
third-party libraries, and its build system fetches Pd's own headers.
This file lists exactly what's used, under which license, where the
source comes from, and - for the two LGPL components - how this project
satisfies the LGPL's static-linking requirements. Same structure as the
Max/MSP side of this repo (`../MAX/THIRD-PARTY-LICENSES.md`), only the
last entry differs (Pd's headers instead of the Max SDK's).

This is not legal advice. It's a best-effort summary based on the plain
text of each license, put together because static linking of LGPL code
specifically needs a bit of care. If you plan to distribute this project
commercially or in a context where the details matter, get an actual
legal opinion.

## Summary table

| Component      | License          | How it's used                          |
|-----------------|-----------------|-----------------------------------------|
| Csound 7        | LGPL 2.1 (or later) | statically linked (`libCsoundLib64.a`) |
| libsndfile      | LGPL 2.1 (or later) | statically linked (`libsndfile.a`)     |
| libsamplerate   | BSD-2-Clause     | statically linked (`libsamplerate.a`)  |
| Pure Data (`pure-data`) | Standard Improved BSD License (Miller Puckette) | headers (`m_pd.h`, `g_canvas.h`) used at build time, fetched via `FetchContent`, not vendored in this repo; the external does not link against Pd itself (see below) |

## Csound 7 - LGPL 2.1 (or later)

- Source: <https://github.com/csound/csound> (this project builds the
  `develop` branch by default - see `scripts/build_csound_static.sh`; if
  you've rebuilt against a specific tag or commit, record it here or in
  your own build notes, since that's the exact source your binary
  corresponds to. Csound's own version banner prints its git commit hash
  at startup - worth saving whenever you rebuild for distribution).
- License text: identical to the LGPL 2.1 reproduced in this project's
  own `LICENSE` file, fetched directly from
  <https://raw.githubusercontent.com/csound/csound/develop/COPYING>.
- Identical build, identical library, as the Max/MSP side of this repo -
  see `../MAX/scripts/build_csound_static.sh` (this side's copy of the
  script is byte-identical, kept separate on purpose so `PD/` can move to
  its own repo later without depending on `MAX/`).

## libsndfile - LGPL 2.1 (or later)

- Source: <https://github.com/libsndfile/libsndfile>
- License text: same LGPL 2.1, fetched directly from
  <https://raw.githubusercontent.com/libsndfile/libsndfile/master/COPYING>.

### How the LGPL's static-linking requirement is satisfied

The LGPL 2.1 (section 6) allows linking LGPL code into a non-free/closed
combined work, statically or dynamically, but requires that the person
who receives that combined work is able to modify the LGPL library and
relink it. Since this whole project builds Csound and libsndfile *from
their own upstream source* via `scripts/build_csound_static.sh`, rather
than vendoring a prebuilt blob, anyone who has this repository already
has everything needed to swap in a modified Csound/libsndfile and rebuild
the external from scratch - which is exactly what section 6(a) asks for.

This only holds as long as **the source and build scripts are
distributed together with the compiled external** - if `csound7~.pd_darwin`
(or the Linux/`.pd_linux`/Windows `.dll` equivalent) is ever handed out on
its own, without this repository, the LGPL's conditions aren't met unless
one of the license's other options is used instead (e.g. a standing public
repository link, or a written offer valid for at least three years to
provide the corresponding source - see section 6, options a-e, in the
full text). The simplest way to stay compliant: keep this project's
source publicly available (e.g. a public GitHub repo) and always point to
it alongside the compiled binary.

## libsamplerate - BSD-2-Clause

- Source: <https://github.com/libsndfile/libsamplerate>
- Copyright (c) 2012-2016, Erik de Castro Lopo <erikd@mega-nerd.com>
- License text, fetched directly from
  <https://raw.githubusercontent.com/libsndfile/libsamplerate/master/COPYING>:

```
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

No copyleft obligation - permissive, just requires keeping this notice
around, which this file does. Identical dependency/build as the Max/MSP
side of this repo (host-agnostic part of the Csound build).

## Pure Data (`pure-data`) - Standard Improved BSD License

- Source: <https://github.com/pure-data/pure-data> (fetched automatically
  at configure time via CMake's `FetchContent`, shallow clone of `master`
  - not vendored/copied into this repository; only `m_pd.h` and
  `g_canvas.h` are actually used, as headers, at compile time).
- Copyright: Miller Puckette and others.
- License text, fetched directly from
  <https://raw.githubusercontent.com/pure-data/pure-data/master/LICENSE.txt>:

```
This software is copyrighted by Miller Puckette and others.  The following
terms (the "Standard Improved BSD License") apply to all files associated with
the software unless explicitly disclaimed in individual files:

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided
   with the distribution.
3. The name of the author may not be used to endorse or promote
   products derived from this software without specific prior
   written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
```

No copyleft obligation - this is why `csound7_pd.c` itself doesn't need
to use this same license just because it's built against these headers;
LGPL was chosen for this project's own code as a separate decision (see
`LICENSE`), for consistency with Csound/libsndfile rather than because
Pd's own license requires it. Note also that, unlike the Max SDK on the
other side of this repo, the external does **not** link against any Pd
library at all (see `source/csound7_pd/CMakeLists.txt`'s
`-undefined dynamic_lookup`) - Pd's own process resolves every
`pd_*`/`class_*`/`atom_*`/etc. symbol at load time, so these headers are
a compile-time-only dependency, an even lighter footprint than the Max
side's relationship to `max-sdk-base`.

## What about Pd / plugdata itself?

Neither Pure Data nor plugdata is distributed by this project - end
users need their own copy (both are free/open-source, unlike Max) to
load `csound7~` at all, same as any other third-party external. Nothing
here redistributes Pd's or plugdata's own application code, only an
addon built against Pd's publicly published API headers, which is
exactly what those headers are for.
