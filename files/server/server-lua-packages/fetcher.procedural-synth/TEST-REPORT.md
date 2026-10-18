# Local validation report - October 15-17, 2026 (America/Chicago)

The prototype builds and passes its automated checks. Two real compatible clients were exercised against a local dedicated server with only Morrowind.esm, Tribunal.esm and Bloodmoon.esm. No production host was contacted or modified.

## Commands and results

Commands run from `C:\serena_workspaces_directory\openmw`:

```powershell
cmake --build MSVC2022_64 --config RelWithDebInfo --target openmw --parallel 3
cmake --build MSVC2022_64 --config RelWithDebInfo --target openmw-server --parallel 3
cmake -S scripts/procedural-synth-tests -B build/synth-core-tests -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=
cmake --build build/synth-core-tests --config Release
ctest --test-dir build/synth-core-tests -C Release --output-on-failure
build/synth-core-tests/Release/synth-tests.exe
lua scripts/test_procedural_synth.lua
lua scripts/test_procedural_synth_client.lua
lua scripts/test_consuming_animated_relay.lua
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-procedural-synth-test.ps1 -LiveProbe
```

Both application targets: **PASS**. Existing compiler warnings in other multiplayer code remain; no compiler errors in the final build. A first build exposed an incorrect RefData deletion accessor; it was corrected to the CellRef count check and rebuilt successfully.

CTest: **1/1 PASS**. The standalone DSP executable checks idle silence, finite bounded PCM, NaN/infinity/control/note validation, A4 frequency, continuous sustain, live low-pass response, release to silence, every waveform, both oscillators, 16-note chords, bounded stealing, panic, queue overflow, shutdown and concurrent audio/control activity. A 16-voice dual-triangle run generated two seconds of PCM in approximately **0.24 seconds** on this Windows machine (a microbenchmark, not full-game profiling).

Server Lua: **9 tests / 62 assertions PASS**. These cover format/defaults, schema parity, exclusive ownership, authoritative object/distance checks, authentication and session epochs, sequence rejection, eligible recipients, malformed/oversized/nonfinite fields, rate limits, safety releases, all preset storage operations, stable IDs, namespace isolation, future-version preservation, module reload persistence, disconnect, timeout, cell change, object removal and late snapshots without historical notes.

Client/editor Lua harness: **196 assertions PASS**. It executes the actual editor and player scripts with mocked engine/UI bindings, including all 26 editable controls, optimistic chords, parameter coalescing, preset application, stale events, editor/native cleanup, listener playback, cell change, liveness expiry, mouse-position velocity and mouse release outside the key. This does not substitute for visual inspection or physical keyboard/mouse testing.

Existing consuming-animation relay regression: **14 tests / 63 assertions PASS**. Whitespace check: **PASS**.

The empty toolchain override on standalone test configuration avoids an unrelated stale `CMAKE_TOOLCHAIN_FILE` environment setting pointing at `C:/tes3mp52/...`; the engine uses its existing configured build directory.

## Actual two-client run

Fixture: `C:\serena_workspaces_directory\fetcher-simulator\test-server\procedural-synth`, local UDP 25567. Launches use the copied current PowerShell launcher through the fixture batch wrappers. The fixture database contains only its test identity and vanilla Synth A/B characters; original local profiles and data are preserved. The dedicated server places and synchronizes one dynamic lute-model activator without a client ESP or downloaded asset.

Observed from the actual clients and native audio status:

* Both clients authenticated, received the server Lua package and runtime activator, and entered ToddTest.
* Synth A invoked the actual activator's Lua activation path and opened the editor. The live script then used the editor's own callback/interface path.
* Three simultaneous C-major notes produced **three native voices and nonzero PCM peaks on both clients**. Player B's source is a normal mono 3D OpenAL stream attached to the activator reference.
* In the first successful run A logged playing at **22:09:58.099** and B logged all three relayed note-ons at **22:09:58.132**: approximately **33 ms from local play action to remote event receipt** in that run. This is not a measured speaker-onset latency or a general network guarantee. OpenAL additionally queues approximately 30 ms of interactive audio.
* Live waveform and cutoff changes were relayed while the chord remained active. Later runs logged the remote native instance's updated cutoff/wave state, as well as its nonzero generated output.
* Releasing the notes returned both clients to **zero voices / zero PCM peak**. Closing removed the session; reopening reacquired it and played a new note.
* A saved `Integration Tone` as `p:1`, loaded Soft Pad, then loaded `p:1`; both clients received cutoff **1234** for the saved tone.
* The SQLite character-storage namespace contained the saved library immediately. After a real server restart and both clients reconnecting, A's initial catalog still contained it, and `p:1` loaded with cutoff 1234. The activator count remained **one** across restarts. B's personal catalog did not contain A's presets.
* While A held note 76, its process was terminated abruptly. B received cleanup, acquired the now-free activator and played/released a note. It logged **PASS acquired after abrupt owner disconnect** and **PASS observer takeover complete**. No indefinitely held native voice or permanent lock remained.
* A further run delayed B's launch by 40 seconds (`-SecondClientDelaySeconds 40`), until A was already sustaining a note. B logged **initial listener snapshot voices=0 receivedNotes=0**, while A reported one sustained voice and nonzero PCM. Current parameters were received without replaying the older note-on.

