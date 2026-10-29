# Local validation report - October 15-17, 2026 (America/Chicago)

## October 29: accepted tuning exports (package 43)

Accepted seven export blocks from the latest ordinary Synth A log: seated
keyboard, violin, timpani, trumpet, flute twice and seated cello. The later flute
block wins. Captures are parsed as numeric data, checked against reference 37
and merged into the existing approved defaults; older cello, clarinet, hand drum
and lute calibrations remain intact. Standing keyboard keeps its built-in pose.
Seated exports include stool scale and all foot/leg settings. Zero-only exploratory
bone fields are omitted without changing their effect. The timpani fallback hand
height is applied before approved calibration so it cannot replace exported values.

The calibration suite verifies all approved export fields, storage migration,
Reset, Preview Off and absence of doubled rotations (594 assertions). The client
UI lifecycle suite passes with the updated seated defaults (17,170 assertions).
Captures and their hashes are retained outside the repository in
`synth-assets/pose-calibration/2026-10-29`; selection evidence is in
`build/studio-default-update-20261029.json`. These data changes require no native
client/server rebuild or protocol change.

## October 29: live seated-pose and bench controls (package 42)

All 28 Lua suites pass. Calibration/UI checks cover seated-only validation,
bounded scales and finite values, independent instrument/foot settings, numeric
editing and local-only updates, saved delta migration/reload, reset, preview
toggle and export. Existing standing exports remain in the same reference frame.

Native two-client checks on private port 25571 verify the default keyboard and
cello seated rigs, live pelvis/foot targets and foot orientation, storage/export,
Preview Off and Reset, and subsequent standing/seated restaging and cleanup.
A requested four-unit pelvis drop measured -4.0001 units; the left foot moved
about -7.05 units forward/back while the untouched right foot changed less than
0.01 unit. The right foot orientation changed with its rotation control. A late
observer retained its own default presentation. Evidence:
`build/studio-usability-qa/seated-tuner-first-seated-tuner-result.json`, matching
`seated-tuner-first-orchestra-*.log`, and `build/studio-seated-tuner-lua.log`.
No native API/protocol change, public workflow or VPS activation is required.

The usual port-25567 server was stopped cleanly with no test clients running.
All 44 package files were copied and hash-verified; all 65 protected database,
profile, settings, key and configuration files matched before/after installation.
The local server restarted successfully with zero players. Native checks used
separate private profiles/database and left no QA driver in the installed package.

## October 28: compact seating, sheet music and verified restaging (package 41)

Replaced the blue chair with TwoPixels Studio's CC BY piano stool, fitted to a
44-unit seat height. cScott's CC BY sheet booklet is joined into both Yamaha
rests and the separate music stand. Credits include the upstream notebook by
Neo_minigan. Each holder has exactly one 212-triangle booklet subtree; the
standing Yamaha retains its original 84 collision triangles. Keyboard NPCs have
no separate stand; non-keyboard NPCs have a stand turned toward their position.

Found the native restaging failure: the Lua binding returned success after
enqueueing a teleport, but MPServer::teleportActor accepted only vanilla actor
identities and rejected script-owned performers. Spawned identities now work;
an optional trusted expected-owner argument permits pinned script-owned moves
only for that owner, preserving the lease and advancing its generation. Existing
three-argument admin calls retain the script-owner restriction. New observer
identity packets retain the spawned flag. Setup waits for actual position/cell
readback, retries a missing move once and reports failure before playback.

All 28 Lua suites pass, including a native-queue mock that returns true without
moving the actor, retry/error handling and recovery with the same identity.
The RelWithDebInfo server build succeeds. Actual NIF/DDS/credit/hash validation
passes for 90 bundle files and ten model sources. Native two-client checks on
private port 25571 inspect stool and page fit from side/performer perspectives,
verify a late observer, and change nine performers to two (lute/trumpet), then
three. Actual actor positions match their new slots, remain separated by over
100 units and have distinct IDs. Standing/seated restage and full cleanup pass.
Evidence: `build/studio-usability-qa/multiview-result.json`, the matching A/B/server
logs, `build/studio-staging-followup-lua.log`, and
`build/studio-stage-followup-asset-validation.log`.

Two independent local servers were also found sharing the fixture/database on
IPv4 and IPv6 port 25567. Both were stopped cleanly before installation; use the
guarded fixture launcher so an existing listener is detected across families.
The confirmed native teleport bug is distinct from this local setup issue.
No VPS servers or public releases are changed by these tests.

Installed package 41, the new native server and assets in the original 25567
fixture. All 65 protected user files were unchanged during installation
(`build/studio-stage-followup-local-install.json`). Four obsolete managed blue
chair files were removed only after matching their previous manifest hashes.
One guarded server was restarted hidden; no interactive client was reopened.

## October 28: credited models and seated NPC stage (package 40 / API 17)

All 28 Lua suites pass. Coverage includes stage layout document validation,
managed routing, seated rig eligibility/fallback, measured floors/slopes,
headroom/walls and bounded preflight work (at most eight rays per frame and
224 rays for 32 performers). Server checks reject stale anchors, excessive
heights and extra/non-array fields; yaw comparison permits equivalent wrapped
angles. A request-boundary regression verifies the ground table reaches stage
validation rather than silently timing out.

Nine downloadable CC BY 4.0 sources replace the generated orchestral props and
add chair/music-stand furniture. Actual NIF/DDS validation checks manifest hashes,
relative texture resolution, finite vertices, mipmaps and source credits. The
violin/bow split retains all 14,614 source triangles exactly once. The seated
Yamaha keeps its 12,189 visual triangles and omits the standing collision boxes.
Sources, license links, hashes and adaptations are recorded in the asset bundle.

Private native tests on port 25571 use separate profiles/database and matching
API-17 builds. Front/side/overhead views inspected replacement violin, cello,
trumpet, flute, clarinet and timpani, plus the retained keyboard/lute/drum.
Seated keyboard/cello pelvis displacement measured -32 units; foot height
differences were below .004 units, with approximately 27 units of forward reach.
The late observer received seated profiles, arms and helper attachments.
Standing restage removed seated props, seated restage restored them, and explicit
cleanup removed all nine managed NPCs and their performer poses.

Native testing caught the rejected floor payload and a matrix supplied to the
quaternion-only bone API; both are fixed. Seated palm construction includes the
pelvis shift, and the timpani grip is raised so wooden sticks clear the new head.
Final source-package native rerun and Lua regression run passed. Evidence:
`build/studio-usability-qa/multiview-result.json`, `seated-multiview-firstpass.json`,
the matching A/B/server logs, `build/studio-cc-asset-validation.log` and
`build/synth-automation-lua-tests.json`.

Limits: this is visual NPC seating with a standing physics capsule, not usable
furniture collision. Custom rigs may fall back to standing or need calibration.
The wooden percussion stick is not a felt mallet. Final artistic grip tuning,
other body proportions, player seating and 32-performer long-session/GPU
profiling remain follow-ups. These local tests do not publish client/content
releases or change VPS servers.

Package 40 and its assets were installed in the original port-25567 fixture.
All 67 protected user save/settings/config/key/database files were unchanged
during the copy (`build/studio-seated-local-install.json`). No QA driver was
installed. The guarded server runs hidden; no interactive client was relaunched
after the inspection, leaving the desktop available to the user.

## October 28: local band monitoring and room acoustics (package 39 / API 17)

Added gentler instrument distance falloff, an in-place Balanced monitor scoped
to one selected band, and a shared instrument-only EFX environment return.
Room dimensions/enclosure/shape come from bounded static collision probes;
surface absorption is a local control. Direct obstruction affects world outputs,
while Balanced bypasses it. No score/network format or protocol bump is needed.

All 27 Lua suites pass, including selected/accepted/observer band scoping,
preference validation/persistence, direct obstruction and dry bypass, closed/open
geometry, and both per-frame and elapsed-time ray budgets. Standalone C++ DSP and
instrument-acoustics tests pass (2/2): room size/material response, finite bounds,
and the shared soft audible radius. Native RelWithDebInfo client build succeeds.

Private two-client checks use port 25571, separate profiles/database and the
unmodified source package plus an isolated test driver. Nine instrument profiles
play together; Balanced/World switches preserve unit counts and advance the same
song clock, Dry bypasses the room, and the late observer remains positional.
The native probe estimated the test room at 34.15 x 29.04 x 14.47 metres, enclosure
1.0 and decay 1.73 seconds, with EFX reflections available. Controls were inspected
in the native client and Balanced/band selection exercised with the mouse.
Evidence: `build/studio-usability-qa/listening-result.json`, matching listening
A/B/server logs and `build/synth-automation-lua-tests.json`.

