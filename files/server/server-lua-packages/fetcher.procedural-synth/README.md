# Procedural synthesizer and Studio

This optional package supplies a native, spatial, multiplayer subtractive synthesizer, its in-game sound editor, MIDI mixer, piano clips, drum machine and personal presets/projects. It does not require Bardcraft scripts, a client ESP, prerecorded samples or an external synthesis library.

## Local use

From the OpenMW repository in PowerShell:

```powershell
cmake --build MSVC2022_64 --config RelWithDebInfo --target openmw openmw-server --parallel 3
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-procedural-synth-test.ps1
```

The setup creates `C:\serena_workspaces_directory\fetcher-simulator\test-server\procedural-synth`. Run its `launch-test-server.bat`, then `launch-test-clients.bat`. This uses a copy of the user's current `fetcher-simulator\launch-test-clients.ps1` workflow, with Synth A and Synth B, port 25567, separate binaries, profiles and a separate SQLite database. The existing root test binaries, Mario/Sith Soldier profiles, Starwind content configuration and original test database are preserved.

The first setup reads the original local database in read-only mode and copies only the `ass` account and its public-key registrations into a new database. It creates two vanilla Imperial characters. A local authentication key is copied privately into this fixture. Neither keys nor account/database files are committed. An existing destination database is preserved on subsequent setup runs. Stop fixture processes before rerunning setup.

Both players start in vanilla `ToddTest`, near `(2175,2176,-703)`. The server places one **OpenMW Synthesizer** at `(2175,2256,-703)`. Setup installs the included, hash-verified Yamaha keyboard and X-stand assets for both local profiles and configures the runtime activator to use them. Add `-VanillaSynth` to setup to use the original lute mesh instead. Activate the instrument to acquire the editor. A second player can listen; concurrent editing is denied. Mouse keys hold until release; pressing lower on a key increases velocity. Z/S/X/D/C/V/G/B/H/N/J/M/comma play chromatic C4â€“C5 when a text field is not consuming input. Hide, Escape or F8 hides the editor and releases manually held keys while MIDI, spatial audio, arm posing and the session heartbeat continue. Normal game camera controls are available while hidden. F8 or activating the same instrument reopens the editor without restarting playback; draft preset names and selection are preserved. Finish stops playback and releases the session. Neither the performance package nor the MIDI test probe steers a camera. Add `-ResetStage` to explicitly back up and reset the fixture stage/character positions; ordinary setup preserves saved positions.

For the scripted live probe, add `-LiveProbe` to setup, then run the same launchers. It opens the actual editor, plays a chord, changes waveform/cutoff, saves and reloads presets, closes and reacquires. Finally Synth A holds note 76 and logs `READY abrupt disconnect`; terminate only that fixture client to exercise observer takeover. Synth B then plays briefly and logs `PASS observer takeover complete`. Rerun setup without `-LiveProbe` to move this optional probe out of the package directory and return to interactive use. The probe saves its own `Integration Tone` presets only in the isolated fixture database.

The editor also has MIDI previous/next, **Play MIDI** and **Stop** controls. Setup copies the locally installed Bardcraft MIDI directory into an isolated data directory; it copies no Bardcraft ESP, scripts or samples. Override its source with `-BardcraftDataDirectory 'C:\path\to\Bardcraft'`. Other installations can supply their own `.mid`/`.midi` files under a configured data directory's `midi/` folder. Files are discovered on the first frame even when resuming a saved multiplayer character. Original mod/song files are preserved and are not redistributed by this repository.

Format 0/1 MIDI with PPQ timing supports merged tracks, tempo changes, running status, velocity, sustain pedal and note cleanup. Imports retain SMF track names, channels, program metadata and tempo maps. Each (SMF track, MIDI channel) becomes an independently assignable part; channel-10 percussion is split further by pitch, with kick/snare/hat synth defaults. Program changes, pitch bend and expression/volume controllers do not alter the assigned sound. Imports exceeding 16 parts including existing clips are rejected explicitly. Imports are limited to 512 KiB, 64 tracks, 60,000 retained events and 30 minutes; parsing yields between bounded groups of operations. Playback stops on long frame stalls or excessive density instead of bursting stale notes into the server. Manual playing, preset loads, Stop, Finish, disconnect and session loss release playback. MIDI notes use the same validated note transport and 3D synth as live playing, rather than transferring audio or MIDI files to listeners.

