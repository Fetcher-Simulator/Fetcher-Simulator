# FETCHER STUDIO roadmap

Updated 2026-10-29. Completed work and subsequent planned slices are identified below.

Package 43 adopts the latest seated keyboard/cello, violin, timpani, trumpet and
flute calibration exports, including seated legs/feet and stool scale. Standing
keyboard/cello, lute, hand drum and clarinet retain their earlier defaults until
new exports are supplied.

## Completed refinement: seated pose and bench tuning (package 42)

The local pose tuner now exposes pelvis position, independent foot targets and
foot rotation, knee directions and thigh/calf/foot/toe bone offsets. Seated
keyboard and cello profiles also have independent stool position, rotation and
uniform scale. Save, reload, reset, preview and export include these values;
old hand-only calibration keeps the existing defaults. Seat/feet camera presets
help inspect the lower body. These are local visual adjustments, with no actor
teleport, physics or song/routing changes. Fine artistic calibration remains
available through player exports before any wider rollout.

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

## Completed slice: installed-game sound browser and voice-line sample playback

Completed 2026-10-20 in package 18 / native synth API 12. Browser and playable
voice-line instruments share the native Studio clock and physical band outputs.
MIDI export and track renaming shipped in package 19; library/recovery in 21.

### Shipped first pass

- Add a searchable, scrollable Studio sample browser for installed voice lines,
  sound effects and music, resolving both archive-backed and loose VFS assets.
  Browse by category/path, audition a selection and show loading/unavailable
  states. Long music files use bounded excerpts rather than loading entire songs.
- Reuse speech_commands.lua and the speech_data.lua, starwind_speech_data.lua
  and custom_speech_data.lua catalogs for voice identities/categories. Normalize
  relative Vo paths into sound/vo VFS paths and show only locally resolvable
  entries as available. The reference is the server /s and /speech system,
  not Voice of the Mudcrab. Discover installed effects/music beyond that catalog.
- Add native sample voices: root-note pitch tracking, velocity/gain, trim start
  and end, one-shot and gated playback with a basic envelope/release. Prepare
  and decode bounded sample data outside the audio callback, using bounded cache
  and voice limits. The existing synthesizer remains available on other parts.
- Assign the selected sample to a Studio part and play it with the keyboard,
  recorded/drawn notes, imported MIDI and linked clip arrangements. In particular,
  a voice line must function as a pitched MIDI instrument, not just chat speech.
- Use the existing native score clock, track mixer/effects, local monitor volume
  and player/NPC/placed-keyboard band outputs. Native sound changes require the
  appropriate client API/package update and an isolated fixture rebuild/check.
- Save VFS references, asset identity and sample settings in projects and sample
  sound presets. Resolve/reload them on load; detect unavailable/mismatched
  assets per listener and report the affected part without substituting a
  different sound or interrupting unrelated parts. Do not redistribute game
  audio or route musical notes through chat-speech cooldowns.

Acceptance: select a real /s-catalog voice line in Studio, audition and trim it,
play a pitched phrase/chord from MIDI and a reusable clip, route it to a player
or owned NPC, then Save/Load and hear matching playback on the second client.
Also exercise installed effects and a bounded music excerpt, archive/loose asset
resolution, missing-asset handling and repeated sample switching/Stop/Finish.
Run in the established port-25567 fixture, preserving the latest user saves and
settings. Existing synth projects, live audio timing and band routing must remain
compatible. See TEST-REPORT.md for checks and first-pass limits.

Later sample work: sustained sample loops, chopping, reverse, crossfades and
more elaborate sampled instruments. These are not prerequisites for the first
browser + voice-line playback slice.

## Completed slice: MIDI export, track names and responsive editing

