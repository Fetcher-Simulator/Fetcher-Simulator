# Local validation report — October 15, 2026 (America/Chicago)

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
* The CC-listed Casio keyboard was not imported: direct Sketchfab access returned 403. Exact license version/source access, Blender/NIF conversion, material optimization and a verified assets-only installation remain follow-up work. The vanilla fallback is usable now.
* Listener snapshots intentionally omit already-held notes; listeners hear subsequent live note-ons. Native/session state is transient, while personal preset libraries are durable. The first prototype has no public preset sharing, sequencer, MIDI import, Bardcraft port or external synthesizer dependency.

The delivered fixture is switched back to interactive use without the automated probe after validation. Production feature activation stays disabled by default.
