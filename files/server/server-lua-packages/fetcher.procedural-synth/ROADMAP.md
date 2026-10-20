# Procedural Synth / Studio roadmap

Updated 2026-10-19. Completed work and subsequent planned slices are identified below.

## Product goal

Create, import and edit MIDI arrangements in the in-game Studio, then perform
them through Bardcraft-style gameplay. Tracks belong to instruments played by
players or NPC band members, rather than being tied to the workstation window.
Multiple performers may use keyboards; the vanilla lute and drum are the first
additional instrument models for testing. Editing a song and performing it are
two views of the same saved arrangement, patches, sample references and routing.

Installed game audio is also an instrument source: voice lines, effects and
music, including archive-backed files and loose files from enabled mods. A
searchable sample browser must support audition, trim/start/end, root note,
pitch tracking, one-shot/gated/loop modes, envelopes and gain. A later slice adds
chopping, reverse and crossfades. Long music files need bounded excerpts and
off-thread decoding; decoding and disk I/O must never occur in the audio callback.
Samples follow the native score clock, mixer, spatial routing and local monitor.
Persist VFS paths and playback settings, not copies of game audio. Multiplayer
must check asset identity/availability and report missing samples. Reuse the
server `/s` speech-command catalog and Bardcraft playback code as references for discovery,
path handling and gameplay presentation; native sample voices provide timing.

Sampling reference: `apps/openmw-server/scripts/speech_commands.lua` handles
`/s` and `/speech`, indexed categories and explicit paths, with
`speech_data.lua`, `starwind_speech_data.lua` and `custom_speech_data.lua` supplying
the catalogs. Reuse those file/category identities where useful and normalize
their relative `Vo` paths into full VFS `sound/vo` paths. The sampler browser must
also discover installed effects and music beyond that command's MP3 voice-only
filter. Keep chat speech playback/cooldowns separate from sample-accurate MIDI
voices.

## Current baseline

The native engine has two-oscillator subtractive synthesis, dedicated kick,
snare and hat voices, precise sound controls, project undo, MIDI part assignment,
16-bar clips and multiplayer positional playback. Fifty-one built-in sounds provide
basic starting points. Personal patches and a working project save per character.

Drum Drive saturates the dedicated drum voices. The final output also has a soft
limiter. The guitar slice adds a physical string voice and independent instrument
drive, amp tone and speaker filtering. Sampling and a general mixer effects rack
remain planned. Studio sequencing now runs on a shared native sample clock;
multiplayer starts use server-clock estimates and queued-audio compensation.

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

## Completed slice: native Studio transport

- One native PCM stream and sample clock for up to 16 independent lanes.
- Precompiled sample-position onsets/gates, cross-loop overlaps, swing, sustain,
  imported MIDI tempo maps, hat choke and direct Stop cancellation.
- Versioned owner uploads with bounded chunks, note-density/gate validation,
  transient server score caching and current-phase late joining.
- RTT-filtered server clocks, future starts, queued-audio compensation and native
  positions for the UI. Step/note/swing edits replace scores at bar boundaries;
  changing loop length/loop mode stops playback before a restart.
- Exact within-stream scheduling tested with different render block sizes;
  native playback continues during a deliberate 1.5-second Lua stall.

This is a native timing foundation, not hardware-locked synchronization between
machines. Continuous device-drift correction and reconstructing held notes for
late listeners remain future work. Current output is the placed keyboard's mono
spatial emitter; each lane's instrument/effects identity is preserved for later
physical destination routing.

## Completed slice: two-octave audition and shared returns

- Expand the synth keyboard to C4-C6 (25 chromatic keys) in the same width.
- Searchable dropdowns with draggable scrollbar and wheel scrolling for MIDI
  files and sound presets in Synth and Studio.
- Shared native delay/reverb, per-lane post-fader sends, saved return controls,
  post-return master volume, network state/late join, undo/redo and Stop cleanup.

## Completed slice: portable keyboard