This is a first-pass acoustic approximation and centered mono monitor. Tests do
not establish perceptual quality on every playback device, exact wall materials,
connected-room propagation or a long-session 32-performer performance baseline.
The shared room return avoids per-performer reverbs; probes are limited to at
most 60 rays/second outside the audio callback. Saved musical effects remain
independent. Public servers/releases are not updated by this local validation.

Package 39 and the API-17 native executable were installed in the original
port-25567 fixture. Installation verified all 65 protected save/settings/config/
key files unchanged; the private QA script is absent from that package.
Evidence: `build/studio-listening-local-install.json`. The guarded server and
Synth A were restarted for manual acceptance.

## October 28: approved live calibration defaults (package 38)

Captured seven approved profiles from the current/rotated Synth A client logs:
cello and trumpet from the earlier session, then violin, lute, flute, clarinet
and the final drum export. Duplicate clarinet exports were identical; the last
drum export supersedes the preceding one. Original blocks and log snapshots are
retained outside the repository under `synth-assets/pose-calibration/2026-10-28`.
`client/pose-defaults.lua` contains the numeric data; zero-only exploratory bone
fields were removed. Keyboard/timpani keep their original profiles.

Source defaults and the tuner now use one compiler against the immutable
package-37 reference geometry. Stored/exported positions and angles replace
values in that frame rather than multiplying the new default's angles again.
Reset/Preview Off use the accepted poses; individual saved edits still override
their corresponding fields. New exports declare the calibration and reference
versions explicitly. Native API/protocol and assets are unchanged.

All 26 Lua suites pass, including 370 profile assertions. Each accepted field
is validated, package-37 full exports reproduce the new default transforms
without duplicate prop/palm rotations, and saving a matching accepted profile
produces no redundant overrides. Native checks cover default target equivalence,
storage round trip, bones, export, uninterrupted playback and a late observer.
Evidence: `build/studio-usability-qa/accepted-defaults-result.json` and the matching
A/B/server logs. Package 38 was copied to the existing 25567 fixture with all
65 tracked user save/settings/config/key files unchanged during installation
(`build/studio-defaults-local-install.json`). Public servers/releases were not
updated and no native build or workflow was required.


## October 27 follow-up: live pose calibration GUI (package 37)

Band > Pose tuner and F9 > Local pose tuner open a compact draggable window
without stopping the native score. It covers instrument/attachment transforms,
hand targets and palm orientation, elbow poles, finger curl/motion multipliers,
36 arm/finger bone overrides, focused performer camera views and export. Numeric
entries update live when valid; malformed/out-of-range input cannot corrupt the
profile. Only changed values are persisted, leaving future defaults available
for untouched instruments/controls. Calibration remains local to each client.

All 26 Lua suites pass. The 64 profile assertions include cache invalidation,
source-default isolation, finite/bounded fields, known-node restrictions,
hand/motion transforms, sparse persistence, reload/reset, export and the placed
keyboard reference frame. The client harness exercises real numeric/arrow
callbacks, invalid text, Escape cleanup, no network writes and first-person/
placed-keyboard camera target discovery before an arm pose exists.

Native UI inspection on a private 25571 fixture checked Prop, hand, bone, Camera
and Export pages. Dragging a slider visibly moved the lute while notes continued;
typed numeric input updated the mesh, Reset restored it, a side preset orbited
the performer and closing restored the preceding camera. Save and export to the
client log were exercised. Two clients verified late performer/prop delivery.
The initial visual run was stopped by physical Escape; private processes were
cleaned up, and testing resumed after the user's instruction to proceed.

A final native round trip caught storage returning read-only userdata rather
than a Lua table. Reload now uses StorageSection:getCopy before validation;
regression coverage enforces that engine storage path. Native checks then
recreated the calibration service from saved target/bone values, exported one
instrument, restored defaults and opened the tuner while the score kept playing.
Evidence: `build/studio-usability-qa/pose-tuner-result.json` and matching pose-tuner
A/B/server logs. The matching source package was installed on the usual 25567
fixture with all 65 tracked user data/settings/key/config files unchanged during
copying (`build/studio-pose-tuner-local-install.json`). Two existing fixture
server processes, bound separately on IPv4/IPv6, were shut down gracefully under
the user's restart approval. One server was restarted through the port guard.
Public servers, native executables/API/protocol and release assets are unchanged
by this Lua-only slice; no workflow was started.


## October 27 follow-up: upright trumpet and corrected support hand (package 36)

The previous visual pass missed two independent mesh errors reported by the
user: folded tubing in the horizontal X/Y plane and a valve row running across
the horn. Tubing now occupies Y/Z, the three valves run longitudinally along Y,
and their pistons remain upright along Z. Export validation checks actual NIF
vertices/transforms for all three axes. The trumpet icon/report and full asset
manifest were regenerated; the generator can now rebuild only one instrument.

Hand targets and palm frames follow that layout: the left palm faces inward
toward the casings, fingers wrapping upward, and the right hand crosses over
the buttons with a lighter valve press. The first updated grip exceeded the
right arm's reach by four units; moving the valve block closer and correcting
the wrist/finger offset removed the clamp. The final native run reports zero
reach clamp error. The user's follow-up caught an upward-facing support palm;
this was corrected before final verification.

Native checks covered front, both sides, rear, overhead and two close oblique
views, plus the stopped holding pose. The clean package used no tuning wrappers.
A late second client received all nine performers/props. Evidence is retained
in `build/studio-usability-qa/trumpet-result.json` and trumpet A/B/server logs.
All 26 Lua suites pass, including 43 instrument-profile assertions; all 43 asset
manifest entries pass size/hash/format validation. As with the preceding slice,
this checks the standard humanoid test rig, not every custom body mesh.

The production package/assets were installed into the established 25567 fixture
with all 65 tracked user data/settings/config/key files unchanged during copying.
No private QA code was installed. See `build/studio-trumpet-local-install.json`.
Public servers, release assets and workflows remain unchanged.


## October 27 follow-up: five-view grip and bowed mesh refinement (package 35)

All nine physical instruments were inspected in the native client from front,
left, right, rear and overhead while a nine-part score played. The final run
used the saved production profiles without temporary tuning wrappers. A second
client joined late and received all nine performers and props. Evidence:
`build/studio-usability-qa/multiview-result.json` (`ok` and `cleanPackage` true),
matching A/B/server logs and `build/studio-multiview-coverage.json` (45 views).

Lute, violin, cello, trumpet and flute wrist/palm targets were adjusted against
those views; hand-drum strike travel now leaves room for palm thickness. The
flute's cross-body wrist is in front of the face. The cello is raised with a
longer floor-reaching endpin so its standing bow target is reachable. Finger
flexion derives axes from available unposed rig transforms, with a cached
fallback for listed nodes lacking transforms. Fargoth exposes twelve computed
axes in this test; thirty finger overrides are still applied. A separate reach
diagnostic reports requested targets beyond the IK reach clamp.

Violin and cello now have solid tapered neck/heel and convex fingerboard meshes,
a shorter pegbox/scroll, nut and bridge-aligned strings. Their serialized NIFs
report a zero fingerboard-to-neck gap. All 43 asset manifest entries pass size,
SHA-256 and format checks. All 26 Lua suites pass, including 40 profile and 21
pose assertions. Native code/API and protocol are unchanged by this slice.

These are stylized standing poses checked on the standard humanoid test rig,
not acceptance of every custom race/body rig or seated cello animation. The
private camera-control harness initially raced a partial file write and used
an independent require cache; atomic control-file replacement and wrapping the
actual performer module fixed the harness before the clean final inspection.

The matching production package, server modules and assets were installed into
the existing port-25567 fixture with all 65 tracked database/profile/settings/key/
config files unchanged during copying. No private QA scripts were installed.
Installation evidence: `build/studio-multiview-local-install.json`. Public
servers, release assets and workflows were not changed by this local test.


## October 27 follow-up: managed bands and participating leader (package 34)

Automatic planning now groups ordinary drum lanes onto one drummer and identical
melodic timbres onto a shared physical performer, preserving musical tracks and
mix settings. Leader plays reserves a group for the placed keyboard. Auto manage
persists in the project, stages a changed song before Play, trims surplus/unbound
owned hires, and removes managed NPCs on Finish/session release. Remove owned
NPCs handles explicit cleanup. Stop retains the current lineup. Cleanup checks
registered actor identity and preserves other active bands' claims; late native
spawn completions after cancellation cannot leave unregistered performers.

The existing local test was found to have two server processes listening on
25567 against the same database. Both were stopped with the user's restart
authorization. The fixture launcher now checks the UDP port and uses a named
launch lock. The saved performer roster contained 24 generated identities; no
database rows were manually removed for testing.

