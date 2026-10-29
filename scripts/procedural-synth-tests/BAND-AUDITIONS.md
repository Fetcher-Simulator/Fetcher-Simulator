# Band sound foundation auditions

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
