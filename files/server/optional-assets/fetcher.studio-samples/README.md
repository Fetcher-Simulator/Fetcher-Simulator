# Fetcher Studio recorded instrument content

Original electric bank: FreePats FSBS Electric Guitar Direct, release 2022-09-11.
Source: https://freepats.zenvoid.org/ElectricGuitar/clean-electric-guitar.html
License: CC0 1.0 Universal. The original license and README are in `licenses/`.
This is a Fetcher adaptation; it does not imply endorsement by FreePats or Fender.

The original six banks require Studio package 49 / native synth API 19 or newer;
the two additional bass attack presets require package 71 (the bank format stays
compatible with native API 19). Add this folder as an OpenMW
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
Combined with electric DI, the original six banks reserve 238,012,916 decoded bytes (227 MiB)
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

## Package 71: captured finger-bass attacks

**Sampled Short Finger Bass** uses the central Darkblack staccato map, with one
recorded dynamic and two real alternates. **Sampled Ghost Finger Bass** uses its
soft/loud ghost maps (velocity 1-63 / 64-127), also with two real alternates.
Both cover concert MIDI 23-64, using 15 sampled roots and nearest-root zones.
The source's written-bass root correction is -12, as for regular bass. No detuned
unison copies or synthetic alternates are included. Original relative recording
levels are retained. The new presets add no pick excitation or modeled damping.

| Bank | Zones | Decoded PCM bytes | Encoded bytes |
| --- | ---: | ---: | ---: |
| `bass-short-v1` | 30 | 6,943,172 | 3,472,210 |
| `bass-ghost-v1` | 60 | 8,640,000 | 4,321,224 |

The eight-bank catalog totals 253,596,088 decoded bytes, leaving 14,839,368 bytes
inside the existing shared 256-MiB limit. Old files and identities stay intact.
These are ordinary gated v2 banks: note-off releases them and long gates cannot
extend their finite natural tails. They do not supply automatic per-note bank
selection, palm-mute/slap/pop recordings or release-trigger samples.

The converter retains 3 ms before onset above -65 dB, averages stereo to mono
48-kHz PCM16, caps short tails at 1.25 s and ghost tails at .75 s, and fades the
last 20 ms. All source/archive/output hashes and per-zone trims are in the new
manifests. CC0 source licenses are retained in `licenses/`.

```powershell
python scripts/procedural-synth-assets/build-bass-attack-banks.py <source-cache> build/synth-core-tests/Release/sample-bank-tool.exe files/server/optional-assets/fetcher.studio-samples
ctest --test-dir build/synth-core-tests -C Release -R recorded-bass-attacks --output-on-failure
```

Rebuilding either bank family preserves the other's catalog entries. The runtime
never evaluates SFZ; the offline converter reads only pinned explicit zone maps.

## Package 50: modeled note articulation (original banks)

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

## Package 72: original Guitar-TECHS captures and per-note selection

The new `guitar-techs-open-v1` / `guitar-techs-palm-v1` banks use original player 2
EVH Wolfgang neck-pickup direct input, from Guitar-TECHS (Pedroza et al., 2025),
https://zenodo.org/records/14963133 . The source is CC BY 4.0, verified in the
published dataset metadata and https://guitar-techs.github.io/ . Preserve
`licenses/guitar-techs-ATTRIBUTION.txt` and accompanying legal text. These banks
are not CC0; each catalog entry records its own license.

Thirteen sparse roots 40-86 cover keys 35-86; 35-39 transpose the low-E capture.
One actual performance per root supplies ordinary velocity scaling without
invented layers/round robins. The original MIDI selects the highest available
string for each pitch. Cuts find the first transient above a local noise floor
within 100 ms of each annotation, retaining 3-ms preroll. Dual-mono DI is averaged
to 48-kHz PCM16. Each root's initial strength is normalized with a .75 peak cap;
exact gains/cuts and hashes are in the manifests. Open tails cap at 3 s, palms
at .7 s, with a 20-ms end fade. No harmonic restoration or external plugin.

Encoded bytes: 3,744,284 open / 873,884 palm. Decoded PCM: 7,488,000 / 1,747,200.
All ten banks total 262,831,288 decoded bytes, below 256 MiB with 5,604,168 bytes
remaining. Large additional clips can still reach the budget. Old banks retain
their identities. API-30 patch references bind existing banks as open/short/dead
variants without duplicating their shared PCM buffers.

```powershell
python scripts/procedural-synth-assets/build-guitar-techs-banks.py <source-cache> build/synth-core-tests/Release/sample-bank-tool.exe files/server/optional-assets/fetcher.studio-samples
```

The converter requires numpy, mido and ffmpeg, verifies the published MD5s and
records SHA-256 provenance. Runtime evaluates no source scripts or SFZ. Existing
family converters preserve these separately curated catalog entries.

## Package 73: bridge pickup and reviewed C starter

`guitar-techs-bridge-open-v1` and `guitar-techs-bridge-palm-v1` adapt original
player 1 Ibanez PF300 bridge DI under the same CC BY 4.0 notice. They use the
same sparse roots, cut rules, 3-s/.7-s tails and actual-performance limits.
Rebuild with the above converter plus `--player P1`. Source MD5/SHA-256 pins,
annotations, exact cuts and conversion gains remain in source/manifests.

Captured Band / Thrash and the Bridge thrash / C exercise use these recordings,
8x pre-amp source calibration and the editable saved amp/channel EQ recipe. The
user accepted C as a clearer starter after rejecting the neck-pickup B render.
Old banks and saved references retain their identities.

The twelve-bank on-disk library totals 272,066,488 possible decoded PCM bytes.
It is loaded on demand, not made resident as a whole. The production reader's
256-MiB cap, unpinned-entry eviction and safe pinned-budget failure remain intact.
Regression checks enumerate each sound set's possible reference union and each
sound-check project, with all below 252 MiB. Large custom projects may still
reach the existing cap. No memory-budget increase accompanies the new content.