Lua regressions cover grouped routes, leader/conductor options, 32 distinct
timbres, surplus removal, busy-band protection, pending manual/managed spawn
cancellation, clear, keyboard alignment, automatic Play/resume, Stop cancellation
and persisted settings. The native harness seeds five old hires, assigns fourteen
parts (ten drum lanes) to the leader and three NPCs, then checks a late observer,
song changes, adding a needed performer, explicit removal and Finish cleanup.
Leader animation reports six arm and thirty finger overrides, native note strikes
and four audio outputs. The leader's keybed alignment was inspected in game.
All 26 Lua suites pass. Native visual inspection also checked the refined
fiddle/clarinet grips and the Band controls. Studio windows now render above
the HUD hotbar so it cannot cover the new controls on smaller displays.

The fiddle wrist now supports the neck from below; the bow frog is held at the
finger contact point instead of the wrist. Clarinet grips cross the upper and
lower key stacks from opposite sides, with lighter finger presses. These use the
generated meshes' measured grip points; custom race/rig variation remains a limit.

The first harness edited stale track objects and raced a final manual-hire reply;
it was corrected to finish seeding before setup and assign props before note
transactions. An empty solo test needed a note before playback. A subsequent
rapid lineup change exposed the old five-second stage request budget silently
dropping valid requests; the bounded burst allowance and immediate rejection
response now prevent a fifty-second UI wait. Native evidence is retained in
`build/studio-usability-qa/managed-band-result.json` and matching logs/harnesses.

The matching package/server modules were copied back to the established 25567
fixture with all 64 tracked database/profile/settings/key/config files unchanged
during copying. The launcher port guard was checked with the port free and with
the updated server running; the duplicate attempt returned without creating a
second process. Installation evidence: `build/studio-managed-band-local-install.json`.

## October 27: standing stage and original orchestral props (package 33)

All 26 procedural-synth Lua suites pass, including formation/authority/reuse,
failure recovery, stale replies, one-step routing Undo, 32-member paging,
GM-aware instrument mapping, patch/model independence and measured prop targets.
The shared project/schema/layout rules match their server copies. Matching
RelWithDebInfo client/server/component targets build successfully; animation
controller component checks pass. The first rebuild hit a locked QA server
executable; it succeeded after the disposable QA processes exited.

An isolated UDP-25571 server and two real clients used separate database,
profiles, packages and keys. Auto NPC band created 32 performers covering all
nine physical profiles. The observer joined after setup and received all 32
routed outputs and props. Every performer had six arm overrides, with 384 finger
bones across the ensemble. Bows and paired mallets were present. XY drift stayed
below two units over the stationary interval (zero in the observed run).
Repeating Auto reused every actor identity; routing Undo/Redo and stopped holding
poses passed. Evidence: `build/studio-usability-qa/orchestra-result.json` and
the matching orchestra A/B/server logs and private harnesses.

Native visual inspection covered the keyboard/stand, lute, drum, violin/bow,
cello/bow, trumpet, flute, clarinet and timpani/mallets. The user identified the
backwards lute: measured vanilla mesh +Y neck / +Z string face and OpenMW's
clockwise named rotation helpers now determine its transform. Fretting and
strumming align with those surfaces. At the user's request, keyboard performers
use the floor-anchored Yamaha/X-stand rather than the small Casio. Wind grips
were lowered after the first visual pass put their mouthpieces too high.
Generated diffuse DDS textures corrected the original props' pale material-only
appearance. Mesh updates reuse instantiated native attachments each pose frame.

The eight original low-poly NIFs/icons and generated material textures have a
CC BY 4.0 notice alongside the retained Sketchfab credits. Generation validates
serialized triangle counts/texture paths; bundle lengths/SHA-256 and NIF/DDS
headers are checked separately. Inventory icons are source renders; the native
visual pass verifies exported model appearance.

Limits: clear level ground is required; ToddTest is too cramped for the rear
rows of a full 32-member stage. Floor/obstruction preview, chairs/music stands,
seated cello and further instrument families remain planned. These grip targets
were inspected on the generated humanoid performers, not every custom body rig.
Six new presets are editable synthetic approximations. This was not a human
listening or long-session latency acceptance test; the 32-output visual test
does not establish a universal real-time voice budget. No public workflow,
release or VPS activation is included in this slice.

The idle established port-25567 fixture received the matching binaries, package
33, server modules and complete hash-verified asset bundle. All 62 tracked
database/profile/settings/key/config files matched before and after copying.
The local server was restarted on the same port; no disposable QA script or
fixture replaced its saved state. Evidence: `build/studio-orchestra-local-install.json`.

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


## 2026-10-18 - program-aware defaults and whole-song Sound sets

API 8 / package 10. Seven Sound sets apply role-based patches to all Studio
tracks: Balanced, Rock, Metal, Electronic, Lo-fi / Jazz, Chiptune and Ambient.
Six additional starter patches bring the built-in catalog to 51. No production
deployment was performed.

- Full RelWithDebInfo client build and standalone native CTest pass. A regression
  first reproduced a silent voice retained after zero-sustain smoothing and
  release. Flooring inaudible release values fixes Stop after sound-set changes.
- Server Lua: 18 tests / 243 assertions. Client/editor/UI: 5838 assertions.
  Sound sets: 273 assertions covering every GM program, role/set validation,
  MIDI bass octave compensation, tom tuning, custom overrides, one-step undo,
  transport continuity, mix preservation and saved-project migration.
- Workstation, native sequence (21), transport, MIDI (28) and arm geometry (16)
  suites pass. A full 457-file local MIDI catalog comparison preserves exact
  event ordering, timestamps and duration for all 447 previously accepted files;
  both readers reject the same 10 files. All 4569 parsed parts resolve valid
  patches in every set. Programs on separate setup tracks and later program
  changes are covered; fixed part assignment uses the first note's program.
- Live isolated port 25567: A played six parts, cycled every set without stopping
  or recompiling the score, retained track gain, then saved/reloaded the set and
  patch snapshots through character SQLite storage. Stop released all voices.
  B received Rock/Metal/clean guitar changes during playback and passed Stop.
- Mixer layout inspected in the running client at existing 196% GUI scale.
  Sound-set arrows/dropdown and all eight track rows fit.

Private evidence: fixture evidence/soundsets-milestone, including the original
playerdata.db SQLite backup and final A/B logs. The user began testing another
MIDI after the probe, so the current session and database were retained rather
than overwriting those edits. A second SQLite snapshot preserves that state.
The optional probe is disabled for the next server launch. All 436 custom MIDI
copies match their originals. Settings, keyboard placement and assets remain.

These are procedural approximations, not a General MIDI sample library. Unknown
percussion currently falls back to snare; per-event program changes, realistic
cymbal/sample coverage and instrument-specific synthesis remain future work.


## 2026-10-18 - repeated sound changes and gradual-underflow slowdown

User reported playback dropping from roughly 240 to 70 FPS after trying many
songs/sets, recovering after Finish/reopen. Live A/B logs were preserved before
restarting; they contain no synth exceptions or transport failures, but do not
contain profiling data for the reported slowdown.

A native 16-track reproduction isolated a matching CPU-growth mechanism even
without notes: oscillator weights and zero-valued parameter smoothers decayed
into subnormal floats. With gradual underflow, fresh render cost was 0.062 s per
second of PCM versus 0.242 s after cycling. Enabling CPU flush-to-zero in the
private diagnostic returned the same cycled engine to 0.062 s. This identifies
a reproducible cause consistent with Finish destroying/recreating the rack.

- Portable per-state silence floors now clear recursive values below 1e-20; no
  process-wide floating-point mode is changed. Controls/wave weights, insert
  blend/mode/cabinet crossfades, EQ input history, pitch/drum decay envelopes and
  peak-meter tails are covered. The floor is far below audible signal levels.
- Same diagnostic after the fix: fresh 0.0668 s, cycled 0.0669 s, cycled with
  CPU flush-to-zero 0.0668 s per second of PCM. Timings are machine-dependent.
- Added deterministic float/double convergence checks and a 16-track soak with
  twelve waveform/drive/cabinet changes followed by 35 seconds of idle PCM.
  Rack allocation remains bounded; voices and long-idle meters reach zero.
  The old native sources fail the new long-idle meter assertion.
- Full standalone native suite passes, including guitar alias suppression,
  synth/drum envelopes, mixer/returns, exact PCM across render block sizes,
  scheduling/Stop/replacement, validation, overflow and concurrent controls.
  Soak render cost: 0.0712 s fresh / 0.0646 s cycled per second of PCM.
- Full RelWithDebInfo OpenMW client build passes. After the user closed the
  client, the isolated test executable was updated, SHA-256 checked and Synth A
  relaunched on port 25567. The optional probe stays disabled. No production
  deployment, project remapping or preset edits were performed.

Private evidence: fixture evidence/soundset-performance, including pre-update
SQLite backup, live logs, native test output and diagnostic timings. This fixes
a reproduced DSP slowdown; a long interactive song-cycling session is still the
check for the user's exact frame-rate/audio symptom and any additional causes.


