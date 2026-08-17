# Acknowledgments

`csound7~` for Pure Data / plugdata exists on the shoulders of decades of
work by the Csound community, the Pd community, and the people who built
the Csound-in-a-host pattern this project follows. This isn't an
exhaustive list of every contributor ever - just the people and prior
projects most directly relevant to what's being built here. See
`../MAX/ACKNOWLEDGMENTS.md` for the Max/MSP side's own version of this
file - same people, different weighting, since here the Pd lineage is
the direct one, not a side note.

## Pure Data itself

- **Miller Puckette** - created Pure Data at UCSD in the mid-1990s (after
  originating the Max lineage itself at IRCAM in the 1980s), the common
  ancestor of the Pd side of this whole project, the same role Barry
  Vercoe plays for the Csound side below. Still Pd's main author.
- **The plugdata team, led by Timothy Schoen** - built
  [plugdata](https://github.com/plugdata-team/plugdata), Pd as a plugin
  with a modern GUI, which is what this project's Pd examples were
  actually built and tested against.

## Csound itself

- **Barry Vercoe** - created Csound at MIT in 1985, the common ancestor
  of this entire lineage.
- **Victor Lazzarini** - Csound co-developer; wrote **`csoundapi~`**,
  the Csound-in-Pure-Data external (part of
  [csound/csound_pd](https://github.com/csound/csound_pd)) - the
  **direct precedent** for this side of the project (unlike the Max
  version, where this was only a future reference - here it's the real
  thing this external follows in the footsteps of). Also documented the
  Csound 6 -> 7 host API migration this project relies on, and is
  credited as a `csound.h` API author.
- **John P. ffitch** - long-time Csound maintainer, credited as one of
  the authors of the Csound API (`csound.h`) this project is built on.
- **Steven Yi** - Csound co-developer; maintained **`csound~` for
  Max/MSP** ([csound/csound_tilde](https://github.com/csound/csound_tilde))
  - the direct precedent for this project's own Max/MSP side (see
  `../MAX/ACKNOWLEDGMENTS.md`), useful here mainly as a cross-reference
  for how the same engine gets hosted on the "other" patcher
  environment. Credited as a `csound.h` API author.
- **Iain Duncan** - built **`csound6~`**
  ([iainctduncan/csound_max](https://github.com/iainctduncan/csound_max)),
  a Max/MSP object using the Csound 6 API, described by its own author as
  largely a port of Lazzarini's Pd object below - i.e. the Max version
  followed the Pd version historically, the reverse of this repo's own
  order (Max side built first, Pd side following).
- **Michael Gogins** - Csound developer, `csound.h` API author, extensive
  work on Csound hosting (including browser/WebAssembly embedding,
  conceptually similar to what this project does for Pd/Max).
- **Andres Cabrera** - Csound developer, `csound.h` API author, built
  `iCsound` and mobile (iOS/Android) Csound hosting examples - another
  precedent for "embed Csound inside someone else's runtime via the API."
- **John D. Ramsdell** and **István Varga** - credited as `csound.h` API
  authors alongside the above.
- **Iain McCurdy** - prolific author of Csound tutorials and example
  `.csd` patches used across the Csound community for learning the
  language; not part of the host-API lineage above, but part of why
  Csound is approachable enough for a project like this to exist.

## Direct prior art

- [`csound/csound_pd`](https://github.com/csound/csound_pd) -
  `csoundapi~` for Pure Data, by Victor Lazzarini. The direct reference
  for this side of the project - its source is the natural next read if
  you want to compare approaches (its own host-glue code obviously
  predates this one and may solve some of the same problems
  differently).
- [`csound/csound_tilde`](https://github.com/csound/csound_tilde) -
  `csound~` for Max/MSP, the equivalent project on the Max side of this
  same repo (`../MAX/`).
- [`iainctduncan/csound_max`](https://github.com/iainctduncan/csound_max) -
  Iain Duncan's `csound6~`, itself described as a port of Lazzarini's Pd
  object - the same Max<->Pd relationship this repo has, just built in
  the opposite direction historically.

## SDKs and libraries

See `THIRD-PARTY-LICENSES.md` for the licenses; credit here for the
software itself:

- **Miller Puckette and the Pd community** - Pd's own API (`m_pd.h`,
  `g_canvas.h`) this external is built against.
- **The plugdata team** - the environment this side of the project was
  actually built and tested in.
- **Erik de Castro Lopo** - author of libsndfile and libsamplerate, both
  statically linked into this project's Csound build (identical to the
  Max side, this part of the build is host-agnostic).

---

Sources for the attributions above:
[csound/csound_pd](https://github.com/csound/csound_pd),
[csound/csound_tilde](https://github.com/csound/csound_tilde),
[Csound API reference (csound.h)](https://csound.com/docs/api/csound_8h.html),
[Victor Lazzarini - Wikipedia](https://en.wikipedia.org/wiki/Victor_Lazzarini),
[Pure Data - Wikipedia](https://en.wikipedia.org/wiki/Pure_Data),
[plugdata GitHub](https://github.com/plugdata-team/plugdata).