Local logs are retained under the fixture's `evidence/run1`, `evidence/run2` and `evidence/late-join` directories, plus the per-character profile logs and server logs. Generated databases, authentication keys, binaries and live logs are not committed.

Early fixture failures (missing defaults.bin, prefixed interior names and an incomplete mesh path) were corrected in the reusable setup before these successful runs. The setup now uses resources and defaults from the same build as its executables.

## Remaining acceptance work and limitations

* Final speaker output, perceptual timbre/click/dropout quality, stereo positioning and walking-distance attenuation still need a person to listen and move the observer. Nonzero generated PCM and an attached 3D OpenAL source establish the engine path, not those perceptual outcomes.
* No native UI screenshot/visual inspection was available in this session. The real editor created and operated without Lua errors; mouse/keyboard usability, layout at multiple resolutions, focus changes and operating-system focus loss still need interactive QA. Intended minimum layout is approximately 964Ã—719 including margins. Sliders use numeric feedback; the waveform graph is a theoretical oscillator preview rather than a captured output waveform.
* Cell changes, malformed events, overlapping sessions and timeout cleanup were covered in the harness. Not all were repeated by walking/operating real clients. Sound device teardown is covered by native close/concurrency tests and ownership code review, not a physical audio-device hot-unplug test.
* Full game CPU/frame-time profiling, several simultaneous synth instances, packet-loss/jitter tests and prolonged listening remain outstanding. The microbenchmark and the exercised chord do not establish these performance goals.
* The original October 15 run used the vanilla lute. The Casio keyboard import and verified local installation were completed October 16, as recorded below.
* Listener snapshots intentionally omit already-held notes; listeners hear subsequent live note-ons. Native/session state is transient, while personal preset libraries are durable. The first prototype has no public preset sharing, sequencer, MIDI import, Bardcraft port or external synthesizer dependency.

The delivered fixture is switched back to interactive use without the automated probe after validation. Production feature activation stays disabled by default.

## Keyboard asset integration â€” October 16

The user supplied `keyboard-casio-vl-tone.zip`. Its SHA-256 is
`61cac05a2dd8e7434beb0946a68d1d737210870ea08309300597056cb2564715`.
The official public Sketchfab model API confirmed AleixoAlonso as author and the
exact CC BY 4.0 license URI. Source metadata, author credit, license links and
adaptation details accompany the converted bundle. Original FBX/Blend files remain
outside the repository.

Blender 4.5.3 with `io_scene_mw` exported Morrowind NIF 4.0.0.2, then reimported the
serialized NIF successfully. The model retains 6,896 visual triangles and separate
keys/buttons, plus 12 box-collision triangles. Verified dimensions are approximately
48 Ã— 12.04 Ã— 5.07 game units. Two materials use a 2K DXT1 atlas and approximate LCD
glass; an optional 1K DXT5 normal map is included. Both DDS files have mipmaps.
The rendered reimport was visually inspected for texture resolution, geometry and
orientation. This is an asset preview, not an in-game screenshot. The keys remain
static geometry.

`package_assets.py` passed NIF/DDS header and manifest checks and produced an
818,793-byte ZIP. Setup verified all seven manifested files before and after
installing them into the shared local client/server data directory. A separate
`-VanillaSynth` setup run confirmed that the fallback removes the custom model/data
configuration; the following default setup restored the keyboard.

An actual dedicated-server restart updated the persistent activator's model to
`meshes/fetcher/synth/keyboard.nif`. A read-only SQLite query confirmed that path in
`world_dynamic_records` and exactly one placed synth in `world_objects`. Both real
clients authenticated, entered ToddTest and ran the existing live probe with the
new assets installed. Synth A opened the editor at 15:14:59, both clients produced
three native voices and nonzero PCM, live waveform/cutoff edits propagated, and
the save/load/close/reacquire sequence completed. After A was terminated while
holding a note, B logged `PASS acquired after abrupt owner disconnect` at 15:15:47
and `PASS observer takeover complete` at 15:15:48. Client logs contained no missing
mesh/texture or Lua failure reports during this run. Live visual positioning and
physical mouse-target/collision behavior still need interactive inspection.