Completed 2026-10-20 in package 19 / native synth API 13. Studio exports complete
songs or one legacy pattern cycle as format-1 MIDI, flattening linked arrangements
and retaining notes, gates, velocities, tempo maps, percussion and track names.
Authored meter is written for other DAWs; imported meter support shipped in package 23.
Exports stay in the owner's local character profile and immediately join the MIDI
picker. Sound/sample/effect/band settings remain in server-saved Studio projects.
Track renaming has explicit Apply/Cancel, Undo/Redo and project persistence.
Fullscreen/resizing expands the Piano workspace, and Drums can duplicate the
current bar across all lanes into the next bar, extending up to 16 bars with Undo.
Offline regressions and native two-client export/reimport/playback checks passed.
See README.md for usage/limits and TEST-REPORT.md for evidence.

## Completed follow-up: focused Piano editor and fullscreen navigation

Package 20 keeps tabs clickable above fullscreen centered page containers. Piano
has a dedicated expanded editor with Escape/Back restoring the prior Studio
geometry, plus single-note right-click deletion with Undo. Studio temporarily
owns the inventory/UI toggle so right-click edits do not close the interface.
The project library and autosave/recovery shipped in package 21.

## Completed slice: project library and autosave/recovery

Completed 2026-10-20 in package 21; native synth API remains 13. Projects opens a
private per-character library with up to 16 named saves, Save As, rename, explicit
delete confirmation and safe migration of the previous working save. Separate
server recovery snapshots update every 30 seconds while dirty, retain three recent
drafts, and restore as unsaved copies. Unsaved document switches/Finish/pickup offer
Save, Keep recovery or Cancel and wait for acknowledgements. Late replies, write
failures and stale revisions preserve newer work. Two-client and restart checks
passed; see TEST-REPORT.md for evidence and README.md for limits.

## Completed slice: automation and MIDI-controller editing

Completed 2026-10-20 in package 22 / native synth API 14. Auto provides saved
per-track parameter/controller lanes, step/linear editing, mouse drag/delete,
Undo/Redo, and native-playhead slider capture as one take. Supported MIDI controls
import explicitly; program following is opt-in and uses existing sound-set
families. Native score playback, loop/crop/seek/replacement chase, band routing,
late observers, SQLite persistence and restart checks passed at the full 8,192
point budget. MIDI export retains supported CC/bend/program lanes. See README.md
for capture timing, existing baked-gate compatibility, bounds and follow-ups.

## Completed slice: tempo/meter editing and musical bar maps

Completed 2026-10-21 in package 23; native synth API remains 14. Song -> Timing map
adds/removes tempo and meter events with bar-position selection, searchable event
lists and full-map Undo/Redo. Song regions, Piano/Arrange rulers/snapping, stopped
recording count-in and native live-edit replacement follow changing bars/tempos.
MIDI imports/exports time-signature and tempo conductor maps; named saves, recovery
and restart playback retain them. Notes, linked clips and automation keep their
quarter-note positions. Timing changes stop the old clock before restart. Native
two-client playback, late listener, UI painting/dragging and restart checks passed
in the original port-25567 fixture. All saved user settings/data were restored.
See README.md for limits and TEST-REPORT.md for evidence.

## Completed slice: arrangement groups, variations, transfers and offsets

Completed 2026-10-21 in package 24; native synth API remains 14. Shift-click and
Select all work across track pages; group move/duplicate/delete/transpose/mute
validate atomically and commit as one Undo. Make unique detaches each selected
source, and Copy source to creates an independent snapshot in another edited part
while preserving destination sound/mix/routing. Per-placement source phase offsets
chase/crop/repeat through the same native/MIDI expansion. Legacy offsets migrate
to zero. Native two-client replacement, save/load/restart and fullscreen UI checks
passed in port 25567 with user saves/settings restored. See README.md for controls,
source/placement budgets and horizontal group/source-copy behavior.

## Completed slice: recording tools (package 25 / API 15)

Native local metronome, configurable 0/1/2/4-bar count-in, input compensation and
robust tap calibration are implemented. Windows MIDI note/velocity/sustain input
uses a bounded callback queue and explicit device/channel controls. Two-client
count-in, take capture, click-free MIDI export, native playback and clean-exit
preference/project persistence checks passed in the port-25567 fixture. Physical
MIDI hardware acceptance and listening/tapping remain unverified here. Tap
calibration estimates timing bias, not automatic hardware round-trip latency.