## 2026-10-19 - portable keyboard and gameplay/sampling roadmap

Package 11 keeps native synth API 8 and the existing project schema. This slice
adds Pick up to both windows, an actual miscellaneous inventory record with a
rendered keyboard icon, placement and reactivation. Dirty projects save before
pickup; an untouched editor preserves the character's existing saved song.

- Full RelWithDebInfo server build passes. Native world-item/content-registry
  suites: 20 tests pass, including activator -> portable item atomic persistence,
  tombstone/replay protection, competing character rejection and restart.
- Server Lua: 21 tests / 257 assertions. Client/editor/UI: 6337 assertions,
  including shared activation-chain suppression of the default ActionTake,
  dirty-save ordering, fresh-save preservation, failure recovery and cleanup.
- Workstation, mixer/returns, transport, sound-set (273), MIDI (28), pose (16)
  and sequence (21) suites pass. Python asset helpers compile; bundle hashes
  verify. The icon was visually checked after rendering from the installed NIF.
- Live isolated A/B test on port 25567: native clips played to the observer;
  legacy stage pickup produced exactly one inventory keyboard with its icon;
  placement removed the inventory copy; activation reopened Studio; Load
  recovered the saved note; a second pickup stopped and removed both physical
  playback sources on the observer without duplication.
- Live testing exposed and fixed missing miscellaneous records in the headless
  server item store, and normal ActionTake consuming newly placed keyboards.
  Native completion drives UI success; client-claimed completion is rejected.
- After an orderly server restart, the portable item remained in inventory and
  the test stage did not respawn a duplicate. A normal client reconnected with
  the persistent item record available. The fixture's auto-spawn suppression
  uses global Lua storage, which is saved on orderly shutdown.

Private evidence: fixture evidence/portable-keyboard, including original full
SQLite/global-storage backups, successful A/B logs and post-restart snapshot.
The original database and global storage were restored byte-for-byte before
normal launch; original saved projects, inventory and stage objects were then
compared. The optional probe is disabled. Production was not restarted.

Roadmap now targets one saved MIDI arrangement for authoring and Bardcraft-style
player/NPC performance, multiple keyboards and vanilla lute/drum test members.
Future sampling covers installed voice lines, SFX and music. Its reference is
the server /s and /speech command system and speech catalogs. Band attachments,
output routing, full MIDI editing and native sample voices are not added here.


## 2026-10-19 - placed-keyboard band outputs

Package 12 requires native synth API 9. Version-1 projects gain optional band
slots/output slot fields; old songs migrate to the original home keyboard.

- Full RelWithDebInfo client/server builds pass. Standalone native CTest passes,
  including the existing DSP soak, mixer/returns, block-independent scheduling
  and new empty-score looping/finite completion/validation checks. Empty outputs
  retain a native song clock without allocating duplicate instrument voices.
- Server Lua: 25 tests / 274 assertions. Covers exclusive reservations, owner
  validation, late snapshots, missing/duplicate/busy/distant destinations,
  character persistence/migration and structural-stop versus live mix updates.
- Client/UI: 6452 assertions including Band navigation, eight member-name fields,
  output selectors and logical-window bounds. Workstation tests cover output
  undo/redo, persistent member identity and independent mix/sound assignments.
- New band transport harness: 35 assertions covering exact disjoint score bytes,
  physical source identity, shared future starts/replacement, member mute/gain,
  missing/rejoin phase, all-remote songs, 24 repeated reroutes and full cleanup.
  Existing transport, sound sets (273), sequence (21) and MIDI (28) suites pass.
- Live isolated A/B: two physical keyboard streams played different parts.
  Sampled audible phase difference was zero on owner and observer in this run;
  this is a local observation, not a cross-device clock-lock guarantee.
  Member mute reached native silence while transport continued. Character SQLite
  load retained member identity, routing and saved mix. A late-started observer
  joined both streams of an already-running arrangement. Removing the bound keyboard silenced only that member;
  home playback continued. Finish removed every band stream on the observer.
- First live run found score Begin could precede a pending structural band
  update, which correctly cancelled its upload on the server. Begin now waits
  behind band/channel settings; the regression and complete second live run pass.
- Original fixture projects, inventory/equipment and global storage restored.
  Normal setup disables both the player probe and QA-only server removal handler.
  One additional persistent keyboard is intentionally retained on the isolated
  stage for manual Band testing. The helper backs up and is idempotent.

Private evidence: fixture evidence/band-routing contains the original SQLite and
global-storage backups, A/B logs, server output and successful project snapshot.
Existing custom MIDI and model/icon assets are preserved. No production restart
or deployment. Player/NPC held instruments, stereo/group buses and continuous
output/device drift correction remain future work. Each output currently has
its own native delay/reverb state with common settings; OUTPUT MAX meters show
the loudest output rather than a summed master bus.


## Player/NPC performers - 2026-10-19 (package 13, native synth API 10)

Built openmw and openmw-server RelWithDebInfo successfully. Server release-file
classification includes procedural_synth_performers.lua. Existing DSP scheduling
was reused; no new DSP algorithm was introduced in this slice.

Regression checks:
- Server synth: 26 tests / 278 assertions, including authenticated non-leader
  consent commands and a strict numeric native object lookup stub. This catches
  the live-discovered eager getObjectByMpNum(nil) bug for player destinations.
- Performer manager: 27 assertions covering native sender identity, inventory,
  accept/decline, expiry, epoch replay, leave/death/reconnect, ownership, native
  spawn completion, dismiss, monotonic NPC identity and saved target validation.
- Client/editor: 6459 assertions, including explicit F9 panel open/close,
  non-modal invitations, readiness, decline and leave.
- Band transport: 37 assertions; workstation: 82 plus mixer/return/band cases;
  arm IK: 16 assertions; native-clock/chunk/late-seek transport harness passed.
- git diff --check passed. Native C++ client/server builds passed.

Live integration used the existing isolated Synth A/B fixture on port 25567.
The earlier disposable 25568 server was stopped; it was not used for final QA.
Original fixture evidence/actor-performers contains live2/live3 and restart logs,
qa-completed.db, and baseline-20261019-220314 with the user's latest pre-QA DB
and profiles. Tests temporarily wrote disposable projects; that latest baseline
was restored byte-for-byte before normal startup. Shared and profile settings
were preserved, and normal setup now creates shared defaults only when absent.

Both clients reported PASS for explicit decline, fresh accept, native player
lute and owned-NPC drum emitters, six arm bones and one held prop per performer,
real-time native prop expiry, holster without interrupting other parts,
current-phase rejoin, NPC keyboard, Leave revocation, NPC removal without source
fallback, and Finish cleanup. A subsequent graceful server restart resolved a
new saved owned NPC and its saved band target. Consent remains transient.
The observer's initial QA diagnostic could not see the runtime performers map;
status() now exposes that map and the full probe passed after correction.

A window inspection exposed a first-person self lute clipping through the
camera; self props/poses now hide in first person while sound continues and
other observers still render the performer. The probe explicitly selects third
person for its self-attachment checks. UI automation was stopped when the user
pressed physical Escape; no further UI actions were performed. Detailed artistic
alignment of every race/outfit/instrument remains a manual calibration task.

Normal fixture startup disables the automated probe and its QA-only removal
handler. PerformerStage supplies vanilla lute/drum only to the isolated test
characters; this is not a public-server item grant. Existing custom MIDI files
remain installed. No production restart, push or deployment was performed.

Limits: NPC props are stage presentation, not lootable equipment. NPCs keep
ordinary AI; followers/stage positioning, combat behavior and finger animations
are not implemented. Native prop limits/expiry are bounded; unavailable actor
parts remain silent. Stereo/group buses and continuous drift correction remain
planned. Created NPCs are opt-in, character-owned persistent world actors.


## Imported MIDI note editing - 2026-10-19 (package 14 / native synth API 10)

This slice changes Lua project/editor/score compilation; it reuses the existing
native synth, spatial Studio streams, audio clock and score replacement API.
No C++ or DSP algorithm change/rebuild was needed. Server/client project and
score-format mirrors are byte-identical. Optional project fields remain schema
version 1; old projects migrate without conversion or truncation.

Regression checks passed:
- Server: 27 tests / 285 assertions, including private edited-note/tempo storage,
  explicitly empty overrides and refusal to overwrite with malformed binary.
- New MIDI editing: 40 assertions covering source PPQ/sustain gates, source score
  byte parity on conversion, notes beyond bar 16, sound/mix/band preservation,
  group edits, atomic refusal, clipboard, undo/redo, empty overrides, extension,
  source-free reload, GM role retention and compact note/tempo limits.
- Same-tick gates preserve native one-sample impulses. Notes at the MIDI end
  marker stay excluded. Unclosed source notes retain their source end when
  another edited part extends the arrangement.
