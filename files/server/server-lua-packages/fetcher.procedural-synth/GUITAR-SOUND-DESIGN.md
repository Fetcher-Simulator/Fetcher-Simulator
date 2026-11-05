# Guitar and heavy sound-design direction

## November 5 acceptance (package 80 / API 37)

The user accepted the second passages in the matched-volume full-band
Master of Puppets MIDI and original industrial comparisons as usable starting
points and authorized closing priority one. These compare the existing captured
set with new Performance recipes. They are not acceptance as exact Metallica or
DOOM recreations. Old presets remain available; personal sounds are untouched.

The final recipes combine source-correct attacks, the reviewed complete captured
rig, tighter/longer gate releases, deliberate pre/post cuts, saved channel EQ and
band balance. New Captured tightness varies the input bass cut from 50 to 181 Hz:
broader for doom, tighter for modern rhythm. Industrial retains a bounded parallel
layer and low-B allocation. Separate rhythm/lead/bass/kit choices remain editable.

Real Green Guitar upward hammers and Darkblack finger-bass endings address the
remaining recorded-gesture foundation. Downward connections/slides remain
modeled. The recorded release level and legato touch default to zero for old
sounds. A new original four-bar sound check isolates picked, connected and
picked-slide phrases over steady backing. All new recordings are credited CC0;
harmonics and paired DI sources retain their separate CC BY license.

SSE2 reduces the same four-chain amp/boost benchmark from roughly 0.46 to 0.16
seconds per second PCM on this PC. Scalar and SIMD retain the golden-reference
maximum error below 0.000001. This is not total application CPU or an unlimited
orchestra claim. Wider rigs, album-specific production and instrument refinement
continue as follow-up work; sample editing is now next in the roadmap.

## Native captured rig checkpoint (package 76 / API 35)

Three matched-guitar-level passages isolated old C, corrected physical-string
source selection through C, and corrected sources through the complete RiffSmith
Distorted capture. The user rated passage three much better. Preserve this as an
improved starting point; do not claim the artist target is finished. The earlier
GuitarML IIC+ prototype was only some improvement and remained buzzy.

Source audit: P1 writes all six string tracks on channel zero. The v1 builder
therefore selected the earliest matching pitch despite its highest-string comment.
V2 reads exact E/A/D/G/B/e track names and global tempo, excludes velocity-10 P1
pickup annotations, and prefers the smallest available fret at concert pitch.
P2 v2 audio is byte-identical to v1. All four old v1 banks rebuild identically.
Paired v2 banks retain both actual performances and full source/cut provenance.

The chosen capture is Matt Hartman's RiffSmith Distorted full eight-channel A2,
MIT licensed explicitly alongside its repository. Fixed 48-kHz native inference
uses the documented causal/residual/head equations and immutable generated
weights. The reviewed boost/input trim precedes it; existing channel/return and
spatial processing follow. It includes cabinet coloration, so presets select no
extra cabinet. Preallocated fixed histories, cached silence, finite clamps and
sleep after a half-second silent tail bound runtime work. There is no dynamic
model parser or plugin host. See the optional amp-model asset provenance/licenses.

The reference golden fixture is original synthetic input (noise, chord tones,
level changes, silence), not commercial audio. The standalone native model
matches the pinned reference renderer within 0.000001 on its 297 checkpoints.
Four active model/boost chains cost approximately 0.47 seconds per second PCM on
the development PC; this is a measurement, not a broad real-time guarantee.
The first native dense-song guitar render matches the reviewed -30.25 LUFS stem
level with the backing/faders preserved. Full-band sound and concurrent runtime
acceptance remain separate from a mathematical golden comparison.
The subsequent native two-client run passed more than seven minutes per client,
including dense imported songs, simultaneous independent bands, live set changes,
late joining, private server-backed saves/reload/replay and owned-NPC cleanup.
Both clients closed and the normal local fixture was restored afterward. No
original profile/database/MIDI files changed. Broader performance/artist-tone
and captured connected/release gestures remain open.


## November 4 listening checkpoint (package 75 / API 34)

User references: https://www.youtube.com/watch?v=E0ozmU9cJDg (Master of Puppets,
remastered) and https://www.youtube.com/watch?v=vyA1z2A-lhU (Rip & Tear).
The user accepted the non-heavy curated band sets as decent starting points.
Heavy/doom fuzz, a staged-amp/scooped thrash chain and a separate processed
FreePats guitar bank did not meet the heavy target. C remains the preferred
thrash baseline. The revised industrial/parallel low-B candidate is better,
but not a finished Rip & Tear tone. Do not describe these as accepted artist
emulations. The FreePats processed bank remains an offline rejected experiment.

The local Master of Puppets MIDI lasts 473.18 seconds, versus 515 seconds for
the supplied recording, and starts at 220 BPM. It is an arrangement, not an
exact transcription. A Basic Pitch test on the first 12 seconds of the mixed
recording missed the opening chord and produced one mixed 36-note part, so it
is unsuitable as a replacement reference. Audio and generated diagnostics stay
private in ignored build directories. Do not redistribute commercial reference
recordings as sample content.