During a session both clients pose the authenticated performer's upper arms, forearms and hands toward the instrument. Two-link IK preserves arm lengths and clamps unreachable targets; live notes add small wrist/target pulses. This first pass does not animate keyboard keys or fingers. The default test asset combines a wider Yamaha keyboard and X-stand, with the keys approximately 89 units above its floor origin. Hand contact and posture still require visual tuning for different races and positions; unreachable targets cannot be reached by stretching the skeleton. The pose does not move or turn the actor. Finish or session cleanup restores normal animation, with a native half-second expiry as backup.

Use `-MidiProbe` for the disposable two-client Greensleeves/arm-pose test. It implies `-LiveProbe`; setup without either flag restores interactive mode.

## Studio workflow

Both views share a local window toolbar. Drag **Drag to move**, resize with the
bottom-right mint grip, and use **Scale - / +** to change text and control sizes
(75-200%, bounded to fit the current viewport). **Fullscreen / Restore** toggles
an in-game full-viewport view and restores the previous window geometry. Window
position, size, scale and fullscreen preference are saved in local player Lua
storage and survive view switches and reopening; they are not sent to the server.

**Monitor Volume (local)** controls the same **Player Instruments** category as
Options > Sound. It affects every procedural instrument you hear, including other
players, independently of Effects and background Music. Game Master still applies.
The Studio Master and track faders remain the shared project mix; monitoring does
not modify presets, projects or other listeners. Native `openmw.synth` version 3
provides `getMonitorVolume()` and `setMonitorVolume(0..1)`; setters validate finite
values and apply to already-playing streams on the engine thread.

The bar-count control cycles from 1 through 16 bars. It changes playback length
without deleting later drum steps or piano notes. Each clip retains 256 steps;
hidden bars survive Save/Load and return when the loop grows. Earlier 1-4-bar
projects are padded on load while retaining their existing composition.

Activate the keyboard and click **Studio**. The tabs switch between **Synth**,
**Mixer**, **Piano** and **Drums**, using one window so only one view captures game
input. F8 / Hide / Escape preserves playback and reopens the last view; Finish
releases the instrument and every channel. Cameras remain under normal game control.

1. Choose a local MIDI using the arrows and click **Import**. This lists all parts
   without starting playback. **Play song** starts the complete MIDI with its
   original tempo map. The original Synth editor's Play MIDI uses the same routing.
2. In **Mixer**, each row selects its own saved sound with the arrows. The catalog
   includes personal character presets as soon as the server response arrives;
   built-ins are available immediately. Faders, Mute, Solo and the Master fader
   apply live. **Sound** opens that row's sound in the synth editor; its changes
   affect that part. Save/Save As there adds to the existing personal preset library.
3. **+ Piano clip** creates an editable, quantized clip. Piano displays one bar and
   octave at a time. Click a grid cell to add a note; click anywhere on a note to
   remove it. Set note length and velocity before insertion. The Piano tab also
   displays imported MIDI parts with track/channel identity and original beat
   positions; imported notes are currently a read-only view.
4. **Drums** adds Kick, Snare or Hat lanes. Click a lane's name to audition it;
   click steps to toggle them at the chosen velocity. Pitch buttons retune that
   lane. Mixer can assign any saved sound to it. Patterns have 16 steps per bar,
   1-16 bars, swing and looping. **Play clips** runs drum and piano clips together.
   **Play song** combines them with the MIDI; clips follow the MIDI's tempo map.
5. **Save** in Studio stores the current named project in the authenticated
   character's `ProceduralSynth/project` SQLite storage key. **Load** replaces the
   current working project with that saved document, without autoplay. This first
   pass has one working project per character. Project and preset storage are
   separate; existing presets are preserved. Saves include sound snapshots,
   assignments, mix, notes, steps, tempo, swing and the MIDI VFS path. The original
   MIDI must remain installed; its file/audio is not stored or sent to listeners.

The document has a maximum of 16 total MIDI/piano/drum tracks, 128 clip notes per
track and 512 clip notes overall. Imported full-length MIDI is retained separately
in memory (up to the existing 60,000-event bound); it is not truncated to the clip
length. Dense piano-roll views draw at most 512 visible notes. All tracks currently
output at the placed keyboard. The document's separate `output` field and
part-to-channel routing leave room for held-instrument destinations, but physical
instrument selection, recording, arrangement/song export, automation, effects and
pan controls are not implemented. Manual playing in the Synth view takes over
from the transport; drum-pad audition can run alongside clips.

Use **-DawProbe** with local setup for the disposable two-client parts/mixer,
hidden playback, drum/piano and server-project round-trip test. It implies the
probe package; setup without probe flags restores normal interaction.

## Files and architecture