- Client/editor: 6844 assertions, including real Studio callback drag preview,
  one release/undo action, resize at enlarged GUI scale, duplicate/delete keys,
  zoom and view navigation without a project edit.
- Existing workstation: 82 assertions plus mixer/returns/band checks; score: 21;
  MIDI parser/player: 28; sound sets: 273; band: 37; performers: 27; arm IK: 16;
  native-clock/chunk/late-seek transport harness passed. git diff --check passed.

The existing isolated port-25567 Synth A/B fixture supplied the live check.
The probe parses a deterministic two-part SMF with off-grid onset, tempo change
and notes after bar 16; converts both parts, changes notes and plays through
independent home and consenting player-lute native outputs. It exercises live
replacement, Stop, character SQLite save/load without the original MIDI,
retained sound/routing/tempo, then an 8,192-note real network/SQLite round trip.
Owner and observer both reported PASS, including Finish cleanup. Evidence is in
fixture evidence/midi-editing/live1, live2 and live3 logs and qa-completed.db.

Computer-use inspection of the enlarged (136%) in-game Piano page verified note
selection, a snapped time/pitch move, right-edge gate resize, selected velocity
refresh, continuous timeline scrollbar movement and Save. Fullscreen gameplay
key injection initially did not respond reliably; the controlled probe opened
the page and the actual GUI mouse gestures worked. The subsequent native band
playback/save/load probe also passed with those manual GUI edits included.

Normal startup installs package 14 with the automated probe disabled. The user's
latest pre-QA database (baseline-20261019-232457), profiles and shared settings
were backed up and restored. Database hash, profile settings and input bindings
were checked after restoration; custom MIDI files remain installed. Only the
isolated fixture was restarted; no production action or push was performed.

Limits: 4,096 converted notes per part / 8,192 per project, 1,024 tempo changes,
30 minutes of song audio. Refused conversion preserves source data. Display is
bounded to 512 note widgets per view (zoom in for denser passages). Clip limits
remain separate. Sustain is baked into effective gates; editable controller
lanes, tempo editing, recording, arrangement clips and MIDI export remain planned.
Sampling/game-audio browsing and voice-line instruments are not added here.


## Quantized recording / selected-song fix - 2026-10-20

Package 15 requires native synth API 11. Client and server RelWithDebInfo builds
passed. Native CTest passed, including new same-pitch live/score release tests.
Large Studio test fixtures now allocate on the heap to avoid Windows test-stack
exhaustion; runtime Studio instances already use heap allocation.

The actual Studio/Band callback regression reproduces A -> select B -> Play,
arrow cycling back, and cancellation of a yielding parse. The selected file now
wins, while replaying the same arrangement preserves its edits. Coroutine loads
capture path/intent/session instead of consulting mutable selection on completion.
Client/UI harness: 7424 assertions; recording model: 24 assertions covering
count-in, chords, repeat suppression, quantization, native clock, loop boundary,
Keep/Cancel/Stop, history, unavailable targets, retry recovery and saved tempo.
Existing server, editing, workstation, sequence, MIDI, sound-set, performer,
band and transport suites also passed. git diff --check passed.

Existing isolated port-25567 Synth A/B live checks reported PASS for new-song
playback from Band without Finish, a count-in chord monitored at the consenting
player lute, whole-take undo/redo, native-clock overdub, Cancel preservation,
SQLite notes/routing round trip and Finish cleanup on the observer. The probe
was updated to accept the user's placed portable keyboard rather than assuming
the old initial activator still existed. No stage reset was required.
Evidence is fixture evidence/recording/live2*.log and qa-completed.db. Only that
fixture was restarted; production and the unused 25568 fixture were untouched.

The latest pre-test DB/profiles/userdata were backed up, restored and hash-checked
before normal startup. The automated probe is disabled and package/build hashes
are checked. QA MIDI files are local fixture-only files. Sound monitoring/input
state is transient; saved takes are normal project notes. No push/deployment.

Limits: computer/mouse input, visual count-in, one clip pass, existing note caps;
no hardware MIDI input, audible metronome, latency calibration, arrangement clips,
automation, MIDI export or samples. This slice does not claim a full DAW recorder.


## New Song / full-song authoring - 2026-10-20

Package 16 uses the existing native synth API 11 binary. This slice changes Lua
and project validation, not native C++/DSP code. All thirteen Lua suites passed:
server 29 tests / 295 assertions; client/UI 8587 assertions; composition 86;
recording 24; editing 40; workstation 82 plus mixer/returns/band checks; sequence
21; MIDI 29; sound sets 273; physical band 37; performers 27; pose 16; transport
passed. git diff --check passed. Client/server project and sequence mirrors match.

Composition tests cover blank source-free authoring, all 48 meter choices,
constant-tempo clock updates, shortening/extension without deleting later notes,
region clipping/rebasing, sustain at region boundaries, loop-wrap overdub, coarse
snap within a tiny region, input events arriving before a wrap frame, document
undo/redo, save/load, validation and legacy clip behavior. Actual Studio callbacks
cover New Song/Create, cancellation of a yielding MIDI parse, authored-song Play,
full-song +Piano, Song-page bounds, Save, load clearing stale selection, and
explicitly selecting a file again. Server tests check private reload, malformed
metadata preserving saved data, and bounded score quanta (3000-1728000 frames).

Native Synth A/B checks in the established port-25567 fixture reported PASS for:
new 3/4 song, independent snare part, consenting player lute output, selected-bar
loop playback, native-clock loop-wrap recording, preserving later notes through
length changes, SQLite notes/meter/region/routing round trip, source-free replay,
shortest meter (1/16 at 240 BPM), longest bar (12/2 at 40 BPM), and observer cleanup
on Finish. The upload quantum bound previously assumed 4/4; it now covers exactly
the supported meter/tempo extrema without removing score duration/rate limits.
Evidence: fixture evidence/composition/live3-*.log and qa-completed.db. The first
run also passed the main workflow and visual review. A second run waited on an
expired invitation after B was launched late; the probe now retries invitations.

Song and New Song pages plus the meter dropdown/scrollbar were visually checked
in the native game at the user's saved 136% UI scale. The latest pre-QA baseline
(evidence/composition/baseline-20261020-110639) contains the user's DB, profiles,
userdata and global storage; these were restored. Database hash and five saved
settings/input hashes matched before normal startup. Only the isolated fixture
was used. Normal setup disables the disposable probe and installs package 16.
No production action, push, or deployment was performed.

Limits: constant tempo/meter for authored songs, one current project per character,
16 total tracks, 4096 notes per full-song part / 8192 per project, 256 song bars
subject to 30 minutes. Step clips remain independent 16-bar 4/4 overlays. No
arrangement clips, automation, MIDI export, audible metronome, hardware MIDI,
latency calibration, project library/recovery or game-audio sample instruments.


## Linked clip arranger - 2026-10-20

Package 17 uses the existing native API 11 binary; no C++/DSP change or engine
rebuild is required. All fourteen Lua suites passed. New arrangement model: 52
assertions; actual client/UI callbacks: 9698; server: 30 tests / 300 assertions.
Existing composition, recording, editing, workstation, sequence, MIDI, sound-set,
physical-band, performer, pose and transport tests pass. Shared server/client
project and sequence files match. git diff --check passes.

Model coverage includes selective capture without doubled notes, PPQ/leading
silence, linked repetition and transposition, source edits reaching every copy,
partial-repeat gates across a tempo map, placement mute/removal, source deletion
and undo, stopped source recording, isolated source audition, saved routing,
legacy drum swing capture, dangling/malformed references, 256-placement and
8192-saved-note bounds, pitch overflow, empty-repeat fast paths and expanded-note
budget rejection. Source notes and direct linear notes share the same saved cap.
The UI harness checks bar-snapped drag preview/release, one undo action, resizing
at enlarged scale, source gate clamping, linked duplicate, nonmusical scrollbar
navigation, source editor/audition navigation and actual Save references.

The existing isolated port-25567 fixture passed native Synth A/B tests for melodic
and drum arrangements, consenting player-lute and local keyboard outputs, loop
playback, shared-source edits replacing a running score, stopped source recording,
8192 source notes plus linked placements and routing over the native event wire
and SQLite, source-free replay and Finish cleanup on the observer. Evidence is
fixture evidence/arrangement/live-*.log and qa-completed.db. No fixture on 25568,
production change, push or deployment was used.

The native UI was inspected at saved 136% scale. Mouse actions moved a placement
from bar 1 to bar 2, resized another from two bars to three, opened the shared
source editor, auditioned it, changed to a four-bar view and dragged the time
scrollbar from bars 1-4 to 5-8. Controls fit the shared resizable window. Follow-up
UI harness checks cover source end clamping and an offscreen placement end.