Further heavy work should evaluate a more faithful amp response with documented
input calibration, usable picked/palm attacks and complete band context. Keep
source-only/amp-only diagnostics and matched guitar-stem loudness. An offline
GuitarML IIC+-style model evaluation is a prototype, not installed/native support.
Model/code redistribution licenses must be audited separately from CC audio.

The native staged preamp and per-lane real alternate selection have bounded DSP
tests; the two-client soak covers dense imports, changes, persistence and cleanup.
These checks do not substitute for the remaining listening/recorded-gesture work.

Reviewed October 30, 2026 using the three supplied videos' English captions,
chapter descriptions and published descriptions. Completed slices are recorded
below alongside the remaining plan. Listening acceptance remains separate.

Package 54 follow-up: the first native cabinet/mix-context slice is implemented
and covered by DSP, UI and routed-output tests. It supplies three CC0 captures,
pre-drive/post-cab cuts, existing post-cab channel EQ and local focus/dim. The
string/articulation and experimental delivery notes follow below. Full-band
listening acceptance remains distinct from the automated measurements.

Package 55 begins the playable-string branch: per-strike articulation, pick
attack/variation/direction/pitch settling and live harmonic-resonance amount
are implemented for generated sources and gated banks. The Strings page and
Auto parameter selector expose them. String-aware strums, connected legato and
slides, external MIDI articulation mappings and new captured gestures remain
planned.

Package 56 implements the experimental branch as a native per-instrument insert:
parallel clean, independently waveshaped dirty and envelope-following noise;
oversampled soft/hard clipping, folding and rectification; mono modulated comb;
and bounded upward compression. Shared cabinet/tone/post-cab cuts follow the
blend. Three opt-in Industrial presets leave normal Rock/Metal sets alone.
Arbitrary FX graph routing and multiband dynamics remain future work.

Package 57 adds explicit string-aware fingering/strum authoring on Piano, with
pitch-preserving previews, tuning/span constraints, down/up/alternate attacks,
selected gate shortening and one-step Undo. These are ordinary saved note
edits, not live per-string synthesis or connected slides/legato. Those remain
within the playable-string branch alongside dedicated recorded gestures.

Package 58 supplies an initial native mono connected-performance layer: overlap-
gated legato or picked slides, finite pitch retargets and held-note return. It
preserves the previous source on unpicked transitions and supports generated and
gated sampled instruments. Per-string/polyphonic ownership and dedicated recorded
slide/hammer-on/release gestures remain outside this slice.

Package 65 supplies independent connected string ownership, standard/drop/seven-
string guitar and four/five-string bass tunings, deterministic automatic selection
and explicit attack-time string automation. Chords retain separate sources and
slides; held-note returns stay within their original string. Unplayable pitches
use normal polyphonic gates. Per-note stored fingering, same-pitch string unisons,
captured gestures and detailed finger animation remain outside this local slice.

Package 66 adds attack-time CC/keyswitch mapping and explicit track-level Auto
write. Native queued attacks retain separate articulation/string/direction values,
including earlier held-note returns; routed listeners receive the same bounded
snapshot. Keyboard-take/clip per-note expression capture remains pending, as do
new recorded gestures and listening-based acceptance. This is local-only.

Package 67 records those snapshots per note, preserves them in reusable sources,
and replays distinct same-frame attacks natively. Piano can edit/clear overrides;
existing patch and track controls still supply unmapped fields. General knob/fader
assignments, more recorded gestures and full-band listening remain planned.

Package 70 supplies original heavy/thrash/funk/clean/acoustic full-band projects
inside New Song, plus reproducible offline patch-only/authored-expression pairs,
amp-bypass guitar and rhythm-section stems. Auditions now include the saved
master, channel processing and return settings. All pairs retain identical note
counts and are level-matched only in review files. Full-band output and actual
private rock MIDI excerpts render without faults. This establishes repeatable
material for listening, not a new source recording or accepted artist tone.

The shipped electric bank remains regular DI plucks; muted attacks are modeled.
The bass bank manifests explicitly exclude release/ghost/keyswitch layers, while
the acquired source maps include staccato material not currently integrated.
Use the checks to choose captured-articulation and mix work; do not replace it
with another blanket distortion boost. Physical grips/sticks/pedals and same-pitch
string unisons remain within priority one.

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

The native engine now has plucked strings, recorded DI banks, modeled pick and
articulation controls, oversampled nonlinear processing, captured cabinets,
amp/channel EQ, compression and the opt-in parallel insert. Simple speaker
filters remain for legacy patches. The next gaps are connected/string-aware
performance, more captured gestures where useful, and full-band listening work.
A bounded mono comb does not replace room reflections or provide an arbitrary
FX-routing graph. Peak/RMS and compatibility checks establish headroom and
unchanged old paths; they do not establish convincing album tone.

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
