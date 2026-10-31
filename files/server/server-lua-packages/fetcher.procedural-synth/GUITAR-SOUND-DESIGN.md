# Guitar and heavy sound-design direction

Reviewed October 30, 2026 using the three supplied videos' English captions,
chapter descriptions and published descriptions. Completed slices are recorded
below alongside the remaining plan. Listening acceptance remains separate.

Package 54 follow-up: the first native cabinet/mix-context slice is implemented
and covered by DSP, UI and routed-output tests. It supplies three CC0 captures,
pre-drive/post-cab cuts, existing post-cab channel EQ and local focus/dim. The
string/articulation and experimental branches below remain planned. Full-band
listening acceptance remains distinct from the automated measurements.

Package 55 begins the playable-string branch: per-strike articulation, pick
attack/variation/direction/pitch settling and live harmonic-resonance amount
are implemented for generated sources and gated banks. The Strings page and
Auto parameter selector expose them. String-aware strums, connected legato and
slides, external MIDI articulation mappings and new captured gestures remain
planned. The experimental parallel-processing branch has not been implemented.

## What the references contribute

- [CyberBlitz: How to DOOM Like Mick Gordon](https://www.youtube.com/watch?v=-9rTxQr1194):
  compression at 0:27, waveshaping at 4:20, simple oscillator foundations at 6:28,
  creative effects at 6:51 and parallel routing at 10:18. The workflow deliberately
  exaggerates quieter layers and effects through subsequent processing. Apply
  these ideas to an opt-in experimental instrument/FX path; they do not define
  the default dynamics of an acoustic kit or conventional thrash band.
- [SpectreSoundStudios: Why Your Guitars sound like SH*T in a Mix!!](https://www.youtube.com/watch?v=CDPav8DPBSQ):
  the 2:01 section starts with drums/bass present while dialing guitar tone.
  Subsequent sections demonstrate speaker/microphone position and amp settings;
  the conclusion returns to evaluating guitar as part of the arrangement.
  Adopt full-band auditioning and a local solo-in-front/dim option.
- [oddprophet: SERUM 2: HOW TO MAKE REALISTIC METAL GUITARS](https://www.youtube.com/watch?v=TnINvIgD26g):
  the 2:42 tutorial combines an evolving synthetic source, pick-shaped amplitude,
  dynamic filtering, distortion/EQ and comb filtering. Cabinet IR convolution
  follows at 8:00, with further processing options later. This supports pursuing
  generated-string sound design alongside recorded DI. The tutorial's demo is
  not evidence that our present oscillator or sample presets achieve that result.

## Current engine gaps

The native engine already has plucked-string synthesis, recorded DI banks,
modeled damping, oversampled drive, amp-style EQ and channel compression. The
cabinet stage in `proceduralsyntheffects.hpp` is four low-pass poles with three
bandwidth choices. It does not reproduce a captured cabinet/microphone response.
Its fixed chain and the current note controls also limit evolving excitation,
effect ordering, parallel processing and explicit guitar articulations.

Package 53's density fix, drum gain staging and different guitar voicings are
useful foundations. RMS/peak checks establish balance and headroom; they do not
establish convincing tone. New samples alone would still feed the same limited
cabinet path. Conversely, a stronger generated source and processing chain can
make useful guitar-like patches without requiring a recording for every gesture.

## Delivery order

1. **Cabinet/microphone and mix context.** Add a native convolution stage shared
   by all voices of each guitar lane, with bounded IR size, cached loading and
   predictable CPU/tail behavior. Support generated responses or explicitly
   redistributable captured IRs with source credits. Keep pre-drive filtering,
   nonlinear drive, cabinet response and post-cabinet EQ/output independently
   controllable. Retain the old cabinets for existing patches; keep the cabinet
   response separate from room reflection/acoustics processing. Add local
   solo-in-front monitoring that leaves saved/routed mixer levels untouched.
2. **Playable string behavior.** Improve velocity-sensitive pick transients,
   evolving string resonance, attack pitch movement and time-varying damping.
   Keep generated-string and recorded DI sources available for the same chain.
   Add explicit open/muted/choked, rhythm/lead, bend/slide and alternate-pick
   controls, with optional authored MIDI/controller mapping. Imported notes do
   not reliably specify these gestures; any automatic interpretation must be
   optional and reversible. Evaluate new recorded articulations where they help.
3. **Experimental processing.** Add opt-in parallel clean/dirty/noise layers,
   configurable waveshaping, modulation/comb effects and multiband or upward
   compression as separate bounded native DSP work. Preserve useful defaults
   for conventional rock/metal; extreme processing belongs to selected patches.

## Acceptance

Use the same representative riffs and complete drum/bass/guitar arrangements
for before/after comparisons. Compare at matched audition loudness; include
isolated stems as diagnostics and full-band renders as the primary listening
test. Retain transient definition and usable bass/kick space. Check repeated
chugs, sustained power chords, clean passages, lead phrasing and low notes.
Offer source-only, amp-only and cabinet-enabled comparisons so improvements can
be attributed to the relevant stage rather than extra output level.

Check mono compatibility for optional layered/widened patches, clipping, CPU,
block-independent scheduling and multiple simultaneous bands. Preserve source
identity, effect settings and articulation data through save/load, Undo, native
score playback, observers and late joiners. Remain entirely native to Fetcher;
the commercial synths/plugins used in the references are not runtime dependencies.