The latest pre-QA database/profiles/userdata/global storage were backed up in
fixture evidence/arrangement/baseline-20261020-114023, then restored. Database and
settings/input hashes matched before normal startup. Normal setup installs
package 17 and disables the disposable probe. Existing custom songs remain.
Limits: track-local linked sources, individual bar-snapped placements, at most
eight sources per track/64 placements per track/256 per project/64 quarter notes
per source, existing note and thirty-minute/rate limits. No automation, source
offsets, independent source copies, MIDI export, new sample instruments or
hardware MIDI support is claimed.


## 2026-10-20: installed-game sampler and browser

Package 18 requires native openmw.synth API 12; I.ProceduralSynth is also 12.
Client and server RelWithDebInfo builds passed. All 15 Lua test scripts passed,
including the real MIDI parser fixture, the 9700-assertion client/UI harness,
31 server tests / 310 assertions and 24 dedicated sampler assertions. Native
CTest passed with sample loading/publication, missing/mismatched identity,
root/octave frequency, independent chord cursors, overlapping one-shots, gated
release, panic, end-of-file retirement, control-thread buffer reclamation,
block-size-independent native Studio scheduling and Stop tests. Existing drum,
guitar, mixer, returns, timing, concurrency and sound-change tests also passed.

The established port-25567 Synth A/B fixture passed real native checks:
/s-catalog MP3 voice, loose music excerpt starting at 30 seconds, and an
archive-only WAV generated for QA. Both clients received package 18 afresh and
resolved matching sample identities. Quantitative native voice/peak readings
confirmed sample audio at a consenting player's lute and an owned NPC's drum.
The owner played a pitched chord/repeated linked source, recorded a new note into
that source, saved/loaded it with sample settings and band routes through SQLite,
played imported Greensleeves MIDI through the sample, and closed/reopened Studio.
Forty distinct excerpt loads/switches/local previews completed while the native
band song continued. Finish removed remote outputs. A mismatched WAV and a
missing path stayed silent while the matching player part continued. Both
listeners reported affected-lane status; unrelated parts were not substituted.
Personal sample presets and project references were verified in character 1001's
actual SQLite storage; character 1002 retained its separate preset storage.

Native UI was inspected at the saved 136% scale: 7183 installed sounds, 6447 voice
entries; searching 'hello' matched 3880 catalog-tagged files even when filenames
used 'hlo'. Dragging the selector scrollbar moved to matches 2125-2132. Mouse/UI
editing changed the voice start to 0.1 seconds, applied a 1.1-second excerpt,
switched to Gated, assigned it to the selected part and saved. Trim fields,
metadata, status and all seven Studio tabs fit the shared window. The metadata
catalog packs 9703 paths into 615 shared groups (~179 KB) to avoid a large
bootstrap transfer; fresh two-client downloads passed with this representation.

Latest pre-QA data was backed up in fixture evidence/samples/baseline-20261020-120833.
After evidence collection, database, global storage, profiles and userdata were
restored. All 13 database/settings/input hashes matched before normal startup.
Normal fixture setup installs package 18 and the final executable, disables the
probe and excludes the generated audio archive. Evidence is evidence/samples/
live-A.log, live-B.log, live-server.log and qa-completed.db. No 25568 fixture,
production change, push, deployment or game-audio redistribution was used.

First-pass limits: mono excerpts, linear pitch/speed resampling, 0.01-30 seconds
per excerpt starting at 0-600 seconds, 64 MiB source-file cap, one decoder worker,
ten-second decode deadline, 16 queued loads, 32 cached excerpts / 176 MiB maximum
PCM plus bounded scratch. Existing instance/lane/voice/score budgets still apply.
A late listener joins on subsequent notes after loading. Advanced sample loops,
chopping, crossfades, reverse, time stretch, stereo lanes and multisample zones
remain future work. Broad long-session/device latency measurements remain
separate from the bounded forty-switch check above.


## 2026-10-20: MIDI export, track naming, responsive Studio and drum-bar copy

Package 19 / native synth API 13. Windows RelWithDebInfo openmw and openmw-server
builds passed. All 16 Lua regression scripts passed, including the actual Bardcraft
Greensleeves import/export test, and the native procedural-synth CTest passed.
The client regression covers expanded piano hit testing/dragging at 2560x1440,
ultrawide and small viewports, pitch bounds, geometry restore, UI rename/export
callbacks and dropdown placement. Drum copy covers both lane pages, velocity/rest
replacement, extension, persistence, Undo/Redo and the 16-bar cap. Export covers
tempo/gates, authored meter, whole-song versus loop-range behavior, linked source
transpose/mute, percussion, 48-byte track names and all 16 melodic parts.

Native -ExportProbe ran on the established port-25567 fixture. Synth A exported
linked notes and named parts, read them back through the restricted native API,
saved/reloaded the renamed project, reimported the immediately listed local file
and played it on the native clock. Synth B reported active voices for that score.
Invalid bytes and parent-path reads were rejected. Native drum-copy/Undo/Redo passed.
Sky visual inspection verified full-width Piano, painting/moving a note at the far
right, geometry Restore, and clicking Duplicate to next bar on the actual Drums UI.
This does not claim external-DAW listening or MIDI-controller/meter editing support.

Local evidence: fixture/evidence/export-layout/ contains live-A.log, live-B.log,
live-server.log, qa-completed.db and exported MIDI. The fresh user baseline was
captured after the user closed their session: baseline-20261020-170606. After QA,
its DB, global storage and profiles/userdata were restored with all 13 recorded
SHA-256 checks matching before normal fixture startup. QA exports are retained
only in evidence. Normal package installation disables the probe. Port 25568 and
production binaries/databases were not used.


## 2026-10-20: focused Piano, right-click delete and fullscreen tab regression

Package 20, native synth API unchanged at 13; Lua-only update. All 16 Lua test
scripts passed, including 12,506 client assertions at this revision. Tests cover
native sibling hit order above the centered Mixer body; transient editor expansion
from windowed/fullscreen Studio; piano hit testing at its new origin; single-note
right-click deletion while multiple notes are selected, Undo and UI-mode retention;
Escape through UI and engine paths without double handling, including a native
Interface pop; prior geometry restoration; and UI-control release on Hide/Finish.

In the established port-25567 fixture, Sky mouse checks verified Mixer -> Piano
without Restore, expanded Piano layout, right-click deletion in expanded and
normal views, Undo restoring the deleted note, and Back to Piano restoring the
normal header/layout. Synth B remained connected. No generated QA project was saved.
The injected Escape key produced neither a UI transition nor a probe onKeyPress
log in this SDL client. Live physical Escape is therefore not claimed verified;
UI/engine dispatch and Interface recovery are covered by regression tests. The
preexisting automation limitation also affected other SDL key shortcuts earlier.

Evidence is local in fixture/evidence/piano-focus/: live-A.log, live-B.log,
live-server.log, qa-completed.db and verification.txt. The user's freshly saved
state was backed up to baseline-20261020-195505 before installation. All 13 DB,
profile and settings hashes were restored before normal setup/startup. The probe
was disabled afterward, and only the usual port-25567 fixture was used.


## 2026-10-20: named project library, autosave and recovery

Package 21, native synth API unchanged at 13. Lua-only client/server update; no
engine executable change required. All 17 Lua regression scripts passed, including
real Bardcraft Greensleeves import/export. Library tests cover legacy-copy migration,
character isolation, malformed/future storage preservation, stale revisions, write
failures, 16-project/3-draft bounds, same-draft update ordering, separate autosave,
unsaved action cancellation/continuation, late save/load acknowledgements and request
timeouts. The actual server handler rejects observer/forged-character library
requests and bounds flood responses (32 server tests / 325 assertions). The client
UI harness runs against the real library backend and checks Save As name entry,
explicit delete confirmation/cancel, and unsaved Finish choices (12,712 assertions).
The new isolated library test adds 29 assertions.

-LibraryProbe passed in the usual port-25567 fixture. Synth A saved two distinct
named projects, renamed/deleted a copy, wrote an autosave separately from named
saves, switched through an acknowledged recovery guard, restored a recovery as an
unsaved copy and saved that copy independently. Synth B heard native playback and
could not retrieve the owner's library. SQLite contained library/recovery rows
only for character 1001 (20,871 and 2,847 bytes in this QA document).

Both clients were terminated and the server stopped/restarted. The second pass
reopened the saved recovered project, verified its two notes and found its recovery
snapshot, proving persistence beyond process memory. Sky visual inspection checked
the Projects layout, named selector, actual Open action and Save As naming dialog.
Project IDs use plain text rather than #, which MyGUI treats as color markup.

Evidence is local in fixture/evidence/library/: phase1-A.log, phase1-B.log,
phase1-server.log, phase1.db, live-A.log, live-B.log, live-server.log,
qa-completed.db and verification.txt. The fresh baseline is
baseline-20261020-201609. After QA all 13 recorded database/profile/settings hashes
were restored before normal installation/startup; generated library/recovery data
remains only in evidence. Normal setup disables the probe. No port-25568 fixture
or production state was changed.

