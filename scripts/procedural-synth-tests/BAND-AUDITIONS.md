# Band sound foundation auditions

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
