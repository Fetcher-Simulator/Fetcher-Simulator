# Fetcher Studio sample-library shortlist

Researched 2026-10-29. These are source/licensing and technical candidates,
not installed instruments or listening-approved final choices. No audio is
included in this change. Preserve source licenses and credit creators when
building our own bank; pin archive version, SHA-256 and retained sample paths
at acquisition. Original library sizes below are not our future bundle size.

| Use | Source and stated license | Why evaluate it | Limits to check |
| --- | --- | --- | --- |
| Electric guitar, first choice for our amplifier | [FreePats FSBS Electric Guitar Direct](https://freepats.zenvoid.org/ElectricGuitar/clean-electric-guitar.html), CC0 1.0 | Raw Fender guitar signal, intended for external amplification; SFZ/WAV source listed as 153 MiB archive, 314 MiB unpacked, version 2022-09-11 | Inspect actual velocity/note coverage, attacks, noise and articulations; this is not proof of convincing palm-muted metal. |
| Alternate electric guitar character | [Karoryfer Black And Green Guitars](https://shop.karoryfer.com/pages/free-black-and-green-guitars), [CC0 source license](https://github.com/sfzinstruments/karoryfer.black-and-green-guitars/blob/main/LICENSE) | Two hollowbody guitars, plucks/staccato/feedback, more than 1,500 samples, about 500 MB | Indie/twang character; not a substitute for a tight solidbody/palm-mute library. |
| Fingered and picked electric bass | [Karoryfer Black And Blue Basses](https://shop.karoryfer.com/pages/free-black-and-blue-basses), CC0 | Two five-string basses: fingered hollowbody and picked solidbody, over 2,000 samples and over 1 GB | Load only needed zones/articulations. Separate slap/pop coverage still needed. |
| Slap/funk bass candidate | [Karoryfer Growlybass](https://github.com/sfzinstruments/karoryfer.growlybass), CC0-1.0 repository license | Additional bass source for the funk bank | Inspect actual SFZ articulation mapping and audition before selecting it. |
| Rock kit, first candidate | [Karoryfer Big Rusty Drums](https://shop.karoryfer.com/pages/free-big-rusty-drums), [CC0 source license](https://github.com/sfzinstruments/karoryfer.big-rusty-drums/blob/main/LICENSE) | Full acoustic kit, multiple toms, snare articulations, hats with several openness states and cymbal chokes; over 4,400 samples, 2.3 GB | Curate stick hits and suitable cymbals first; no need to ship every brush/mallet/mechanical variant. Rock suitability is an audition hypothesis. |
| Jazz/funk/general-purpose kit | [Virtuosity Drums, Versilian Studios and Karoryfer](https://versilian-studios.com/virtuosity-drums/), CC0 | Up to 36 dynamic layers on drums, alternate cymbal hits, several microphone perspectives; about 1.1 GB | Recorded room sound is baked in; choose a suitable mic mix so our game-room reflections do not exaggerate it. |
| Classical nylon and steel acoustic guitar | [Meta Open Sound](https://github.com/facebookincubator/Meta-open-sound), CC0 for these instrument collections | Nylon: 348 samples; steel: 375. Both have three dynamic layers, SFZ maps and production WAV archives | Verify per-collection license and articulation semantics; the separate Oculus SFX collection is CC BY 4.0, not CC0. |
| Compact classical fallback | [FreePats Spanish classical guitar](https://freepats.zenvoid.org/Guitar/acoustic-guitar.html), CC0 1.0 | Small SFZ/WAV option: 6.9 MiB archive, 19 MiB unpacked | Creator notes imperfect recording conditions and noise processing; compare against Meta nylon. |

Karoryfer's [current free-library policy](https://shop.karoryfer.com/pages/free-samples)
states its free sample libraries are now CC0, with a specific exception for
Marie Ork. The candidate repositories above also carry explicit CC0 licenses.
Older archives can contain older notices, so preserve current source evidence.
CC0 permits copying, modification and redistribution, including commercial use:
[Creative Commons deed](https://creativecommons.org/publicdomain/zero/1.0/).

## Proposed implementation order

1. Acquire and inspect the DI guitar, bass, stick-kit and nylon/steel sources.
   Record archive hashes/licenses and audition actual samples before promising
   artist-specific realism. Do not ship commercial sampler executables or GUIs.
2. Add a bounded native bank of note/velocity zones, root tuning, gated/one-shot
   behavior and alternate-hit selection. Support the required SFZ subset offline
   or convert into a validated Fetcher bank, rather than claiming full SFZ support.
3. Add articulation selection: sustained/short/muted guitar, bass technique and
   drum/hihat states. Include release behavior and cross-lane choke groups.
4. Keep a shared immutable sample cache, preload needed material outside the
   audio callback, and validate multi-player memory/CPU/loading costs. Give each
   source a stable content ID so players hear the same bank over multiplayer.
5. Feed sampled electric guitar/bass into our existing drive, cabinet and mixer.
   Curate rock/metal/funk starting patches only after level-matched auditions.

Current Studio sample playback assigns one sample reference to a track. It is
useful for single hits and voice lines, but does not yet supply this instrument
mapping/articulation system. Adding preset names or stretching one recording
across the keyboard does not implement the steps above.