CTest 1/1, server Lua 9 tests/62 assertions, client Lua 196 assertions, and existing
relay regression 14 tests/63 assertions were rerun and passed. Python conversion
helpers parsed successfully; whitespace validation passed. Evidence is retained
locally under the fixture's `evidence/keyboard` directory. Setup was returned to
interactive use without the probe. The same fixture batch launchers now use the
keyboard by default; no production configuration was changed.

## Bone API and Bardcraft MIDI â€” October 16

The client was rebuilt with `openmw.animation.boneTransformsVersion = 1`:
`getBoneNames`, `getBoneTransform`, `setBoneTransform` and `clearBoneTransform`.
Setters queue graph changes at the synchronized Lua action boundary. Reads are
restricted to synchronized `onFrame`/input callbacks; unposed reads exclude these
offsets on ancestor bones. The controller restores the underlying matrix before
ordinary animation and applies its offset after it, preserving scale and avoiding
feedback on partially keyed or static transforms. Leases expire after 0.5 real
seconds by default. Animation-group changes reattach controllers; model rebuilds
discard them. `openmw.mp.getPlayerObject` resolves the server-supplied owner GUID.

New validation commands, from the repository root:

```powershell
cmake --build MSVC2022_64 --config RelWithDebInfo --target openmw --parallel 3
cmake -S scripts/bone-pose-tests -B build/bone-pose-tests -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE= -DCMAKE_PREFIX_PATH=C:/serena_workspaces_directory/openmw/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows
cmake --build build/bone-pose-tests --config Release
$env:PATH = "$PWD\deps\vcpkg-x64-windows-2022-2026-02-24\installed\x64-windows\bin;" + $env:PATH
ctest --test-dir build/bone-pose-tests -C Release --output-on-failure
lua scripts/test_procedural_synth_midi.lua 'C:\serena_workspaces_directory\fetcher-simulator\Data Files\fetcher-simulator\Gameplay\BardcraftOpenMW\midi\Bardcraft\preset\greensleeves.mid'
lua scripts/test_procedural_synth_pose.lua
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-procedural-synth-test.ps1 -MidiProbe
```

Application build: **PASS**. Native bone CTest: **1/1 PASS**, covering finite/bounded
inputs, zero-quaternion rejection, weighting, 200 repeated partial-animation
updates without position/rotation accumulation, scale preservation, lease expiry,
static transforms, explicit restore and osgAnimation skeleton-space updates.
The synth DSP CTest remains **1/1 PASS**. Server Lua now passes **10 tests / 66
assertions**, including authenticated performer identity despite a spoofed owner
field. Client/editor harness passes **298 assertions**, including resumed-character
MIDI discovery, catalog-only paths, synth/relay playback, preset interruption,
manual-key takeover and close cleanup. MIDI parser/player checks pass **21
assertions** with the installed Greensleeves file, including tempo changes,
running status, velocity, sustain, shared pitches across/within channels, malformed
data and frame-stall cleanup. Arm geometry passes **16 assertions** for link
lengths, reach limits and degenerate targets. Existing relay regression remains
**14 tests / 63 assertions PASS**.

Local setup copied **21 MIDI files** from the existing Bardcraft installation into
the fixture's `midi-data` directory. No Bardcraft scripts, ESP or prerecorded
instrument samples are needed. The new parser read Greensleeves as **1,221 retained
events, 11 tracks, 40.52 seconds**. Both actual clients produced polyphonic native
PCM through the existing 3D synth and reported six active performer pose bones.
The observer probe pointed its camera toward the performer to exercise visible
bone updates: culled skeletons intentionally stop evaluating until visible.
After solving IK before nonuniform race/model scaling, logged hand-to-clamped-IK
target error was approximately **0.000 game units on A and 0.005 on B** in the
steady-state run. This measures the computed reachable target, not contact with
the keyboard's keys. The complete song ended and closing the editor removed the
listener instance. Local evidence is retained in `evidence/midi`.

The final MIDI run logged no Lua errors. A also logged 48 native `RigGeometry
rendering with no skeleton` messages during initial character loading, before
the synth was acquired or any scripted bone pose was installed; B logged none.
Those startup rendering messages remain outside this feature's validation.

