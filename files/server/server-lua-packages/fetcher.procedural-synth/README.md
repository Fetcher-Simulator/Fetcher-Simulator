# Procedural synthesizer prototype

This optional package supplies a native, spatial, multiplayer subtractive synthesizer, its in-game editor and personal presets. It does not require Bardcraft, a client ESP, prerecorded samples, a DAW, or a synthesis library.

## Local use

From the OpenMW repository in PowerShell:

```powershell
cmake --build MSVC2022_64 --config RelWithDebInfo --target openmw openmw-server --parallel 3
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-procedural-synth-test.ps1
```

The setup creates `C:\serena_workspaces_directory\fetcher-simulator\test-server\procedural-synth`. Run its `launch-test-server.bat`, then `launch-test-clients.bat`. This uses a copy of the user's current `fetcher-simulator\launch-test-clients.ps1` workflow, with Synth A and Synth B, port 25567, separate binaries, profiles and a separate SQLite database. The existing root test binaries, Mario/Sith Soldier profiles, Starwind content configuration and original test database are preserved.

The first setup reads the original local database in read-only mode and copies only the `ass` account and its public-key registrations into a new database. It creates two vanilla Imperial characters. A local authentication key is copied privately into this fixture. Neither keys nor account/database files are committed. An existing destination database is preserved on subsequent setup runs. Stop fixture processes before rerunning setup.

Both players start in vanilla `ToddTest`, near `(2175,2176,-703)`. The server places one **OpenMW Synthesizer** at `(2175,2256,-650)`. Setup installs the included, hash-verified Casio keyboard assets for both local profiles and configures the runtime activator to use them. Add `-VanillaSynth` to setup to use the original lute mesh instead. Activate the instrument to acquire the editor. A second player can listen; concurrent editing is denied. Mouse keys hold until release; pressing lower on a key increases velocity. Z/S/X/D/C/V/G/B/H/N/J/M/comma play chromatic C4–C5 when a text field is not consuming input. Escape or Close releases notes and the session.

For the scripted live probe, add `-LiveProbe` to setup, then run the same launchers. It opens the actual editor, plays a chord, changes waveform/cutoff, saves and reloads presets, closes and reacquires. Finally Synth A holds note 76 and logs `READY abrupt disconnect`; terminate only that fixture client to exercise observer takeover. Synth B then plays briefly and logs `PASS observer takeover complete`. Rerun setup without `-LiveProbe` to move this optional probe out of the package directory and return to interactive use. The probe saves its own `Integration Tone` presets only in the isolated fixture database.

The editor also has MIDI previous/next, **Play MIDI** and **Stop** controls. Setup copies the locally installed Bardcraft MIDI directory into an isolated data directory; it copies no Bardcraft ESP, scripts or samples. Override its source with `-BardcraftDataDirectory 'C:\path\to\Bardcraft'`. Other installations can supply their own `.mid`/`.midi` files under a configured data directory's `midi/` folder. Files are discovered on the first frame even when resuming a saved multiplayer character. Original mod/song files are preserved and are not redistributed by this repository.

Format 0/1 MIDI with PPQ timing supports merged tracks, tempo changes, running status, velocity, sustain pedal and note cleanup. All melodic tracks use the selected synth sound; program changes, pitch bend and expression/volume controllers do not select or alter sounds. Channel 10 percussion is skipped. Imports are limited to 512 KiB, 64 tracks, 60,000 retained events and 30 minutes; parsing yields between bounded groups of operations. Playback stops on long frame stalls or excessive density instead of bursting stale notes into the server. Manual playing, preset loads, Stop, closing, disconnect and session loss release playback. MIDI notes use the same validated note transport and 3D synth as live playing, rather than transferring audio or MIDI files to listeners.

During a session both clients pose the authenticated performer's upper arms, forearms and hands toward the instrument. Two-link IK preserves arm lengths and clamps unreachable targets; live notes add small wrist/target pulses. This first pass does not animate keyboard keys or fingers. The small standless Casio remains the test asset; hand contact and posture require visual tuning for a future keyboard/stand, and an out-of-reach instrument cannot be reached by stretching the skeleton. The pose does not move or turn the actor. Close or session cleanup restores normal animation, with a native half-second expiry as backup.

Use `-MidiProbe` for the disposable two-client Greensleeves/arm-pose test. It implies `-LiveProbe`; setup without either flag restores interactive mode.

## Files and architecture