## Maintenance slice: sequencer workflow, samples and concurrent playback

Package 31 / native synth API 16 / protocol 27 adds whole-pattern capture into
linked arrangements, piano-roll notes in Play clips, responsive Arrange/zoom,
track/lane duplication and drum bar/lane deletion. Samples reuse /voicelines
subtitle lookup and set new drum sample triggers to the root. Count-in clicks
are independently enabled by default. Studio documents/native transport support
32 parts with four UI pages; the client resource allowance becomes 128 timbre/
stream units. MIDI channel numbers retain their standard 1-16 range.

The audio worker now accounts for rendering time in its refill schedule, and
settled patches avoid redundant per-sample calculations. Controlled WASAPI tests
compare the previous and candidate builds with two dense independent scores.
See TEST-REPORT.md for measurements and the limits of that test. This maintenance
slice precedes the remaining feature roadmap; it does not itself publish builds.

## Completed slices: standing stage and first orchestral instruments

Package 33 adds Auto NPC band (one performer per part, up to 32), roster reuse,
explicit Restage grid, stationary performers facing the captured leader, and
one-step routing Undo. Keyboard performers use the floor-anchored Yamaha/X-stand;
the vanilla lute now has its measured neck/string orientation and fretting/strum
targets. Drum, wrist and finger motion have separate profiles.

The next instrument slice adds original violin, cello, trumpet, flute, clarinet
and timpani props/icons, moving bows/mallets, nine selectable physical instruments,
GM-aware initial model selection, six editable synthetic orchestral patches and
an Orchestra sound set. Model choice persists independently of the sound. Native
attachments move existing meshes instead of recreating them each pose frame.
Two-client checks cover 32 stationary performers, late observers, reused identities
and stopped holding poses. See TEST-REPORT.md for visual checks and limits.

Package 34 refines the automatic setup: compatible melodic parts share a player,
all ordinary drum lanes share a drummer, and Leader plays reserves the owner's
workstation group. Track content/mixing remains independent. Managed Play updates
the lineup for a changed song, trims surplus hires (including old unbound NPCs),
and waits before starting audio. Stop retains the stage; Finish/session release
removes managed NPCs. Remove owned NPCs provides explicit cleanup. The leader is
aligned with the standing keyboard and uses note-driven wrist/finger motion.
Fiddle and clarinet grip refinement and Studio-over-hotbar window ordering are
included in this follow-up.

Package 35 adds five-view native inspection of all nine profiles, palm-derived
finger flexion axes, grip/strike refinements, a reachable standing cello bow
position and repaired solid violin/cello necks. Serialized NIF validation checks
that the fingerboard touches the neck. Custom rigs and seated cello work remain.
Package 36 corrects the trumpet's horizontal tubing plane and transverse valve
row, with grips and valve presses adjusted to the upright longitudinal layout.
Package 37 adds a local live pose tuner: instrument/attachment transforms,
hand/palm/elbow/finger controls, advanced arm/finger bone offsets, performer
camera views, validated local persistence and data export. Band playback keeps
running while the tuning panel is open. Defaults can now be refined interactively.
Package 38 incorporates the approved seven-instrument calibration exports and
keeps a stable reference frame for saved tuning, preventing duplicate rotations.

## Completed slice: band listening balance and room acoustics

Package 39 / native synth API 17 implements the selected approaches: gentler
instrument-only distance falloff and a local Balanced monitor for one selected
band. Band > Listening and F9 > Listening expose the controls. Existing audio
sources change perspective in place, preserving the score clock, voice budget,
authored faders and other clients' listening choices.