Limits: all melodic MIDI channels use the current preset. Percussion, program
changes, pitch bend and expression/volume controller rendering are outside this
pass. Notes use the existing 40/sec, burst-80 server budget; excessive density or
a frame stall stops playback safely. The small standless keyboard still needs a
replacement or stand and proper placement. Arms clamp to reachable targets;
there is no actor movement, key animation, finger IK or automatic facing. No
in-game screenshot or human assessment of posture/hand contact was obtained.
The fixture is restored to interactive mode after validation.

## Combined stage, world transforms and camera controls — October 16

The default local asset is now one floor-origin `keyboard-stand.nif`, combining
the user-supplied Yamaha keyboard and X-stand under their verified CC BY 4.0
licenses. Attribution and provenance for both authors accompany the runtime
bundle; the older Casio remains available. Blender export/reimport and preview
inspection passed: 12,189 visual triangles plus 84 collision triangles, dimensions
104 × 30.16 × 111.68 units, four mipmapped diffuse DDS maps and two optional normals.
Packaging produced a 3,521,484-byte assets-only ZIP; setup verified all 16 manifest
files before and after installation. The new `-ResetStage` option explicitly
backs up the isolated SQLite database before resetting the stage layout.

The earlier small numerical hand errors did not establish correct rendered arm
positions: the user supplied an in-game screenshot with arms raised overhead.
The native world-transform getter included an absolute viewer camera in its node
path. It now follows OSG's world-transform convention, skipping that camera and
its ancestors, including when resolving unposed ancestor matrices. A regression
compares the result to `osg::computeLocalToWorld` and verifies camera motion does
not move the returned world bone position. The application rebuild passed; native
bone CTest and synth DSP CTest each pass 1/1. In-game inspection confirmed the
combined keyboard model renders and the performer's first-person hands are near
the keys. Third-person hand contact and posture across races remain visual QA.

The observer camera lock came from `midi-player.lua` forcibly setting yaw/pitch
every frame. Those calls and the camera dependency were removed. The runtime
performance module does not steer cameras. Hide / Escape / F8 now dismisses the
editor while retaining the session, MIDI scheduler and heartbeat. F8 or activation
reopens the same session; Finish stops playback and releases it. Hidden manual
keys release, game keys stop acting as piano keys, and preset/name/selection
state survives reopening and incoming updates. Another UI mode is preserved
when it hides the synth. The public interface is version 3 (`hide`, `show`,
`editorVisible`); `close` still ends the session.

Client/editor harness: **790 assertions PASS**, including a hidden session held
past the server lease with regular liveness, hidden MIDI loading, playback to
completion, reopening without new acquire/events, pending edit flush, hidden
preset responses, Finish and disconnect cleanup. Server Lua: **10 tests / 66
assertions PASS**; MIDI parser/player **21 assertions PASS**; arm geometry **16
assertions PASS**; relay regression **14 tests / 63 assertions PASS**.

In the actual two-client camera-toggle run, Synth A logged playback and six active
pose bones at 16:44:04, hid its editor for 11 seconds, then logged **PASS hidden
MIDI heartbeat and reopen without interruption** at 16:44:15. Synth B received
polyphonic native PCM and logged **PASS listener audio and performer pose** at
16:44:11 with camera steering removed. The probe's final wall-clock completion
assertion did not pass during the interactive run; it does not establish song-end
behavior in that run. Deterministic playback-completion and Finish tests above
passed. Native-window automation encountered user input and further UI input was
stopped. Logs are retained privately in the fixture's `evidence/camera-toggle`.
The fixture was then reinstalled without either probe and restarted for normal
interactive testing. No production configuration or original local database was
modified.

## Studio / multitimbral MIDI / drum and piano clips - October 17

The engine was rebuilt successfully with `openmw.synth.version == 2`: native
white-noise oscillators, an exponential per-voice pitch envelope and a 32-stream
client-wide limit. DSP CTest passes 1/1, including all five waveforms, finite
noise percussion, decay to silence and a measured downward kick pitch sweep.
The bone-pose regression remains 1/1 PASS. No server binary change was required;
the fixture uses the updated server Lua files and shared document validator.

The package is version 3, with `I.ProceduralSynth` interface version 4. SMF import
preserves names, track/channel identities, programs and original tempo maps.
Channel-10 drum pitches become separate lanes. Each of up to 16 tracks selects
its own personal/built-in preset snapshot; native streams are lazy and have at
most eight voices per DAW track. Mixer level/mute/solo/master changes are relayed
with channel configuration. Note identity includes channel and pitch; late
listeners receive all timbres before future notes. Deleting tracks releases their
streams. The original per-owner 40/sec, burst-80 note budget still applies to the
whole project, with release/stop cleanup on rejection.