- Pick up from Synth or Studio into a real inventory item with a matching icon.
- Native authoritative pickup atomically removes the placed object and updates
  inventory; existing stage activators become portable miscellaneous items.
- Place from inventory, activate to reopen Studio, and load the character's
  saved arrangement. Dirty projects save before pickup; untouched windows do
  not overwrite an existing song. Personal patches stay per character.
- Two-client playback/cleanup, repeated pickup and server restart are tested.
  Band routing and actor-held props are covered by the completed slices below.

## Completed slice: placed-keyboard band routing

- Band page with eight saved named member slots and outputs for all sixteen
  tracks; member gain/mute remain independent of timbre and the track mix.
- Persistent placed-keyboard bindings with server reservations and explicit
  Ready/Unbound/Missing/Busy/Out of range states; no automatic fallback.
- One native spatial stream per physical output, disjoint lane scores, common
  server song timeline/start/replacement and per-stream queue compensation.
- Native silent-score clock, observer snapshots, missing-output cleanup,
  current-phase rejoin and Stop/Finish across every output.
- Per-output delay/reverb tails with shared settings. Stereo, group buses and
  continuous drift correction remain planned.

## Following slices

1. Song editing: convert imported parts to editable note data, with selection,
   drag/resize/copy, zoom/scroll, velocity/gate editing, quantized recording,
   arrangement clips and automation. Preserve tempo maps and export edited MIDI
   for the performance path. Existing click-to-create clips remain supported.
2. Project library and autosave/recovery, followed by native sample instruments
   and the installed-game-audio browser described above. Test spoken lines,
   effects and trimmed music with single notes, chords, drums and band playback.
   Distinguish MIDI export from rendered audio export.
3. Broader multiplayer synchronization: continuous drift correction, measured
   device latency, richer late-join state and further rate-budget work.


## Completed slice: native channel mixer

- Separate project mixer snapshots per lane; sound changes preserve the mix.
- High-pass, bass/treble shelves and variable-frequency mid bell; EQ bypass.
- Peak compressor with soft knee, threshold/ratio/attack/release/makeup and bypass.
- Post-strip/post-fader track peak/RMS and gain reduction; pre-limiter master
  peak, output RMS and overload indication before local/spatial volume.
- Smoothed audio-thread DSP on the existing shared clock; faders now follow
  patch effects and channel dynamics rather than changing instrument input gain.
- Project migration, SQLite round trips, undo/redo, bounded coalesced network
  settings, late-listener snapshots and two-client Stop verification.

This slice does not add sends/buses or change the mono keyboard emitter.


## Completed slice: program-aware defaults and whole-song sound sets

- Use initial MIDI channel programs, including setup tracks, to map instrument
  families; use names only when the file omits program metadata.
- Add six melodic/tom starter patches and compensate MIDI bass octave tuning.
- Seven complete sound sets on Studio Mixer, with immediate previous/next and
  the shared searchable dropdown. Apply to all existing parts/clips as one Undo.
- Preserve score data, routing and the mix; update native parameters during play.
- Save set selection and timbre snapshots; old projects/same-file imports retain
  existing assignments until explicitly remapped. Ordinary per-track edits are
  still available and mark the set Custom.
- This does not implement samples, all GM percussion, mid-part program switching,
  or musical role inference from unlabeled notes.


## Completed slice: player/NPC band performers

- Native character identities, session-only invite/accept/decline/leave consent,
  F9 performer panel and authoritative inventory readiness for keyboard/lute/drum.
- Character-owned persistent Studio NPCs with create/dismiss and saved roster;
  ordinary world NPCs and other characters' performers remain outside ownership.
- Exact actor spatial emitters on the existing native song clock; holster/death/
  leave/missing/out-of-range silences only those parts, with current-phase rejoin.
- Expiring native visual props, reusable arm IK and first-person self-prop hiding.
  Invitations do not open UI or force cameras. No NPC follower/stage AI or finger
  animation yet. NPC instrument props are presentation, not inventory equipment.
- Two-client integration and restart persistence checks in the original isolated
  port-25567 fixture; latest user project/settings backed up and restored.
