# Band sound foundation auditions

## Original full-band checks (package 70)

In game: New Song -> Band sound checks -> choose exercise and sound set -> Open
sound check. Normal unsaved-work protection applies. Play song or clips, edit
individual notes and compare whole sound sets or Focus selected. Opening does
not start playback or automatically create NPCs.

```powershell
New-Item -ItemType Directory -Force build/studio70-band-checks | Out-Null
lua scripts/procedural-synth-tests/export-band-checks.lua build/studio70-band-checks
cmake --build build/synth-core-tests --config Release --target score-audition
& build/synth-core-tests/Release/score-audition.exe build/studio70-band-checks/heavy-authored.bin build/studio70-band-checks/heavy-authored.tsv files/server/optional-assets/fetcher.studio-samples build/studio70-band-checks/heavy.wav 0 9.5714285714 all
ctest --test-dir build/synth-core-tests -C Release -R score-audition-config --output-on-failure
```

Five four-bar projects use the same runtime factory: heavy rhythm (112 BPM),
thrash (184), funk (104), clean chords/lead (92) and acoustic/classical (96).
`exercises.tsv` lists exact render lengths (one cycle plus a one-second tail).
`patch` removes only attack snapshots; `authored` retains them; `source` bypasses
instrument amp/parallel/cab processing while retaining the performed notes.
Use `guitar`, `bass` or `drums` to render corresponding stems. No copied melody
or external MIDI is required. Clean/open phrases may differ little between
patch and authored modes; this is a control comparison, not an asserted upgrade.

TSV configurations now accept `master=`, `effects.<name>=`, `mix.<name>=`, and
an optional fourth track-header field for audibility (`@lane level group 0/1`).
Legacy three-field headers retain audible tracks, default channel strips and
master 1. New exporters write actual project levels and channel/return settings.
Dry/amp-bypass comparisons isolate a stage; they do not simulate game-room
reflections, source distance or physical controller timing.

The package-70 local review files pair Patch first, a half-second pause, then
Authored at 0.08 RMS with 0.85 peak protection. Gains apply only to review audio.
Raw measurements and per-pair gains are retained alongside the WAVs. Do not
change a saved sound merely because a louder example appears more impressive.
Dedicated captured mutes/releases/slap and listening acceptance remain open.

## Original connected-performance exercise (package 58)

```powershell
New-Item -ItemType Directory -Force build/studio-legato-auditions | Out-Null
lua scripts/procedural-synth-tests/export-legato-auditions.lua build/studio-legato-auditions
cmake --build build/synth-core-tests --config Release --target score-audition
foreach ($source in @('guitar-lead','sampled-electric-lead')) {
    foreach ($mode in @('poly','legato','picked')) {
        & build/synth-core-tests/Release/score-audition.exe "build/studio-legato-auditions/$source-$mode.bin" "build/studio-legato-auditions/$source-$mode.tsv" files/server/optional-assets/fetcher.studio-samples "build/studio-legato-auditions/$source-$mode.wav" 0 5 all
    }
}
```

Six original note events exercise overlapping gates, held-note return and a gap
before a fresh phrase. Poly is the unchanged baseline; Legato retains its source
and picked velocity; Picked reattacks during connections. Recorded output needs
the existing credited electric DI bank. Compare at matched levels. This models
pitch connections; it does not add captured fret/hammer-on/release recordings.

## Original string-performance exercise (package 57)

```powershell
New-Item -ItemType Directory -Force build/studio-strum-auditions | Out-Null
lua scripts/procedural-synth-tests/export-strum-auditions.lua build/studio-strum-auditions
foreach ($source in @('guitar-steel','sampled-guitar-steel')) {
    foreach ($mode in @('reference','strum')) {
        & build/synth-core-tests/Release/score-audition.exe "build/studio-strum-auditions/$source-$mode.bin" "build/studio-strum-auditions/$source-$mode.tsv" files/server/optional-assets/fetcher.studio-samples "build/studio-strum-auditions/$source-$mode.wav" 0 9 all
    }
}
```

The original E minor / G / A minor / D progression contains 84 notes. The tool
delays 68 picks across 16 alternating gestures, preserving pitches, velocities
and note-off positions. Recorded playback needs the credited steel bank already
available in the optional sample assets. Compare each source before/after at
matched level; this demonstrates authored timing, not connected articulation.

## Original parallel-processing exercise (package 56)

