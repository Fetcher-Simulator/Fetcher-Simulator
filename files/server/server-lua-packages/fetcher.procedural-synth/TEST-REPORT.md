# Local validation report — October 15–16, 2026 (America/Chicago)

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
* No native UI screenshot/visual inspection was available in this session. The real editor created and operated without Lua errors; mouse/keyboard usability, layout at multiple resolutions, focus changes and operating-system focus loss still need interactive QA. Intended minimum layout is approximately 964×719 including margins. Sliders use numeric feedback; the waveform graph is a theoretical oscillator preview rather than a captured output waveform.
* Cell changes, malformed events, overlapping sessions and timeout cleanup were covered in the harness. Not all were repeated by walking/operating real clients. Sound device teardown is covered by native close/concurrency tests and ownership code review, not a physical audio-device hot-unplug test.
* Full game CPU/frame-time profiling, several simultaneous synth instances, packet-loss/jitter tests and prolonged listening remain outstanding. The microbenchmark and the exercised chord do not establish these performance goals.
* The original October 15 run used the vanilla lute. The Casio keyboard import and verified local installation were completed October 16, as recorded below.
* Listener snapshots intentionally omit already-held notes; listeners hear subsequent live note-ons. Native/session state is transient, while personal preset libraries are durable. The first prototype has no public preset sharing, sequencer, MIDI import, Bardcraft port or external synthesizer dependency.

The delivered fixture is switched back to interactive use without the automated probe after validation. Production feature activation stays disabled by default.

## Keyboard asset integration — October 16

The user supplied `keyboard-casio-vl-tone.zip`. Its SHA-256 is
`61cac05a2dd8e7434beb0946a68d1d737210870ea08309300597056cb2564715`.
The official public Sketchfab model API confirmed AleixoAlonso as author and the
exact CC BY 4.0 license URI. Source metadata, author credit, license links and
adaptation details accompany the converted bundle. Original FBX/Blend files remain
outside the repository.

Blender 4.5.3 with `io_scene_mw` exported Morrowind NIF 4.0.0.2, then reimported the
serialized NIF successfully. The model retains 6,896 visual triangles and separate
keys/buttons, plus 12 box-collision triangles. Verified dimensions are approximately
48 × 12.04 × 5.07 game units. Two materials use a 2K DXT1 atlas and approximate LCD
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

## Bone API and Bardcraft MIDI — October 16

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

## Combined stage, world transforms and camera controls � October 16

The default local asset is now one floor-origin `keyboard-stand.nif`, combining
the user-supplied Yamaha keyboard and X-stand under their verified CC BY 4.0
licenses. Attribution and provenance for both authors accompany the runtime
bundle; the older Casio remains available. Blender export/reimport and preview
inspection passed: 12,189 visual triangles plus 84 collision triangles, dimensions
104 � 30.16 � 111.68 units, four mipmapped diffuse DDS maps and two optional normals.
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