Studio provides Synth/Mixer/Piano/Drums navigation, independent sound editing,
editable sixteenth-note piano clips, velocity steps, native kick/snare/hat presets,
pad audition, 1-4-bar patterns, swing and looping. Imported MIDI has a full-length
piano-roll view; editing that source MIDI remains outside this pass. Real elapsed
time drives the transport rather than filtered engine frame duration. The earlier
song-end wall-clock failure was not reproduced with that clock: complete
Greensleeves now finishes at its parsed duration of 40.52 seconds.

A bounded version-1 project document stores one working project per authenticated
character under `ProceduralSynth/project`, separate from `presets`. It includes
sound snapshots, mix/routing, clip notes, drum steps and a MIDI VFS path. Unknown
future projects cannot be loaded or overwritten. Save acknowledgements identify
the session and saved generation; later edits stay marked unsaved. Original MIDI
files are still required locally and are not redistributed or stored as audio.

Final automated checks:

* Server: **12 tests / 88 assertions PASS** (channel isolation/removal, snapshots,
  per-character project persistence, unchanged preset storage, future documents).
* Client/editor/Studio harness: **1,043 assertions PASS**, including real window
  callbacks, clip creation, preset routing, sound edits, hide/reopen, project save
  generations and real-clock playback despite a different engine frame delta.
* Workstation transport/model: **38 assertions PASS**, including swing, note
  durations, independent MIDI parts, tempo changes, mute/solo/master, pad audition,
  stale MIDI cleanup, loop-off clips alongside full MIDI, validation and limits.
* MIDI parser/player: **27 assertions PASS** with the installed Greensleeves file,
  including same-pitch parts on one channel, names/programs and split drum pitches.
* Existing arm geometry: **16 assertions PASS**. Existing relay: **14 tests /
  63 assertions PASS**. Whitespace check: PASS.

Actual two-client run with `-DawProbe`: five parts were imported from the local
Bardcraft Greensleeves MIDI; the first two used Flute-like and Bass. Synth B
reported native voices on both independent channels. A hid Studio for 12 seconds
and reopened it while playback continued. The full song ended, then native drum
steps and piano clips ran together. A saved and loaded the named project through
the server, verified both assignments, then closed; B received cleanup. The final
run logged all PASS milestones at 13:56-13:57 America/Chicago. An earlier retry
caught a catalog-arrival race; built-ins are now immediately available, with
personal catalog data arriving asynchronously.

A read-only SQLite check found the saved project as a 3,660-byte blob for Synth A,
with both test characters' existing preset blobs still present. A private SQLite
backup was taken before integration. Actual native-window inspection verified
Mixer rows, selecting a preset, the full MIDI piano roll, adding a snare lane and
changing a step. The local evidence is retained under the fixture's `evidence/daw`
and `evidence/daw-first-pass` directories, outside Git. Setup was returned to
interactive mode without the probe and both clients were restarted.

Limits: outputs currently share the placed keyboard. Held-instrument destinations,
recording/overdub, source-MIDI editing/export, arrangement editing, effects,
automation and pan controls are future work. There is one saved working project
per character, 16 total tracks, 128 clip notes per track / 512 total and at most
512 drawn notes per piano-roll view. Native source exhaustion and dense songs stop
playback with a message. No final-speaker listening assessment, large multi-DAW
CPU profile or packet-loss/jitter measurement is claimed. Production was not
contacted or modified.

## Instrument monitoring, sixteen bars and window controls - October 17

Native RelWithDebInfo client build: PASS. Server/schema tests: 12 tests / 89
assertions. Workstation: 59 assertions. Real client/editor/Studio harness: 1,657
assertions. Existing MIDI and arm geometry suites: 27 and 16 assertions. The
regressions cover all sixteen loop lengths; hidden piano/drum bars surviving a
short-loop save/load; legacy short-pattern migration; final-step playback in bar
16; silence for hidden bars; scaled mouse hit testing; repeated dynamic updates;
window bounds, fullscreen/restore and persisted preferences; and local monitoring
without project or network edits.