* `apps/openmw/mwsound/proceduralsynth.{hpp,cpp}`: standalone DSP/control engine; no Lua, OpenAL, VFS or sample-file dependency.
* `apps/openmw/mwsound/synthdecoder.hpp`: procedural mono PCM decoder for existing OpenAL streaming.
* `apps/openmw/mwsound/{sounddecoder.hpp,openaloutput.cpp,soundmanagerimp.*}` and `apps/openmw/mwbase/soundmanager.hpp`: interactive streaming and spatial source lifecycle.
* `apps/openmw/mwlua/synthbindings.*`, `luabindings.cpp`: the player-context `openmw.synth` API.
* `apps/openmw-server/scripts/procedural_synth{,_schema}.lua`: ownership, validation, relay, runtime activator and persistence.
* This package's `client/{global,player,editor,studio,workstation,midi,window,controls}.lua`: activation, native channel lifecycle, sound/Studio views, transport/clip editing and SMF import. `shared/schema.lua` and `shared/project.lua` are checked against their server copies by tests.
* `scripts/procedural-synth-tests`, `scripts/test_procedural_synth*.lua`, `scripts/setup-procedural-synth-test.ps1` and `scripts/seed-procedural-synth-test.py`: unit tests, client harness and disposable local setup.

DSP is 48 kHz mono, converted to signed 16-bit PCM for OpenAL spatialization. Each voice has two oscillators, amp ADSR, filter ADSR and a resonant low-pass state-variable filter. Sine, additive band-limited triangle, PolyBLEP saw/square and deterministic white noise are supported. A per-voice exponential pitch envelope provides falling kick-drum pitch. Synth Kick, Noise Snare and Closed Hat are native synth presets. There is a shared sine LFO for pitch, filter or amplitude modulation. Continuous values and waveform mixtures are smoothed; output uses a soft limiter. Default polyphony is 16, configurable from 1 to 32, with oldest-voice stealing. There are at most 32 simultaneous native instances across the whole client. DAW tracks create streams lazily on their first note and use at most eight voices each; the manual synth retains its 1–32 voice control. Running out of sources stops the transport with a message.

Only OpenAL's existing stream worker renders PCM. Controls use fixed-size command storage (256 commands) and a mutex which the audio reader only tries, never waits on. Overflow clears queued commands and requests voice release. Parameter snapshots and all voice/filter/oscillator storage are bounded. Interactive decoders use three 10 ms queued buffers and a 5 ms worker cadence while active. Ordinary streams retain their six 125 ms buffers and normal 50 ms cadence. No continuous PCM is sent over the network.

SoundManager owns streams attached to world references. They use normal environmental processing, Player Instruments/master volume, fixed musical pitch independent of simulation time, 100-unit reference distance and 3000-unit maximum distance. Cell unload, object deletion/disable, explicit close, ten seconds without native keepalive, sound clear and backend shutdown clean up streams. Lua also drops instances on cell change, invalid references, disconnect and missing server liveness.

## API

Available to player scripts on a compatible binary:

```lua
local synth = require('openmw.synth') -- version == 4, sampleRate == 48000, maxInstances == 32
local instrument = synth.create(worldObject) -- spatial source; throws when unavailable
instrument:set('wave1', 2)           -- false for unknown/out-of-range/nonfinite values
instrument:noteOn(60, 0.8)           -- MIDI pitch 0..127, velocity 0..1
instrument:noteOff(60)
instrument:allNotesOff()             -- envelope release, clears pending notes
instrument:keepAlive()              -- call at least every few seconds while in use
print(instrument.peak, instrument.voices, instrument.closed)
instrument:close()                  -- idempotent; no further playback
```

`synth.parameters[name]` exposes immutable `min`, `max`, `default`, `discrete` metadata. `peak` is generated PCM peak before spatial attenuation, not a microphone or final output meter. The package's optional `I.ProceduralSynth` interface exposes `status`, `noteOn`, `noteOff`, `setParameter`, `presetCommand` and `close` for other client scripts/input methods, subject to an owned editor session. Older clients without native module version 4 show a compatibility message on activation instead of acquiring a lock.

