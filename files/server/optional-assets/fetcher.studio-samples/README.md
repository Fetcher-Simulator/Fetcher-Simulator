# Fetcher Studio recorded instrument content

Original electric bank: FreePats FSBS Electric Guitar Direct, release 2022-09-11.
Source: https://freepats.zenvoid.org/ElectricGuitar/clean-electric-guitar.html
License: CC0 1.0 Universal. The original license and README are in `licenses/`.
This is a Fetcher adaptation; it does not imply endorsement by FreePats or Fender.

The six banks require Studio package 49 / native synth API 19 or newer. Add this folder as an OpenMW
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
The original metal preset is an amp sound; additional articulations are described below.

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

## Package 49: recorded band

Mixer's **Recorded Band / Clean**, **Recorded Band / Rock** and **Recorded Band /
Metal** sound sets opt in to recorded instruments by MIDI program. Existing
projects and older sound sets retain their sounds. The **CC Sampled Instruments**
preset bank also offers **Sampled Finger Bass**, **Sampled Picked Bass**,
**Sampled Classical Nylon**, **Sampled Acoustic Steel** and **Sampled Acoustic
Drum Kit**. Drums → Kit → **Recorded Acoustic**, then **+ Kit**, creates ten
independently mixed lanes sharing the same recorded drum bank.

| Bank | Source | Zones | MIDI range | Decoded PCM bytes |
| --- | --- | ---: | --- | ---: |
| Finger bass | Karoryfer Black and Blue Basses, Darkblack | 60 | 23–64 | 34,560,000 |
| Picked bass | Karoryfer Black and Blue Basses, Babyblue | 60 | 23–63 | 34,560,000 |
| Classical nylon | Meta Open Sound | 48 | 40–83 | 27,900,376 |
| Acoustic steel | Meta Open Sound | 48 | 40–84 | 31,398,728 |
| Acoustic kit | Karoryfer Big Rusty Drums | 120 | GM pieces below | 41,472,000 |

All are CC0 1.0, with original license text in `licenses/`. Source links, pinned
archive hashes, individual sample hashes, zone maps and output identities are in
`catalog.json`, `manifests/` and the repository's `sample-sources.json`.
Combined with electric DI, all banks reserve 238,012,916 decoded bytes (227 MiB)
inside the shared 256-MiB PCM budget. PCM is shared across referencing lanes and
performers; it is not copied once per track. Encoded content totals about 114 MiB.

The bass banks use regular finger/pick plucks with two velocity layers and two
real alternate recordings. Source maps use written bass pitches an octave above
the recorded fundamental; our roots are lowered by 12, verified against decoded
recordings. Nylon and steel retain three recorded velocity layers with sparse
roots across the stated ranges. They do not invent alternate takes. Nylon's
reference velocity crossfades become three hard splits. String-specific SFZ
key switches, release triggers and per-string voice limits are not imported;
normal gated polyphony supports keyboard/MIDI chords.

The kit maps 35/36 kick, 37 side stick, 38/40 snare, 41/43 floor tom, 45/47 mid
tom, 48/50 high tom, 42/44/46 closed/pedal/open hat, 49 crash, 51 ride, 53 ride
bell, 52 china, 57 sizzle crash and 59 sizzle ride. Three selected velocity layers
and two real alternates use close microphones (snare top). All hits are native
one-shots: note-off does not cut the tail; repeated hits overlap. Closed/pedal
hats choke open hats across lanes sharing this bank in the same native output.
Separate outputs/bands and other banks retain independent tails. Unrecorded GM
pieces such as clap/cowbell/tambourine retain synthesized fallbacks in recorded
sound sets. Acoustic/fretless/slap/pop/synth bass programs also retain their
existing sources; these recordings do not represent those techniques.

The v2 bank adds per-zone one-shot and bounded choke groups. Runtime still reads
only bounded PCM/zone data, never SFZ or external paths. Bass tails are capped
at 3 s, acoustic guitars at 3.5 s, and drum pieces at 0.6–5 s. The converter
retains 3 ms before the first sample above approximately -65 dB and fades the
last 20 ms. It averages stereo to mono. Original relative layer levels remain;
presets provide whole-instrument gain compensation. Dedicated palm mutes,
release samples, strumming/legato controls and listening-based genre balancing
remain follow-up work. Dedicated guitar/bass/drum-kit props are also separate
work; audio content does not replace existing physical models.

To rebuild the five added banks, place the pinned archives and extracted SFZ
metadata in a source-cache folder, then run:

```powershell
python scripts/procedural-synth-assets/build-band-sample-banks.py <source-cache> build/synth-core-tests/Release/sample-bank-tool.exe files/server/optional-assets/fetcher.studio-samples
```

The converter validates source archives against the tracked hashes and checks
the production reader's output. `sample-bank-tool <bank> <preview.wav> bass`,
`acoustic` or `drums` renders an appropriate native audition phrase.

## Package 50: modeled note articulation

Studio package 50 / native API 20 adds modeled damping to gated guitar/bass bank
notes. **Palm mute (modeled)** controls the amount, **Muted decay** its decay
time, and **Muted tone** its darkness. The model operates before amplification;
short AMP releases give the muted presets tight note-off behavior. It reuses
the existing string-control parameters and supports Auto-page curves. Zero mute
is an exact bypass. One-shot percussion and ordinary single samples bypass this
model, and the encoded banks/fingerprints above are unchanged.

**Sampled Tight Metal Rhythm**, **Sampled Muted Rock Rhythm**, **Sampled Muted Funk
Guitar** and **Sampled Electric Lead** share the original recorded DI bank with
different damping/amp settings. **Recorded Band / Funk** joins the recorded band
sets; GM program 28 selects the appropriate muted preset. These model a damped
recorded note; dedicated muted/release/slap/slide recordings remain follow-up.
For original riff/power-chord comparison renders, append `riff` after a tool
mode (`metal`, `metal-muted`, `rock`, `rock-muted`, `funk`, `funk-muted`, `lead`).