Bounded collision probes estimate the listener's room dimensions, enclosure and
shape. A shared instrument-only OpenAL EFX return supplies early reflections and
late reverb, with smooth geometry changes and adjustable wet level/absorption.
World-mode direct paths respond to static obstruction; Balanced bypasses direct
obstruction. Dry and no-EFX fallback are supported. This first pass approximates
geometry/materials; it does not simulate connected rooms or identify materials.
Two-client native checks cover player/NPC outputs, uninterrupted mode switches,
unchanged unit counts, geometry/EFX response and independent observer routing.

## Completed slice: credited models and initial seated stage

Package 40 / native synth API 17 replaces the generated orchestral props with
credited CC BY 4.0 Sketchfab models, retaining the existing Yamaha/stand and
vanilla lute/drum. Provenance, source hashes and adaptation notices ship in the
asset bundle. New instruments should follow the same documented CC-source policy.

Band's Layout selector offers Standing and Seated. Owned keyboard/cello NPCs
receive separate seated profiles, planted-foot leg IK, chairs and music stands;
other performers stay standing. The seated Yamaha preserves key/body thickness.
Bounded local floor/slope/footprint/headroom/wall checks precede setup, with
server-selected grid positions and validated floor hints. Restaging changes
posture explicitly; helper props expire and are cleared with performers.
Native two-client checks cover models/grips, both seated rigs, late observers,
standing/seated restage and complete managed-NPC cleanup. Custom proportions
and final artistic grip refinement remain ongoing work in the live pose tuner.

Package 41 replaces the oversized chair with a compact piano stool, joins a
credited sheet booklet into the Yamaha rests and separate stand, and supplies
stands to non-keyboard NPCs. Native spawned-actor teleport support and positional
readback repair lineup changes that previously left reused NPCs in old slots.

## Remaining roadmap (next: guitars, electric bass and a full drum kit)

1. **Guitars, electric bass, full drum kit and convincing genre playback.**
   Prioritize electric guitar and bass, nylon-string classical guitar,
   steel-string acoustic guitar, and a complete acoustic drum set before further
   orchestral expansion or conductor/layout features. The goal is convincing
   musical playback, with Black Sabbath, Metallica and System of a Down as
   representative listening targets alongside funk, clean/acoustic music and
   other genres. Use representative riffs, arrangements and matched-level
   listening comparisons to guide the sound work.

   - **Sound foundation:** evaluate the current plucked-string/drum voices and
     drive/cabinet processing against representative phrases first. Extend the
     synthesis or add appropriately licensed multisamples where the existing
     voices cannot deliver convincing attacks, sustain and timbre. Acceptance
     requires audible improvement in timbre, dynamics and articulation. Record
     sources, redistribution licenses and credits for new sound libraries.
   - **Guitar and bass articulation:** clean, crunch, fuzz, high-gain rhythm and
     lead guitar; tight palm-muted/choked notes, sustained chords, picking,
     bends/slides and usable low-register/drop-tuned phrases. Electric bass
     needs distinct fingerstyle, picked and slap/pop starting sounds. Classical
     nylon and acoustic steel strings need distinct attacks/resonance, picked
     and fingerpicked notes, strummed chords, damping and natural releases.
     Expose articulation controls and explicit MIDI/controller mapping when
     needed; ordinary MIDI notes do not reliably encode picking, muting or slap.
   - **Complete drum set:** kick, snare/rim, rack/floor toms, closed/open/pedal
     hi-hat, ride and crash cymbals, with double-kick patterns for metal. Use
     velocity response and variation to avoid identical repeated hits, correct
     open/closed-hat choke behavior and appropriate cymbal tails. Map imported
     GM percussion pitches to the correct kit pieces. Keep one physical drummer
     for compatible kit lanes/parts, retaining independent sound/mixer controls.
   - **Curated presets and whole-band sound sets:** matched-level classic heavy
     rock/doom, thrash metal, alternative metal, modern high-gain, clean electric,
     funk, blues/jazz, classical and acoustic starting points. Funk includes
     muted rhythm guitar, finger/slap bass and a responsive drum kit. Preserve
     existing user patches and allow independent rhythm/lead/bass/kit choices;
     whole-song auditioning should not require replacing every track manually.
   - **Physical performance:** obtain credited CC0/CC BY guitar, bass and full-kit
     models, inventory icons and suitable grips/strumming/plucking/stick/pedal
     motion. Extend the live poser as needed for guitar/bass and seated drummer
     calibration. Stage the complete kit as one performer footprint and keep
     its props, sound selection and MIDI part routing independently editable.

   Delivery order: establish guitar/bass/classical/acoustic and kit identities,
   routing and audition phrases; improve the sound/articulation foundation;
   curate and balance the genre banks; then complete models/animation and native
   band acceptance. Test riffs, chords, bass lines, funk grooves, fingerpicking,
   acoustic strums and complete imported rock/metal songs. Verify MIDI program/
   percussion mapping, save/load, Undo, late listeners, concurrent bands and
   sustained performance at matched listening levels before calling it complete.