Interface version 4 adds `studio()`, `workstation()`, `importMidi(path)`, `saveProject()` and `loadProject()`, plus per-channel meter/parameter summaries and transport time in `status()`. Version 3 added `hide()` and `show()` and the `status().editorVisible` field; `editing` continues to mean owning the session, including while hidden. `close()` retains its original meaning of stopping playback and releasing ownership. Interface version 2 added `midiSongs()`, `playMidi(catalogPath)` and `stopMidi()`. Native `openmw.animation.boneTransformsVersion == 1` adds `getBoneNames`, `getBoneTransform`, `setBoneTransform` and `clearBoneTransform`; see `files/lua_api/openmw/animation.lua` for coordinate spaces, threading, validation and expiry. Transform reads return the matrix and a rotation-only transform, with an option to exclude scripted offsets for IK. `openmw.mp.getPlayerObject(serverGuid)` resolves the authoritative performer to the correct local/remote model in `onFrame`. These visual APIs are independent of the synthesizer. Older clients with no bone extension can still use the audio/editor, with the pose omitted.

The melodic controls are editable: both oscillator waveforms/levels/tuning/fine tuning, oscillator-2 enable, amplitude ADSR, cutoff/resonance, filter-envelope amount/ADSR, LFO rate/depth/destination, volume, gain, voice limit, pitch-envelope amount and pitch decay. The dark editor includes numeric feedback, amplitude-envelope graph, theoretical oscillator preview, output peak/voice count, and mouse/PC musical input. Graph/meter updates are bounded and do not rebuild the entire editor each frame.

New, Save, Save As, Load, Rename, Delete and Reset are implemented. The arrow controls select a catalog entry; Load applies it. Built-ins are Soft Pad, Plucked String, Retro Lead, Bass, Flute-like, Synth Kick, Noise Snare and Closed Hat and Open Hat. Built-ins are read-only; Save on a built-in creates a personal preset. Each character has up to 32 personal presets, stable monotonic `p:N` identifiers and 64-byte names. Existing data is isolated in the `ProceduralSynth/presets` character-storage namespace. Format version 1 fills absent fields with defaults and refuses unknown future versions without overwriting them.

## Server and protocol

Install the three server Lua files through the existing release-files list and add this package directory to the configured server package root. `server.omwscripts` includes the server script, but it returns an empty module unless `Config.PROCEDURAL_SYNTH_ENABLED = true`; default configuration is disabled. Compatible binaries must already be installed on clients. Generic production placement remains an explicit operator operation, for example the existing admin `/placeat fetcher_procedural_synth_v1 1 100` command. The automatic test placement settings are only generated in the isolated fixture.

Existing reliable custom Lua events carry `Synth_Request` client-to-server and `Synth_Message` server-to-client. A client global activation handler forwards only the synth activator to its player script. Acquisition resolves an actual server-placed mpNum and verifies its record, authoritative cell and distance (300 units). The existing server `Activate` intent hook also recognizes the synth. Sender pid/characterId are supplied by the native authenticated bridge; payloads cannot choose a different owner or storage character.

Every owned session has a server epoch, input sequence, output sequence and preset revision. Requests and nested project documents are whitelisted and bounded; preset fields are whitelisted and finite. Notes have a 40/sec, burst-80 token bucket; full parameter updates are coalesced to 10 Hz by the client and limited to 12/sec, burst 16 by the server. Preset operations, acquisition, sync and heartbeat have separate budgets. Note-off/panic/close bypass the musical bucket; redundant note-offs/panic are discarded so they cannot become an unlimited relay. Queue/rate overflow uses release instead of leaving a partially accepted chord held. The controller plays locally and receives no duplicate note echo. Channel IDs 1–16 identify DAW timbres; channel 0 is the original manual synth. Note ownership is keyed by channel and pitch. Channel parameters/levels precede notes and are included in late-listener snapshots. The original global 40/sec, burst-80 note-on budget is shared across the whole project. Off/panic remain safety operations; stopped/deleted tracks release their sources.

An 8-second lease renewed every 2 seconds, authoritative object/range checks, disconnect and cell-change handlers release ownership. Server liveness reaches listeners every two seconds; silent transport loss expires client audio. Clients in the same or loaded actor cells receive state. Newly relevant listeners get current parameters before future live notes and never receive a historical note-on replay. Notes are intentionally not reconstructed midway through a held chord for a late join.

## Model and delivery

The fallback is the vanilla Morrowind `meshes/m/Misc_de_lute_01.nif`, confirmed from the original Morrowind record. A runtime activator uses this existing mesh, so no ESP or extra asset install is required.

The default custom model combines **Yamaha keyboard** by **Rapunzel123** (Sketchfab UID `ed15fc0df2d244a583e2ac0929b57045`) and **Keyboard X Stand** by **Michael Perez / pixelflygraphics** (`d76fd9cba4234dd1891581c2df6b7b00`), both CC BY 4.0. The user supplied the downloads; source metadata and hashes are recorded in `stage-provenance.json`. Full source links, attribution, license and adaptation details accompany the runtime bundle in `files/server/optional-assets/fetcher.procedural-synth`. Preserve these notices when distributing it. The older Casio model and its notices remain available as an alternative.

