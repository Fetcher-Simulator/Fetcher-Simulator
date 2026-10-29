# Fetcher Studio recorded instrument content

First bank: FreePats FSBS Electric Guitar Direct, release 2022-09-11.
Source: https://freepats.zenvoid.org/ElectricGuitar/clean-electric-guitar.html
License: CC0 1.0 Universal. The original license and README are in `licenses/`.
This is a Fetcher adaptation; it does not imply endorsement by FreePats or Fender.

Use with Studio package 48 and native synth API 18. Add this folder as an OpenMW
data directory, or copy its `sound/` tree into an existing data directory.
No content plugin is required. The local test fixture already uses its existing
`synth-assets` data directory. Public distribution through the launcher requires
including this content in the matching release bundle; Git/Lua changes alone do
not install the bank for other testers.

On Mixer, select **Sampled Electric Clean**, **Sampled Electric Crunch** or
**Sampled Electric Metal** in the sound dropdown. Synth's **CC Sampled Instruments**
and **Guitar** banks expose them too. All share recorded guitar and differ in
native amplifier/cabinet processing. Existing procedural presets remain available.

## Content and conversion

- `sound/fetcher/banks/electric-di-v1.fsb`: 34,061,890 bytes; 60 key/velocity zones,
  two recorded alternatives per source group, MIDI key coverage 35-86.
- The source's softer/harder velocity groups are retained where provided. Higher
  notes with a single source velocity group do not acquire invented layers.
- Two of the four original alternatives per group are retained and cycled in
  deterministic round-robin order. Other source recordings remain in the archive.
- Leading silence is aligned below -65 dB with 3 ms retained. Recordings are
  mono 48-kHz PCM16, capped at six seconds with a 20-ms cap fade. No per-layer
  level normalization removes the source dynamics.
- The native in-memory bank is 68,121,812 PCM bytes, shared by all referencing
  tracks/performers within a client. The bank is loaded once by the sample worker.
- `.fsb` here is Fetcher's bounded v1 format, not an FMOD bank or a general SFZ
  player. No SFZ, includes, macros or scripts are interpreted at runtime.
- There are no dedicated palm-mute/slide/legato recordings in this v1 adaptation.
  The metal preset is an amp sound; convincing articulation remains follow-up work.

`bank-manifest.json` records source/sample SHA-256 values, exact retained zones,
conversion choices, output SHA-256 and the engine's 128-bit file fingerprint.
The fingerprint matches the existing multiplayer sample-reference mechanism;
release asset integrity should continue to use SHA-256.

## Rebuild

Build `sample-bank-tool` from `scripts/procedural-synth-tests`, extract the pinned
FreePats archive outside the repository, then run:

```powershell
python scripts/procedural-synth-assets/build-electric-sample-bank.py <extracted-source> build/synth-core-tests/Release/sample-bank-tool.exe files/server/optional-assets/fetcher.studio-samples
```

The converter supports only the audited source's note/velocity groups and random
variants, verifies the resulting v1 identity, and refuses a changed output until
it is deliberately audited/versioned. Native `sample-bank-tool <bank> <preview.wav>`
renders a note/velocity audition; append `metal` for a distorted comparison.