The actual two-client DAW probe imported and played the five Greensleeves parts,
continued while hidden, then played drum/piano clips. It created notes and steps
in bars 2 and 16, cycled every loop length, saved with one active bar, loaded from
server SQLite and recovered the later bars. The retained bar-16 piano note was
also inspected in the native piano roll. A private fixture database backup was
taken before testing. The test project is restored from that backup afterward;
personal preset blobs are compared without printing their contents.

Native UI inspection verified scale changes, fullscreen/restore, dragging,
resizing and geometry shared by Synth/Studio. This caught an existing engine
LuaWindow bug: drag deltas were applied to a zero rectangle. Gestures now start
at the current rectangle, and native dragging/resizing was retested successfully.
Sound options displayed Player Instruments at 50% after the setting changed in
Studio. Changing it there to approximately 76% updated the native getter and
persisted in the isolated client's settings.cfg; Synth B remained at 100%.
Invalid native monitor values (negative, above one, NaN and infinity) were rejected.
Private logs are under the fixture's evidence/window-bars directory. Final-speaker
loudness and perceptual mix quality were not measured.

## Native drum-machine milestone - October 17

Native client and standalone DSP builds pass. The new native tests cover fixed
drum tuning across MIDI notes; independent noise decay; one-shot tails after
note-off; overlapping same-pitch hits and voice limits; panic/choke cleanup;
finite output and extreme controls for all four drum types. The test executable
also renders an eight-second normalized WAV preview of solo hits and a beat.

The Lua regressions cover precise milliseconds/kHz/percent entry, invalid drafts,
visible sub-millisecond increments, all 41 editable native parameters, scaled
controls, drum view selection, listener hat choke, 64-edit history, undo/redo
branching, grouped kit creation, track sound undo and legacy document migration.
The server test ensures Stop still reaches a one-shot tail after note-off while
repeated empty panic requests remain suppressed. Imported GM snare/open/closed
hat parts choose the corresponding fixed-tuning drum engines.

The real two-client drum probe exercised four native drum lanes, kit and step
undo/redo, hat choking on the listener and server SQLite save/load of the new
parameters. Native UI inspection verified a typed 150 ms body decay, a 1 ms
increment and Undo restoring 121 ms. Private evidence and the engine-rendered
WAV are under the isolated fixture's evidence/drum-milestone directory. The
fixture database is backed up before the run and its previous synth project and
preset blobs are restored and compared afterward. No production deployment.

Final results: server 13 tests / 94 assertions; workstation 78 assertions;
client/editor/Studio harness 3,877 assertions; MIDI 27 assertions; arm geometry
16 assertions; standalone native DSP ctest 1/1; RelWithDebInfo client build PASS.
The final live run also explicitly verified Stop after note-off on both clients.


## Guitar/effects and preset banks - 2026-10-18

Native API 5 / package 6. All checks use the isolated port-25567 fixture.

- Release native core test and full RelWithDebInfo openmw build pass.
- String tests cover pluck output, MIDI pitch across guitar registers, shortened
  palm-mute decay, repeated-note gates, extreme settings and panic/release.
- Insert tests cover exact legacy bypass, distinct drive characters, finite
  extreme controls and silence after tails. A 9 kHz hard-distortion probe measures
  folded 3 kHz/fundamental ratios of 0.00964 at 4x versus 0.13291 at 1x (about
  22.8 dB reduction in this probe). This is not a claim of alias-free processing.
- A standalone, interleaved stress render of 16 instruments x 8 string voices,
  all with active 4x distortion and speaker stages, takes about 0.44 seconds for
  one second of PCM on this machine. This is not an end-to-end game profile.
- All 45 built-ins pass actual native parameter validation, chord/single-note
  rendering, soft-velocity/register checks and finite PCM checks. Levels were
  reviewed numerically; rendered auditions are available for listening review.
- Lua server: 14 tests / 203 assertions. Workstation: 82 assertions. Real
  player/editor/Studio scripts in the mock harness: 5324 assertions (including
  many lifecycle updates). MIDI: 26 assertions. Arm geometry: 16 assertions.
- Server tests cover new parameter validation, personal patch round trips, full
  late-listener configurations, catalog metadata and preserved legacy defaults.
  Workstation tests cover genre kits as one undo action and complete guitar
  insert snapshots in project persistence and undo.
- Native two-client probe: independent clean and metal guitar tracks produce
  audio, preset undo/redo succeeds, the server SQLite round trip retains string
  and drive/cabinet controls, and Stop releases the tested guitar voices. Synth B
  verifies both native sources and their independent effects configurations.
- Final late-join run hit the existing game-frame transport safety stop when the
  observer joined. Restarting Play clips through the real Studio UI completed
  the observer check. Audio-clock sequencing remains the next milestone; this
  slice does not fix game-frame timing or promise uninterrupted join playback.
