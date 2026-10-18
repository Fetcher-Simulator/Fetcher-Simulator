# Procedural Synth / Studio roadmap

Updated 2026-10-18. Completed work and subsequent planned slices are identified below.

## Current baseline

The native engine has two-oscillator subtractive synthesis, dedicated kick,
snare and hat voices, precise sound controls, project undo, MIDI part assignment,
16-bar clips and multiplayer positional playback. Forty-five built-in sounds provide
basic starting points. Personal patches and a working project save per character.

Drum Drive saturates the dedicated drum voices. The final output also has a soft
limiter. The guitar slice adds a physical string voice and independent instrument
drive, amp tone and speaker filtering. Sampling and a general mixer effects rack
remain planned. Sequencer
dispatch still runs from Lua game frames rather than a native audio clock.

## Completed slice: native instrument effects and guitar sounds

The following scope is implemented. Native/Lua tests and a disposable two-client
fixture cover the sound, history and persistence checks below. A late join hit
the existing frame-stall transport stop; restarting clips completed its observer
check. Listening review can guide further preset refinement; the cabinet models
are deliberately lightweight.

### A. Sound engine and controls

- Add a reusable per-instrument native effects stage after voice summing and
  before channel level, protective output limiting and world spatialization.
  Each Studio instrument gets independent settings; manual playing uses the
  same path. A full arbitrary effects-routing graph is a later milestone.
- Provide overdrive, distortion and fuzz characters, with input drive, tone,
  wet/dry and output gain. Add oversampling around nonlinear processing and
  parameter smoothing; measure aliasing and CPU cost before choosing defaults.
- Add amp-style tone shaping and a lightweight cabinet/speaker filter. Keep
  musical drive separate from channel volume and the final protective limiter.
  This first cabinet stage is a tonal model, not imported impulse-response
  convolution or a claim to emulate a particular amplifier.
- Add a plucked-string voice with velocity-sensitive excitation, damping and
  palm-mute control. Begin with clean plucks, rhythm chords and distorted leads;
  advanced bends, slides, strum input and sampled articulations can follow.
- Expose controls in the in-game editor. Save the instrument and effects together
  in a patch, include edits in Studio undo, and replicate the complete sound to
  observers and late joiners. Local monitor volume remains a listener preference.
- Default new stages to bypass for old patches/projects. Preserve existing
  parameter identifiers and built-in IDs; update native API compatibility,
  mirrored schemas and bounded network validation together.

Acceptance: compare clean/crunch/high-gain/fuzz on the same phrase and at matched
output levels; exercise chords, repeated notes, silence and extreme controls;
check finite output, transitions, aliasing and maximum-load CPU behavior. Verify
old-save compatibility, save/load, undo and matching two-client playback,
including Stop, disconnect and late join. Render auditions for listening review.

### B. Curated starting banks

Organize presets by both sound role and genre tags. A sound can belong to several
genres. Keep instrument patches, complete drum kits and optional project
templates distinct, so choosing a sound does not replace an arrangement.

Initial coverage:

| Bank | Starting sounds |
| --- | --- |
| Rock, punk and metal | Clean guitar, crunch rhythm, muted high-gain rhythm, lead, driven bass, punchy kit |
| Blues, funk and jazz | Warm clean/edge-of-breakup guitar, muted pluck and round bass |
| House, techno and trance | Tight kick, snare, hats, sub, acid bass, pluck and lead |
| Hip-hop, trap and lo-fi | Long 808-style bass, short kick, snare, hats and mellow keys |
| Drum and bass and dubstep | Short punchy drums, reese bass and wobble bass |
| Pop, synthwave and disco | Synth brass, bass and pad |
| Ambient and cinematic | Slow pads, drones, bells and evolving textures |
| Chiptune and retro games | Pulse lead, triangle bass and noise percussion |

These are starting banks, not an exhaustive genre list. Acoustic piano,
orchestral instruments and realistic acoustic kits need additional synthesis or
sampling; do not label oscillator approximations as realistic replacements.

Category/genre browsing and Load + Audition are implemented. Built-ins stay read-only; editing
and saving creates a personal copy. Preserve saved sound snapshots across bank
updates. The existing 32-personal-preset limit remains unchanged.

Author presets only after their required DSP exists. Match useful playing levels,
check several registers and velocities, and audition each in a short arrangement
with other sounds rather than evaluating solo tone alone.

## Following slices

1. Native sample-clock transport and scheduled note queues: tempo, swing, looping,
   lookahead and reliable multiplayer start synchronization. Preserve per-track
   instrument/effects separation and plan held-instrument destinations here.
2. Mixer expansion: EQ, compression, delay/reverb sends, buses, useful meters and
   explicit routing between tracks, instruments and physical world emitters.
3. Song editing: editable imported MIDI, note selection/drag/resize/copy,
   recording, arrangement clips and automation using the native clock.
4. Project library, autosave/recovery, sample instruments, audio export and
   broader multiplayer synchronization/rate-budget work.

Timing remains foundational: more presets and effects do not remove frame-based
sequencer jitter. Do not expand arrangement/recording before addressing it.
