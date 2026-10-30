# Captured cabinet responses

Author: Bastian Karschewski / Jester Dyne Productions.
Source: [Jester's Brutal Pack 1.0](https://www.jester-dyne-productions.com/brutal-ir-pack/).
License: **CC0 1.0**, stated on page 3 of the included original handbook.

The three original 48 kHz mono PCM24 recordings and source handbook are retained
here for provenance and reproducible preparation. `catalog.json` records the
original archive/file SHA-256 hashes and processing metadata. Runtime playback
uses the embedded kernels in `apps/openmw/mwsound/cabinetirdata.hpp`; it requires
no external plugin, download or audio-thread file access.

| Cabinet setting | Recording | Speaker / microphone |
| --- | --- | --- |
| 4 | 1_Cookie_Monster.wav | Celestion Vintage 30 / Shure SM57 |
| 5 | 2_Darth_Genocider.wav | Eminence DV-77 / Shure SM57 |
| 6 | 3_Kitten_Slayer.wav | Celestion Rockdriver Junior / Sennheiser e606 |

The creator's cabinet is a modified oversized Behringer BG412S. Names describe
the supplied captures and do not imply endorsement or reproduction of a
particular artist's recorded sound. Each embedded kernel uses the first 2,048
frames (42.67 ms), a final 128-frame fade, and constant gain normalization over
120–6,000 Hz. At least 99.59% of each original response's energy remains before
the tail fade. Longer captured room ambience is intentionally excluded from this
cabinet stage; Fetcher's room processing remains separate.

To reproduce with Python, numpy and scipy:

```powershell
py -3 scripts/prepare-studio-cabinet-irs.py path/to/JestersBrutalPack_1.0.zip
```

The preparation tool verifies the pinned archive hash before reading its files.