- Computer-use inspection confirms the physical-string controls, effects page,
  genre-filtered browser, pagination and Load + Audition. Audition started a native
  voice and released it. The string view shows its signal path rather than an
  unrelated oscillator illustration.

Private fixture evidence is in evidence/guitar-milestone: Synth_A.log / Synth_B.log
(first successful pass), final client logs, and native-guitar-preview.wav. The
14-second preview renders eight guitar patches in catalog order, 1.75 seconds
per patch, with one common output scale: Clean Electric, Warm Jazz Guitar,
Muted Funk Guitar, Blues Breakup, Crunch Rhythm, Metal Palm Mute, Singing Guitar
Lead and Garage Fuzz. No perceptual realism claim is inferred from PCM tests.

The original ProceduralSynth project/preset rows are restored byte-for-byte from
the fixture backup after testing. All 436 custom MIDI files match source SHA-256
hashes. Other tests, production services and unrelated working-tree files are
outside this milestone.


## Native Studio sample-clock transport - 2026-10-18

API 6 / package 7. The optional native Studio shares one PCM stream and clock
across its instrument/effects lanes. Native track slots publish raw atomic
pointers once and retain ownership off the render path. Score parsing/validation
and allocation run on the control thread; rendering uses fixed gate storage,
try-lock control consumption and retained immutable score data.

Validation:

- Full RelWithDebInfo openmw build and standalone Release native ctest pass.
- Native Studio tests compare exact PCM with 480- and 137-frame render blocks;
  scheduled onsets, two-lane timing and cross-loop gates remain identical.
  Future delay, historical-attack skipping on seek, finite completion, repeated
  gates, live replacement, Stop cancellation, post-Stop live notes, validation
  and release of old gates after a finite replacement are covered.
- Native render faults are bounded; score/active-gate/voice/unit limits are
  explicit. Invalid/nonfinite controls, malformed bytes and unconfigured lanes
  are rejected. Stop uses a short release and clears future scheduled gates.
- Lua server: 16 tests / 215 assertions, including authenticated uploads,
  binary validation, future shared start, late snapshots, malformed/incomplete
  chunks and suppression of duplicate/invented Stop floods.
- Sequence compiler/native workstation: 21 assertions, covering exact swing
  samples, cross-loop gates, finite clipping, sustain, MIDI tempo changes,
  game-frame independence, live rebuilds and stopping before loop-mode changes.
- Transport tests cover half-RTT clock estimates, bounded upload/commit order,
  future starts, replacement without restart, late seeking, Stop tombstones
  and stale-session download protection. The server/client sequence copies
  compare byte-for-byte.
- Existing workstation reference tests: 82 assertions. Client/editor/Studio
  mock lifecycle tests: 5324 assertions; the mock retains the reference path,
  while native behavior is covered separately by the native/transport tests and
  real two-client fixture. MIDI: 26 assertions. Arm geometry: 16 assertions.
- All test commands and diff whitespace checks pass.

Final real two-client TimingProbe:

- Synth A runs kick, snare, closed/open hats and guitar clips together.
- A deliberate 1.5-second busy Lua loop leaves native playback running; native
  position advances 1.5 seconds and 18 scheduled strikes execute during it.
- A live snare edit queues a bar-boundary replacement without stopping playback.
- Synth B joins the already-playing performance, receives the score/configuration
  and plays from the current phase without restarting the performer.
- Final estimated audible-phase difference is -0.00142 seconds with a 0.0343 s
  clock RTT in this local run. An earlier queue-compensated run measured
  -0.000164 s. These are software estimates, not microphone, DAC or driver
  latency measurements; continuous cross-device drift correction is not tested
  or implemented.
- Stop cancels native queued notes and leaves zero tested voices on both clients.
- Imported Greensleeves with its MIDI tempo map and accompanying clips plays
  through the native transport. No frame-stall safety stop or manual restart is
  needed for the late join.

Private evidence: isolated fixture evidence/timing-milestone/Synth_A-final.log
and Synth_B-final.log, with earlier pass logs retained alongside them. Setup
-TimingProbe deliberately stalls Lua and belongs only in the disposable fixture.
The normal setup removes the probe. Before delivery, the original project and
personal preset rows are restored exactly from timing-milestone-backup.db and
all 436 custom MIDI files are checked against source SHA-256 hashes.

This milestone does not add separate physical instrument destinations, stereo
mixing/sends, recording, arrangement automation, held-note reconstruction on late
join or long-term device-clock correction. Those remain on the roadmap.