Limits: one in-flight client library operation, 16 named entries, three recent
per-document recovery snapshots, 30-second autosave while dirty and connected,
and explicit Keep/Cancel of unfinished takes. Recovery may predate a later named
save. Referenced MIDI/game-audio assets remain external. This does not claim
cross-server project transfer, offline autosave, or collaborative document editing.


## Automation/controller editing and bootstrap backpressure (2026-10-20)

Package 22 / native synth API 14. Windows RelWithDebInfo openmw and openmw-server
build successfully. All 18 Lua test scripts pass, including the new 36-assertion
automation suite, server ownership/late-join relay, exact physical-output curve
partitioning, and actual Studio graph paint/drag/delete/history at GUI scale.
The standalone C++ DSP suite passes. It verifies step/linear curves, exact PCM
across 480/137-frame blocks and repeated loops, positive two-semitone pitch bend,
controller chase, sustain and same-frame pedal-on/gate-off ordering, program
precedence, Stop/manual-patch restoration and malformed control rejection.

The larger package crossed the socket send-buffer budget and initially failed
bootstrap with incomplete source files. The fix pumps at most four packets with a
128 KiB target budget, retries rejected sends, and preserves source/completion/character
list order. A deterministic queue test sends more than the socket-buffer size
with repeated simulated backpressure; both native clients then bootstrap cleanly.
The previously added library module is also included in the server release file
manifest so a clean CMake configure succeeds.

Disposable native probes ran in the existing port-25567 fixture, with the user
clients closed. Synth A exported and parsed bend/program/CC data, played the score,
captured a curve take, exercised Undo/Redo and named saves, then saved/reloaded the
full 8,192-point project over the real Lua wire and SQLite and played its score.
Synth B joined during playback and reported native voices, strikes and fault=0.
A full server/client restart retained curves and the program policy and replayed
on both clients. Native UI checks at windowed 160% / fullscreen 186% verified the
expanded graph, point painting, point dragging, the parameter picker and switching
Mixer -> Auto in fullscreen. Right-click deletion is covered by the actual UI
regression harness; it was not claimed as a live GUI deletion test.

Evidence is retained under the fixture's evidence/automation/first-run and
restart-ui directories. The fresh baseline is baseline-20261020-205434; all 15
saved-data/settings/input/database hashes were restored after the disposable QA.
The ordinary package is installed with the QA probe disabled. Saved user projects
and profiles are preserved. Only the existing local fixture is changed; no push,
production deployment or new port-25568 fixture is involved.

First-pass limits: recording captures Auto slider edits for the next bar-boundary
replacement, rather than monitoring an incoming hardware controller. Controller
import is explicit for compatibility with older baked sustain gates. Program
changes use current sound-set family patches and remain off for sample parts.
Mixer/return automation, external MIDI input, variable bend/RPN maps and richer
recording modes remain roadmap items; README.md documents behavior and bounds.


## Tempo/meter maps and native bar-boundary activation (2026-10-21)

Package 23 / unchanged native synth API 14. All 19 Lua suites pass, including a new
47-assertion timing suite, server ownership/persistence/malformed-grid/finite-end
checks and actual Timing/Piano/Arrange callback tests. Forward/inverse clocks,
partial bars, crop/loop, map-preserving shortening/header edits, PPQ preservation,
Undo/Redo, atomic import rejection, MIDI conductor round trip and local tempo/meter
recording count-in are covered. Server/client project and sequence mirrors match.
The standalone C++ DSP suite passes; no native engine/API changes were needed.

Native probes ran on the existing port-25567 fixture. Synth A authored 120/60/80 BPM
segments with 4/4, 3/4 and 7/8 passages, PPQ notes, expression automation and a linked
bass source. It wrote/read the MIDI conductor map, saved/reopened through the real
Lua wire/SQLite library, and queued a live note edit after the first tempo change.
Playback continued past the mapped bar boundary without a native fault. Synth B
joined during playback and reported voices/strikes and fault=0. Both clients and
the server restarted; saved maps and arrangement replayed on both clients.
The deterministic server test verifies the exact 7-second activation at the mapped
bar rather than the old fixed 6-second boundary.

Native fullscreen UI review at 186% verified readable Timing controls, searchable
point selection, Piano bar captions across the 4/4-to-3/4 transition, empty-grid note
painting through the new bar overlay, and dragging an Arrange clip from bar 1 to
bar 3 with unequal bar widths. Deletion is covered by the UI regression harness,
not claimed as a live GUI deletion check. The piano zoom caption uses stable
quarter-note units; its live bar-range caption follows panning.

Evidence is under evidence/tempo-map/first-run and restart-ui in the fixture,
with lua-tests.json and restoration.json. The fresh user baseline is
baseline-20261021-114032; all 15 database/profile/settings hashes were restored.
The normal package is installed with the disposable probe disabled. Existing
user projects, recovery state, input bindings and GUI settings are preserved.
No new port-25568 fixture, production deployment or remote push was used.

First-pass limits: step tempo changes, 1,024 tempo/128 meter events, supported
power-of-two meters, 40-240 authored BPM and existing time/note budgets. Mid-bar
meter changes start a new bar. Song edits retain quarter-note positions rather
than rescaling notes/placements. Legacy clips keep their independent pattern grid.
Metronome, tempo ramps, recording latency and hardware MIDI remain follow-ups.


## Arrangement groups, source variations/transfers and phase offsets (2026-10-21)

Package 24 / unchanged native synth API 14. All 20 Lua suites pass. The new
1,012-assertion arranger-tools suite covers cross-track selection and toggling,
group move/duplicate/delete/transpose, origin clamp, mixed-meter displacement,
atomic history/overflow/capacity/active-take rejection, independent source edits,
destination sound/mixer/band preservation, copied-source isolation and phase offsets.
A 976-case offset/partial-length sweep verifies preflight count versus actual
chase/crop/repeat expansion. Native score and MIDI export share that expansion.
Old offsets migrate to zero; malformed/nonfinite/out-of-source offsets reject.
Server tests cover private source/offset storage and invalid-write preservation.
Actual Studio callback tests exercise Shift-selection, nonmutating group drag
previews/release, Make unique, destination picker, offset numeric entry, Ctrl+A/D,
Delete and history. The unchanged standalone native DSP suite passes.

The disposable port-25567 probe created linked sources on two tracks, duplicated
and moved a selection, detached variations, edited a unique source, copied it to
another part, assigned an offset, exported/read MIDI and saved/reloaded through
the real Lua wire and SQLite. Native playback on A and late-joining B reported
strikes/voices with fault=0. Group Undo/Redo/replay and live group replacement passed.
A full server/client restart retained sources and offsets and replayed on both
clients. A live offset edit then queued and completed a native score replacement.
The offset history key explicitly marks musical changes for native recompilation.

Native fullscreen UI at 186% verified group dragging across tracks, six-placement
Select all, selected-region highlighting, the Tools dropdown, independent-source/
transfer buttons and the offset control. Shift-click, Delete and numeric offset
entry are covered by the actual callback harness rather than claimed as physical
GUI key/deletion checks. The initial native probe used cached pre-fix probe source
and stopped after Undo/Redo cleared selection; the corrected probe reselects before
its next live edit. The failed diagnostic is retained in probe-recovery; the fresh
successful run and restart evidence are in first-run and restart-ui.

The fresh baseline is evidence/arranger-tools/baseline-20261021-115528 in the
existing fixture. All 15 recorded database/profile/settings/input hashes were
restored after QA; the ordinary package is installed with the probe disabled.
See evidence/arranger-tools/lua-tests.json and restoration.json. No new fixture on
port 25568, production deployment or remote push was used.

Limits: 8 track-local sources, 64 placements per track / 256 per document, existing
8,192 stored-note and 60,000 expanded-note budgets. Make unique needs a source slot
for every selected instance. Groups move horizontally with fixed quarter-note
spacing; cross-track source transfer is an explicit independent library copy.
Resizing and phase offset affect one primary placement. Automation, sound and mix
stay with tracks. Selection is local/transient. Lasso selection, vertical placement
dragging, cross-track shared source identities and grouped edge resizing are not
part of this first pass; recording improvements remain next in the roadmap.

## Recording tools: package 25 / native synth API 15 (2026-10-21)

Built openmw and openmw-server RelWithDebInfo with MSVC2022_64. Native Release
synth tests passed, including bounded MIDI callback queue acceptance, invalid
short-message rejection, overflow/discard and recovery. All 21 Lua suites passed;
the new recording-tools suite has 40 assertions covering frozen settings/native
lead/count-in, click scores and tempo/loop phase, event age/compensation, sustain,
channel filtering, stale/overflow cleanup, local persistence and robust median/
MAD calibration. The real UI callback harness exercises setup controls without
changing the musical document/history. A stopped converted-MIDI take reserves
native clock room for its count-in; count status respects denominator pulses.