2. **Section layouts and conductor presentation.** Add saved spacing/layout
   presets, instrument sections, conductor gestures and formation previews.
   Extend seating to players and additional instruments, with chair interaction
   and custom-rig/body-height support. Keep explicit safe restaging and the
   standing grid; avoid moving a formation when its leader walks away. Improve
   client-assisted footprint checks for cramped/uneven stages and furniture.
3. **Orchestral instrument expansion and sound refinement.** Add remaining families
   (including double bass, oboe, bassoon, horn and trombone), articulation, rig/body
   variation support and better acoustic presets/multisamples. Refine grips and
   seated performance across rigs. Balance a Star Wars-theme-scale score;
   the six initial synthetic patches are starting points, not a complete orchestra.
4. **Large orchestra performance and multiplayer timing.** Profile 32 sounding
   physical performers, late joins, separate concurrent bands and long sessions.
   Improve output/voice culling, spatial section grouping and device-drift/latency
   correction before increasing score/performer limits further. Imports over the
   current 32-part budget fail clearly; do not silently drop orchestral parts.
5. **Acoustic and monitoring refinement.** Add authored room zones and acoustic
   presets, connected-room propagation, material-aware absorption, diffraction
   and more accurate direct/reflected balance. Refine the bounded geometric
   estimate in irregular rooms and outdoor transitions. Retain one shared room
   return, user controls and bounded geometry work outside the audio callback.
   Explore stereo monitoring and optional world/monitor blending after profiling.
6. **Recording follow-ups.** Physical MIDI controller acceptance, live controller
   monitoring/capture, shared-source overdub during playback and additional record
   modes. Automatic hardware round-trip measurement and non-Windows input remain
   planned; tap-based timing compensation is available now.
7. **Mixer/effects routing.** Stereo/panning, group buses, flexible effect chains
   and mixer/return automation. Native EQ/compression, instrument drive/cabinet
   and shared delay/reverb already exist.
8. **Further sample tools and sound libraries.** Sustained loops, chopping,
   reverse, crossfades and sound-library expansion beyond the priority band set. Continue
   listening/balance refinement alongside the orchestral instrument work.

This section is the current priority order. Completed-slice notes below describe
what was available at each milestone; their older follow-up lists do not override
this order. Long-session performance and compatibility checks apply throughout.


## Current baseline

Studio can create songs from scratch, import/edit MIDI, record quantized takes,
and arrange linked melodic/drum clips. The native engine provides subtractive,
drum and plucked-string synthesis, drive/cabinet processing, channel EQ and
compression, shared delay/reverb and fifty-seven built-in sound starting points.
Personal patches, 16 named projects and three recovery snapshots save per character.

Up to 32 tracks route to placed keyboards or player/owned-NPC band performers,
with native audio-clock sequencing, server-timed starts, spatial outputs and
held-instrument presentation. The portable keyboard, inventory icon, scalable
resizable windows and saved band routing are implemented. Runtime baseline is
package 43 / native synth API 17 / protocol 27 (matching builds required). Installed-game sampling, library/recovery,
automation/controller, tempo/meter, arrangement and first-pass recording tools are
implemented, together with initial seated keyboard/cello NPC staging.

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