```powershell
New-Item -ItemType Directory -Force build/studio-parallel-auditions | Out-Null
lua scripts/procedural-synth-tests/export-parallel-auditions.lua build/studio-parallel-auditions
cmake --build build/synth-core-tests --config Release --target score-audition
foreach ($mode in @('reference','chug','saw','bass')) {
    & build/synth-core-tests/Release/score-audition.exe "build/studio-parallel-auditions/$mode.bin" "build/studio-parallel-auditions/$mode.tsv" files/server/optional-assets/fetcher.studio-samples "build/studio-parallel-auditions/$mode.wav" 0 9 all
}
```

The original four-bar riff includes drums/bass. Reference disables only the
Chug preset's parallel blend; Chug enables it. Saw replaces the guitar source;
Bass keeps Chug and replaces the bass. All schedule the same 184 strikes.
Compare Reference/Chug at matched levels. Optional guitar/drums stems isolate
changes. No imported song, sample bank or external plugin is required.

## String expression exercises (package 55)

```powershell
New-Item -ItemType Directory -Force build/studio-string-auditions | Out-Null
lua scripts/procedural-synth-tests/export-string-auditions.lua build/studio-string-auditions
foreach ($source in @('generated','recorded')) {
    & build/synth-core-tests/Release/score-audition.exe "build/studio-string-auditions/$source.bin" "build/studio-string-auditions/$source.tsv" files/server/optional-assets/fetcher.studio-samples "build/studio-string-auditions/$source.wav" 0 9 all
}
```

Each original four-bar exercise plays the same notes as Open / Palm / Dead /
Open, two seconds per bar plus a one-second tail. No imported song is required.
The recorded example needs the optional credited electric-DI bank installed at
the supplied asset root. The generated example uses the native source.

## Recorded-score comparisons (package 53)

Use a local MIDI to render the actual compiled score/preset snapshots through
native Studio without opening the game. This does not normalize the output or
simulate room/spatial attenuation. Source MIDI files are not included here.

```powershell
cmake --build build/synth-core-tests --config Release --target score-audition
lua scripts/procedural-synth-tests/export-score-audition.lua 'C:/path/song.mid' recorded-thrash build/studio-band-auditions/song
& build/synth-core-tests/Release/score-audition.exe build/studio-band-auditions/song.bin build/studio-band-auditions/song.tsv files/server/optional-assets/fetcher.studio-samples build/studio-band-auditions/song.wav 60 16 all
```

The final arguments are seek seconds, duration seconds and a stem group:
`all`, `drums`, `guitar`, `bass` or `other`. Output includes raw peak/RMS and
scheduled strikes. Compare identical excerpts; short drum transients naturally
have lower average RMS than sustained guitars. These measurements establish gain
relationships and native scheduling, not perceived fidelity to a recording.

## Procedural sound foundation

Package 44 contains synthesized guitar/bass and full-kit starting sounds.
The following renders use the real native synth with the Lua preset parameters;
they do not use recorded instrument samples. Each patch plays a one-second
phrase/hit followed by 0.75 seconds of release. Guitar patches share a C-major
chord, bass patches share a single C note and kit pieces have their assigned
fixed tuning. The renderer prints patch order and raw/audition RMS values.

```powershell
New-Item -ItemType Directory -Force build/studio-band-auditions | Out-Null
lua scripts/procedural-synth-tests/export-band-auditions.lua build/studio-band-auditions
cmake -S scripts/procedural-synth-tests -B build/synth-core-tests
cmake --build build/synth-core-tests --config Release --target synth-tests
foreach ($family in @('electric','acoustic','bass','kit')) {
    & build/synth-core-tests/Release/synth-tests.exe --bank-preview-matched "build/studio-band-auditions/$family.tsv" "build/studio-band-auditions/$family.wav"
}
```

The comparison target is 0.06 RMS, limited by 0.8 peak headroom. Short percussive
attacks may therefore have lower average RMS. Comparison normalization affects
only the audition WAV, not the saved patch or in-game faders. Check full-band
mix balance and playing response separately before accepting a new sound library.

`band-sounds-player.lua` is an opt-in private two-client fixture driver. It checks
four GM string families plus ten drum pieces, one shared drummer, thirteen live
sound-set choices, a late observer, source-free server Save/Load/replay and NPC
cleanup. Keep this driver out of the normal package manifest and user fixture.