The combined Morrowind NIF is 104 × 30.16 × 111.68 game units, with its origin on the floor, 12,189 visual triangles and seven box colliders (84 triangles). Four diffuse DDS textures preserve the keyboard's original material assignments and the stand atlas; two optional `_n.dds` normals are included. Textures have mipmaps. The actual exported NIF was reimported and rendered for inspection. Conversion and packaging commands are documented in `scripts/procedural-synth-assets/README.md`.

Server Lua packages transfer scripts, not NIF/textures. For another installation, copy the entire assets bundle to a client data directory and add that directory to every client's `openmw.cfg` with `data="..."`. Install it on the server's configured data path as well, then set `Config.PROCEDURAL_SYNTH_MODEL = 'meshes/fetcher/synth/keyboard-stand.nif'`. No new ESP is required. The isolated setup does this automatically and verifies every manifested file before and after copying. Shared local profiles use one asset data directory. Keep the default lute configuration on servers whose clients lack the assets. Automatic mesh transfer is not implemented.

## Validation and remaining acceptance work

See `TEST-REPORT.md` for commands, observed live results and limits. In particular, event timing and nonzero procedural PCM do not establish perceptual audio quality, final speaker output, walking-distance attenuation or UI visual QA.

## Native drum machine and precise editing

Package version 5 requires native `openmw.synth.version >= 4`. The existing
version-1 preset/project formats remain supported. Missing new sound parameters
receive stable defaults: `drumType = 0` and `ampCurve = 1` retain the melodic
engine. Existing saved drum presets/project snapshots retain their old settings;
reload a built-in drum preset to select the new native drum engine.

In Studio > Drums, **+ Kit** adds Kick, Snare, Closed hat and Open hat as one
undoable edit. Individual lane buttons remain available. Click a lane to audition
and select it, then **Sound** to edit that lane directly. Drum presets select the
dedicated editor automatically; **Drum / Synth** switches engine family. Drum type
values are 0 Synth, 1 Kick, 2 Snare, 3 Closed hat and 4 Open hat.

Native drum voices have fixed tuning in Hz independent of incoming MIDI note,
separate exponential body/noise decays, noise high/low-pass filtering, a pitch
sweep, attack click and saturation. Hats use six inharmonic, band-limited square
oscillators alongside filtered noise; their metallic layer is filtered too. A
closed hat chokes open-hat channels belonging to the same placed instrument on
every listener. Repeated hits can overlap within the track's voice limit.
Drum decays specify time to -60 dB. MIDI note-off leaves a one-shot tail intact;
Stop, panic, session cleanup and source expiry still release it. The server keeps
one pending-release flag for accepted one-shot hits so Stop reaches listeners even
after all MIDI gates have ended; redundant panic remains suppressed.

All 41 native parameters are available across the two sound views. Click a numeric
value and press Enter or leave the field to apply it. Times display and accept
milliseconds by default (150 means 150 ms); explicit `0.15 s` is also accepted.
Frequency accepts Hz or `2 kHz`; percentages accept `75` or `75%`. Invalid/nonfinite
and out-of-range values preserve the previous setting. Time/frequency sliders are
logarithmic. +/- buttons adjust small increments; Shift makes continuous buttons
and slider drags finer. The melodic engine also has an amplitude curve control.
Studio tempo, swing and note-entry velocity use the same numeric controls.

Studio **Undo / Redo** (Ctrl+Z / Ctrl+Y or Ctrl+Shift+Z outside text fields) restores
project edits, including steps, notes, track additions/deletions, kit creation,
loop length, mix and track sound changes. History is local, bounded to 64 edits,
and is not saved or transmitted. Consecutive edits to one continuous value are
coalesced within 400 ms. Undo/redo stops playback and marks the restored document
as a new unsaved edit; loading a saved project starts fresh history. Standalone
manual-synth edits outside a project do not have project history.

Use setup **-DrumProbe** for the disposable two-client drum/history/choke/Stop and
SQLite round-trip test. It leaves Studio open for interface inspection. Setup
without probe flags restores interactive operation. The native DSP test accepts
`--preview <path.wav>` to render a normalized eight-second drum audition and beat
without the game; this is a test helper, not Studio WAV export.

Audio-clock scheduling, arrangement, recording, sampling, mixer effects and export
remain subsequent milestones. This change does not claim sample-accurate timing
or acoustic-drum realism.