Native QA ran in the existing port-25567 fixture, with a fresh baseline captured
before QA clients. Synth A used a native two-bar count-in, captured and kept one
note, saved it privately, exported/read back exactly one MIDI note (no click
events), and played it on the native song clock. Synth B received the musical
part and reported fault=0. Calibration clock and local playback click start/stop
passed. A full server/client restart retained the musical take and local count-in,
compensation and click preferences; playback resumed on both clients with fault=0.

The initial cold diagnostic did not finish its count-in before shutdown; the
fresh native run passed. A forced-exit restart skipped the engine's player-storage
save and therefore failed the preference assertion. The corrected test used
core.quit(), verified player_storage.bin was written, then restarted and passed.
This establishes normal-exit persistence, not crash-proof preference storage.

The fullscreen Recording setup layout was visually inspected at 186% scale.
Injected mouse/key input did not reliably trigger the game controls, so physical
GUI interaction is not claimed; setup callback coverage is automated. WinMM was
available and device enumeration returned zero devices; invalid-device open failed
safely. No physical MIDI keyboard, real unplug/error driver, human tap timing or
audio loopback latency measurement was tested. Hardware playing and listening
acceptance remain follow-ups. Observer evidence is native score/voice counters,
not a captured audio loopback recording.

Evidence: evidence/record-tools/{first-run,clean-exit,restart-ui}, with diagnostic
runs retained separately. Baseline is baseline-20261021-125232; all 15 recorded
database/profile/settings/input hashes were restored after QA. Ordinary package
25 is installed with the probe disabled. Lua results and restoration hashes are
in lua-tests.json and restoration.json. No port-25568 fixture, production deploy
or remote push was used. Existing user saves/settings were retained.

## FETCHER STUDIO branding and Axiom 61 input check (package 26 / API 15)

Synth/Studio headers and the fullscreen title now display FETCHER STUDIO. Package
version advances to 26 for distribution; native API and all data identities stay
unchanged. The client/editor callback suite passed 14,562 assertions, including
recording setup. Installed Lua/manifest files match source; the ordinary port-25567
server was restarted with clients closed and existing data/settings preserved.

Windows detects Axiom 61, MIDIIN2 (Axiom 61), and MIDIIN3 (Axiom 61). A diagnostic
compiled against the exact native StudioMidiInput header successfully enumerated,
opened and closed the main Axiom 61 input. With the user playing the hardware it
received six note-ons and six velocity-zero note-offs on channel 1, velocities
29-79, with zero dropped events and connection intact. Controller messages 116/117
were also observed. This verifies the native hardware callback path; it is not a
claim of physical in-game record/replay or measured end-to-end audio latency.
No sustain pedal was available. Evidence is evidence/axiom-branding/live-input.log.

## MIDI monitoring across normal editors (package 27 / API 15)

Fixed two gates: the player frame disconnected MIDI outside Recording/Setup, and
the recorder polled notes only on Recording. A connected device now stays active
across visible Studio/Synth pages. Studio monitors the selected part/lane; Synth
uses the edited track or channel-zero manual instrument. Destination changes
release existing gates while retaining the input handle. Hide closes it promptly.
Unarmed playing does not modify notes/history. No native API/build changes.

All 21 Lua suites passed. The recording-tools suite now has 58 assertions, with
normal-page polling/release, connection retention, manual channel zero and target
changes. The real player/editor callback suite passed 15,578 assertions, including
Piano and Drums note routing, edited/manual Synth, editor switches and immediate
Hide cleanup. Physical Axiom callback input was verified in package 26; a physical
key performance through the updated in-game pages remains a manual acceptance
check. No sustain pedal is available. The port-25567 fixture is used, with no new
fixture, production deployment or remote push.

## Installed MIDI archive import and larger catalog (package 28 / API 15)

Added user-provided df_mid.zip (12 files) and DF-patch-Dfmusics2.zip (131 files)
to distinct Bardcraft/custom archive folders in the port-25567 fixture midi-data.
All 143 files passed the actual Studio MIDI parser and SHA-256 copy verification.
Existing 457 MIDI files were preserved; the installed library totals 600. Archive
contents remain local fixture assets, outside the repository.

Raised installed-song discovery from 512 to 1,024; virtualized scroll/filter
behavior is unchanged. The real client harness verifies song 600 is discoverable
and that enumeration stops at 1,024 installed songs. Local exports remain added
separately. All 21 Lua suites pass; the client suite has 15,579 assertions. This
checks file parsing/discovery, not full performance of every imported song.
See evidence/daggerfall-midi/import.json and lua-tests.json.


## Studio maintenance and 32-part playback (package 31 / API 16 / protocol 27)

Validated 2026-10-26 on Windows. Matching native client/server candidates build
in RelWithDebInfo; the standalone DSP suite builds/runs in Release. All 23
`scripts/test_procedural_synth*.lua` suites pass. The client/widget suite has
16,575 assertions, the usability suite 42, samples 26 and recording tools 62.
Eleven component checks covering surf movement, appearance and handshakes pass.

Coverage includes multi-lane capture and grouped duplication, preserving later
linear song notes and hidden legacy bars, source audition isolation, piano-roll
notes in Play clips, song auto-extension, imported-part independent duplication,
bar clear/delete/Undo, new sample root matching and live score replacement,
installed INFO transcript search, independent count-in click preferences, 32-part
MIDI import/export/score compilation/routing and four-page UI selectors. Arrange
fullscreen width/height, zoom and tab hit areas are checked through actual widget
callbacks. Visual styling and human interaction/listening remain manual checks.
Native tests configure all 32 parts, play lane 32, reject lane 33 and release all
lanes on Stop; existing deterministic automation/effects/sample/tail tests pass.

A disposable private server on UDP 25571 and separate profiles/database/packages
exercised the actual source package through the new client/server. It verified
sample root assignment, installed subtitle lookup, capturing authored Piano plus
drums, native Play clips strikes, track 32 over the server and four count-in
strikes with nonzero pre-gain PCM while the continuous metronome was disabled.
The ordinary port-25567 fixture, running user client and VPS were preserved.
An initial staging attempt lacked the new server MIDI-library module; this was
corrected by staging all matching procedural_synth modules before acceptance.

Controlled real-device OpenAL WASAPI comparison, with private monitoring muted:

- Previous native build, two independent 14-sounding-part dense scores: clocks
  fell 2.496 and 2.986 seconds behind wall time over the 55-second measurement.
- Candidate, same sounding workload and two complete 32-part outputs configured:
  both clocks stayed between -4.553 and +5.501 ms, ending -4.003 ms. Adding the
  second complete output caused a 9.845 ms continuity change. Two transient
  underruns were logged during its configuration/startup; sustained drift did
  not recur during the measurement.
- Two actual clients then owned separate placed keyboards and imported separate
  32-part MIDI songs, all parts sounding two-note chords. Synth B started while
  A was playing; both clients received both complete scores. Over 30 seconds,
  both output clocks stayed within [-2.911,+9.029] ms across the two clients,
  with no native faults, lost sessions or reported underruns in the successful
  run. A first harness run quit A before B completed its interval; the harness
  was corrected to keep both owners connected until both measurements finished.

The worker subtracts rendering time from its polling interval and yields its
mutex when that interval is exhausted. Interactive queues hold four 10-ms
buffers, with audible clocks compensating for queued audio. Exact smoothing
fixed points avoid redundant per-sample parameter/wave passes; drum/filter math
is skipped or shared when equivalent. A matching RelWithDebInfo DSP benchmark
reduced two dense 16-part streams from about 720 to 370 ms per second of PCM.
The 128-unit guard permits two 32-part keyboards plus outputs/monitors; it does
not promise unlimited voices or real-time performance for every permitted patch.

Evidence is retained locally in `build/studio-usability-qa/{baseline-result,
candidate-result,network-result}.json`, their logs, the private QA harnesses,
`build/studio-usability-dsp-32-tests.log` and component/build logs. OpenAL's null
backend showed unsuitable clock behavior and is excluded from device-timing
acceptance. These are native counters/device clocks, not audio-loopback or human
listening tests. The friend's original Vanilla songs and long-session hardware
playback should be rechecked after a matching rollout. No release/workflow or VPS
activation is part of these checks.

After private acceptance, the idle ordinary port-25567 server reported zero
players and was stopped cleanly. Matching binaries, the Studio package and its
server modules were installed there and SHA-256 verified; all 52 tracked local
database/profile/settings/key/config files matched before/after installation.
The local server was restarted. The user's running public client, other Lua
packages and public server installations were left intact.