## 2026-10-18 - native channel mixer (API 7 / package 8)

Implemented a native mono channel strip for each of the 16 Studio lanes, with
EQ/compressor bypass, project snapshots, undo/redo and listener replication.
No production deployment was performed.

Validation:

- Full RelWithDebInfo OpenMW client build and standalone CTest: PASS.
- Native mixer: sample-exact default bypass, fresh muted-lane silence, high-pass
  rejection/passband, shelf response and mid cut. Analytical 4:1 compression
  settles at 15 dB reduction for a 0 dB constant input / -20 dB threshold;
  attack, release recovery, makeup, bypass and exact final mute: PASS.
- Extreme EQ/compressor settings remain finite across 16 strips. An isolated
  16-strip benchmark took about 0.023 seconds per second of PCM on this machine;
  this excludes voice synthesis, OpenAL and game rendering.
- Native Studio PCM remains identical at 137- and 480-frame render sizes with
  EQ/compression active; lane/master meters and invalid native settings: PASS.
- Server Lua: 17 tests / 224 assertions, including old-document bypass defaults,
  malformed mixer rejection, owner checks, per-character persistence, restart and
  late snapshots. Client/editor/UI harness: 5558 assertions. Workstation: previous
  82 assertions plus mixer patch-independence, round trip, undo/redo and bounds.
  Native sequence/compiler 21, MIDI 26 and arm geometry 16 assertions: PASS.
- Transport harness: pending channel settings delay score upload until complete;
  clock/start/replace/late-join/Stop checks remain passing.

Local two-client test on port 25567:

- Synth A saved/reloaded a five-lane project with active EQ/compression and
  exercised undo/redo; native track/master RMS and compressor reduction were live.
- Synth B joined the running loop and received the same saved controls and
  active compressor. An 80-step bass edit reached its final -5 dB value on B.
- Both clients continued playing and then released all voices after Stop.
- First probe attempt checked reduction during detector startup; the probe was
  corrected to wait for active reduction. The complete repeated run passed.
- Processor and main mixer pages were inspected in the running native client at
  the existing 196% GUI scale. Controls, units, bypasses and meter text fit.

Evidence is private to the isolated fixture's evidence/mixer-milestone directory.
Original character project/preset rows are restored after the probe; MIDI assets,
monitor preferences and world placement are preserved. This validates source PCM
and local software behavior, not measured sound-card output, transparent dynamics
at every setting, or hardware synchronization between clients.


## 2026-10-18 - two-octave audition, selectors and shared returns

API 8 / package 9. Added C4-C6 virtual audition, searchable selectors with a
scrollbar and wheel navigation, and shared native mono delay/reverb returns.
No production deployment was performed.

- Full RelWithDebInfo OpenMW build and standalone CTest pass. Return tests cover
  exact 40 ms impulse timing at zero feedback, isolated buses, causal/decaying
  reverb, maximum feedback/decay stability, 10 ms Stop fade and clear, resume,
  validation, return meters, and identical PCM at 137-/480-frame block sizes.
- Server Lua: 18 tests / 231 assertions. Added invalid/unauthorized global return
  controls, saved sends, restart, late snapshots and dry old-project migration.
- Client UI harness checks C6 note 84, outside release, a 512-entry search,
  scroll clamping/wheel behavior, exact selection, literal search, focus, Escape
  and lifecycle cleanup. Workstation checks post-return master separation,
  saved sends/returns, undo/redo and feedback validation. Existing sequence,
  transport, MIDI and arm geometry checks continue passing.
- Live two-client fixture: A saved/reloaded sends, delay 375 ms and reverb 2.8 s,
  undid/redid edits, and played the native clock. Both return meters were active.
  B joined mid-performance, received the same effects/sends and wet output,
  received master 0.35, and drained wet output after Stop. A also passed Stop.
- GUI checks use the existing 196% scale and 457-song catalog. Wheel scrolling
  and scrollbar jumps were exercised live. The drag fix has a persistent-widget
  capture regression check; ongoing human input prevented a final automated drag. Popup layer
  prevents underlying widgets showing through. Persistent row/scrollbar widgets
  retain text focus and mouse capture instead of rebuilding the popup on drag.

Private logs: isolated fixture evidence/returns-milestone. Original character
synth storage is restored after testing, and all 436 custom MIDI files retained.
Wet delay/reverb history is rebuilt locally after late join; only settings and
score phase are replicated. Output remains one mono spatial keyboard emitter.