* `apps/openmw/mwsound/proceduralsynth.{hpp,cpp}`: standalone DSP/control engine; no Lua, OpenAL, VFS or sample-file dependency.
* `apps/openmw/mwsound/synthdecoder.hpp`: procedural mono PCM decoder for existing OpenAL streaming.
* `apps/openmw/mwsound/{sounddecoder.hpp,openaloutput.cpp,soundmanagerimp.*}` and `apps/openmw/mwbase/soundmanager.hpp`: interactive streaming and spatial source lifecycle.
* `apps/openmw/mwlua/synthbindings.*`, `luabindings.cpp`: the player-context `openmw.synth` API.
* `apps/openmw-server/scripts/procedural_synth{,_schema}.lua`: ownership, validation, relay, runtime activator and persistence.
* This package's `client/{global,player,editor}.lua`: activation/event forwarding, local playback and editor; `shared/schema.lua` is checked against the server schema by tests.
* `scripts/procedural-synth-tests`, `scripts/test_procedural_synth*.lua`, `scripts/setup-procedural-synth-test.ps1` and `scripts/seed-procedural-synth-test.py`: unit tests, client harness and disposable local setup.

DSP is 48 kHz mono, converted to signed 16-bit PCM for OpenAL spatialization. Each voice has two oscillators, amp ADSR, filter ADSR and a resonant low-pass state-variable filter. Sine, additive band-limited triangle, PolyBLEP saw and PolyBLEP square are supported. There is a shared sine LFO for pitch, filter or amplitude modulation. Continuous values and waveform mixtures are smoothed; output uses a soft limiter. Default polyphony is 16, configurable from 1 to 32, with oldest-voice stealing. There are at most eight simultaneous native instances.

Only OpenAL's existing stream worker renders PCM. Controls use fixed-size command storage (256 commands) and a mutex which the audio reader only tries, never waits on. Overflow clears queued commands and requests voice release. Parameter snapshots and all voice/filter/oscillator storage are bounded. Interactive decoders use three 10 ms queued buffers and a 5 ms worker cadence while active. Ordinary streams retain their six 125 ms buffers and normal 50 ms cadence. No continuous PCM is sent over the network.

SoundManager owns streams attached to world references. They use normal environmental processing, SFX/master volume, fixed musical pitch independent of simulation time, 100-unit reference distance and 3000-unit maximum distance. Cell unload, object deletion/disable, explicit close, ten seconds without native keepalive, sound clear and backend shutdown clean up streams. Lua also drops instances on cell change, invalid references, disconnect and missing server liveness.

## API

Available to player scripts on a compatible binary:

```lua
local synth = require('openmw.synth') -- version == 1, sampleRate == 48000
local instrument = synth.create(worldObject) -- spatial source; throws when unavailable
instrument:set('wave1', 2)           -- false for unknown/out-of-range/nonfinite values
instrument:noteOn(60, 0.8)           -- MIDI pitch 0..127, velocity 0..1
instrument:noteOff(60)
instrument:allNotesOff()             -- envelope release, clears pending notes
instrument:keepAlive()              -- call at least every few seconds while in use
print(instrument.peak, instrument.voices, instrument.closed)
instrument:close()                  -- idempotent; no further playback
```

`synth.parameters[name]` exposes immutable `min`, `max`, `default`, `discrete` metadata. `peak` is generated PCM peak before spatial attenuation, not a microphone or final output meter. The package's optional `I.ProceduralSynth` interface exposes `status`, `noteOn`, `noteOff`, `setParameter`, `presetCommand` and `close` for other client scripts/input methods, subject to an owned editor session. Older clients without the native module show a compatibility message on activation instead of acquiring a lock.

Interface version 2 adds `midiSongs()`, `playMidi(catalogPath)` and `stopMidi()`. Native `openmw.animation.boneTransformsVersion == 1` adds `getBoneNames`, `getBoneTransform`, `setBoneTransform` and `clearBoneTransform`; see `files/lua_api/openmw/animation.lua` for coordinate spaces, threading, validation and expiry. Transform reads return the matrix and a rotation-only transform, with an option to exclude scripted offsets for IK. `openmw.mp.getPlayerObject(serverGuid)` resolves the authoritative performer to the correct local/remote model in `onFrame`. These visual APIs are independent of the synthesizer. Older clients with no bone extension can still use the audio/editor, with the pose omitted.

All 26 controls are editable: both oscillator waveforms/levels/tuning/fine tuning, oscillator-2 enable, amplitude ADSR, cutoff/resonance, filter-envelope amount/ADSR, LFO rate/depth/destination, volume, gain and voice limit. The dark editor includes numeric feedback, amplitude-envelope graph, theoretical oscillator preview, output peak/voice count, and mouse/PC musical input. Graph/meter updates are bounded and do not rebuild the entire editor each frame.