## Completed slice: imported MIDI note editing

- Explicit undoable part conversion preserves source PPQ/effective sustain gates,
  sounds, channel mix and saved player/NPC/keyboard output assignments.
- Note selection, group move/resize, copy/paste/duplicate/delete, velocity, snap,
  half-to-four-bar time zoom, pitch/time navigation and draggable time scrollbar.
- Drag previews commit once on release; native live edits use bar replacement.
- Compact bounded saved note overrides and tempo map; empty overrides stay silent,
  full converted arrangements reload/play without their original MIDI file.
- Whole-song MIDI parts extend beyond the independent sixteen-bar clip loop.
  MIDI sound roles survive conversion/source-free reload and sound-set cycling.
- Native two-client band playback, private SQLite persistence and an 8,192-note
  network round trip tested in the existing isolated port-25567 fixture.


## Completed slice: quantized keyboard takes and selected-song playback

- Dedicated recording keyboard page with mouse/musical typing, four-beat visual
  count-in, frozen-grid onset/gate quantization, velocity and chord capture.
- Overdub on the native audible clock; tempo-map-aware song takes and bounded
  clip-loop capture; Keep/Cancel, held-note cleanup and one undo step per take.
- Recorded notes save using existing project data and band routing. Input state
  stays transient. Temporary commit failures have explicit retry/cancel recovery.
- Selected-song Play replaces the prior loaded song on every Studio page;
  immutable asynchronous load identity prevents stale-selection/session races.
- Native live/sequence pitch gating prevents premature same-pitch releases.
- External MIDI devices, audible metronome and recording latency calibration
  remain follow-up work, alongside arrangement/automation/export.


## Completed slice: source-free New Song and song regions

- New Song settings and Song page: empty authored arrangement with starter piano,
  melodic/drum parts, constant tempo, meter, 1-256 bars and explicit loop markers.
- Reuse full-song PPQ editor and recording; existing patch/mixer/band routing and
  private per-character persistence. No source MIDI file or conversion required.
- Preserve later notes when shortening; native playback bounds/regions exclude
  those notes until extended. Rebase region playhead and clip crossing gates.
- Meter-aware bar captions/grid, count-in and bounded replacement quantum;
  loop-boundary overdub cleanup, including regions smaller than the snap grid.
- Cancel pending MIDI selection/parsers on document replacement/undo/load.
  Explicit file selection still works, and old clip/imported projects migrate.
- Native two-client loop/record/save/load and consenting-player playback tests;
  extreme supported meters checked against bounded server score uploads.
- Arrangement clips, automation, imported tempo/controller editing, MIDI export,
  library/recovery, sample instruments and hardware MIDI input remain follow-ups.


## Completed slice: linked clips and arrangement timeline

- Capture selected piano-roll notes into one reusable source and first placement;
  capture legacy piano/drum lanes while retaining sound, mix and band output.
- Track-local source library, shared note editing/recording, source audition,
  individual placement move/repeat/trim/duplicate, transposition and mute.
- Eight visible track rows, four/eight/sixteen-bar zoom, bar-snapped mouse capture,
  draggable time scrollbar and wheel navigation. One undo step per drag release.
- Compact optional saved sources/references, PPQ/tempo-map-aware expansion into
  the existing native clock and band partitioning. Old projects stay compatible.
- Bound stored notes, source/placement counts and expanded playback work; reject
  invalid references, pitch overflow and destructive source-length truncation.
- Native A/B source edits, stopped recording, spatial routing, SQLite/wire round
  trip at the full 8192-note document budget, replay and Finish cleanup verified.
- Automation/controller editing, MIDI export, multi-selection/independent source
  copies, project library/recovery and game-audio samples remain follow-up slices.