New, Save, Save As, Load, Rename, Delete and Reset are implemented. The arrow controls select a catalog entry; Load applies it. Built-ins are Soft Pad, Plucked String, Retro Lead, Bass and Flute-like. Built-ins are read-only; Save on a built-in creates a personal preset. Each character has up to 32 personal presets, stable monotonic `p:N` identifiers and 64-byte names. Existing data is isolated in the `ProceduralSynth/presets` character-storage namespace. Format version 1 fills absent fields with defaults and refuses unknown future versions without overwriting them.

## Server and protocol

Install the three server Lua files through the existing release-files list and add this package directory to the configured server package root. `server.omwscripts` includes the server script, but it returns an empty module unless `Config.PROCEDURAL_SYNTH_ENABLED = true`; default configuration is disabled. Compatible binaries must already be installed on clients. Generic production placement remains an explicit operator operation, for example the existing admin `/placeat fetcher_procedural_synth_v1 1 100` command. The automatic test placement settings are only generated in the isolated fixture.

Existing reliable custom Lua events carry `Synth_Request` client-to-server and `Synth_Message` server-to-client. A client global activation handler forwards only the synth activator to its player script. Acquisition resolves an actual server-placed mpNum and verifies its record, authoritative cell and distance (300 units). The existing server `Activate` intent hook also recognizes the synth. Sender pid/characterId are supplied by the native authenticated bridge; payloads cannot choose a different owner or storage character.

Every owned session has a server epoch, input sequence, output sequence and preset revision. Requests are shallow and bounded; preset fields are whitelisted and finite. Notes have a 40/sec, burst-80 token bucket; full parameter updates are coalesced to 10 Hz by the client and limited to 12/sec, burst 16 by the server. Preset operations, acquisition, sync and heartbeat have separate budgets. Note-off/panic/close bypass the musical bucket; redundant note-offs/panic are discarded so they cannot become an unlimited relay. Queue/rate overflow uses release instead of leaving a partially accepted chord held. The controller plays locally and receives no duplicate note echo.

An 8-second lease renewed every 2 seconds, authoritative object/range checks, disconnect and cell-change handlers release ownership. Server liveness reaches listeners every two seconds; silent transport loss expires client audio. Clients in the same or loaded actor cells receive state. Newly relevant listeners get current parameters before future live notes and never receive a historical note-on replay. Notes are intentionally not reconstructed midway through a held chord for a late join.

## Model and delivery

The fallback is the vanilla Morrowind `meshes/m/Misc_de_lute_01.nif`, confirmed from the original Morrowind record. A runtime activator uses this existing mesh, so no ESP or extra asset install is required.

The custom model is [Keyboard (Casio VL-Tone)](https://sketchfab.com/3d-models/keyboard-casio-vl-tone-be22fbe1ef0044f3bedc6e3edf459593) by **AleixoAlonso**, under **CC BY 4.0**. The author and exact license were verified against the model's [public Sketchfab metadata](https://api.sketchfab.com/v3/models/be22fbe1ef0044f3bedc6e3edf459593) on October 16, 2026. The user supplied the original download. The converted runtime bundle is in `files/server/optional-assets/fetcher.procedural-synth`, with attribution, license URI, source provenance, conversion report and SHA-256 manifest. Preserve these notices when distributing it.

The Morrowind NIF retains 6,896 visual triangles and separate keys/buttons, adds a simple 12-triangle box collider, and measures 48 × 12.04 × 5.07 game units. Materials use the original UV atlas converted to a mipmapped 2K DDS and approximate the LCD glass. A 1K `_n.dds` normal map is included for OpenMW's optional normal-map autodetection; setup does not change graphics preferences. Blender's reimport of the actual exported NIF was rendered and inspected. Conversion and packaging commands are documented in `scripts/procedural-synth-assets/README.md`.

Server Lua packages transfer scripts, not NIF/textures. For another installation, copy the entire assets bundle to a client data directory and add that directory to every client's `openmw.cfg` with `data="..."`. Install it on the server's configured data path as well, then set `Config.PROCEDURAL_SYNTH_MODEL = 'meshes/fetcher/synth/keyboard.nif'`. No new ESP is required. The isolated setup does this automatically and verifies every manifested file before and after copying. Shared local profiles use one asset data directory. Keep the default lute configuration on servers whose clients lack the assets. Automatic mesh transfer is not implemented.

## Validation and remaining acceptance work

See `TEST-REPORT.md` for commands, observed live results and limits. In particular, event timing and nonzero procedural PCM do not establish perceptual audio quality, final speaker output, walking-distance attenuation or UI visual QA.
