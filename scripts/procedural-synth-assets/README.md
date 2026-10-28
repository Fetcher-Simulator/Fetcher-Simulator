# Fetcher Studio instrument assets

## Original orchestral props

`generate_orchestra.py` creates original low-poly violin, cello, bow, trumpet,
flute, clarinet, timpani and mallet meshes, small generated material textures,
and transparent inventory icons. They use no downloaded or game-owned geometry
or textures. Credit/license details are in `ORCHESTRA-ASSETS.txt`; the existing
Sketchfab keyboard/stand credits remain in the same bundle.

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.5/blender.exe' --background --factory-startup --disable-autoexec --python-exit-code 1 --python scripts/procedural-synth-assets/generate_orchestra.py -- --output files/server/optional-assets/fetcher.procedural-synth --working build/orchestra-assets
```

Requires Blender 4.5 with `io_scene_mw` and ImageMagick. Export verifies triangle
counts and resolves serialized relative texture paths. The generator records
bounds/counts in `orchestra-report.json`, preserves existing notices and refreshes
the whole bundle's SHA-256 manifest. Native in-game inspection verifies exported
geometry/materials and hand alignment; source renders alone are insufficient.
Props are static meshes moved by the Lua performance profiles. The cello's
standing stance is provisional until seated layouts are implemented.

## Casio keyboard asset conversion

Source: **Keyboard (Casio VL-Tone)** by **AleixoAlonso**, Sketchfab UID
`be22fbe1ef0044f3bedc6e3edf459593`, licensed CC BY 4.0. See the runtime bundle's
`ATTRIBUTION.txt`, `LICENSE-CC-BY-4.0.txt` and `provenance.json` for links and changes.
Original downloads and Blender working files stay outside the repository.

Requirements: Python 3.9+, Blender 4.5 with the `io_scene_mw` add-on installed,
and ImageMagick (`magick` on PATH). Commands from the repository in PowerShell:

```powershell
$work = 'C:\serena_workspaces_directory\synth-assets\casio-vl-tone'
$assets = "$PWD\files\server\optional-assets\fetcher.procedural-synth"
python scripts/procedural-synth-assets/prepare_source.py 'C:\Users\REPTILE\Downloads\keyboard-casio-vl-tone.zip' $work
& 'C:\Program Files\Blender Foundation\Blender 4.5\blender.exe' --background --factory-startup --disable-autoexec --python-exit-code 1 --python scripts/procedural-synth-assets/convert_keyboard.py -- --source "$work\original" --output $assets --working "$work\working"
python scripts/procedural-synth-assets/package_assets.py --source "$work\original" --output $assets --working "$work\working" --zip "$work\output\fetcher-procedural-synth-assets.zip"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-procedural-synth-test.ps1
```

Extraction checks archive paths and records the source ZIP's SHA-256. First extraction
requires the official public Sketchfab API to confirm the license. Subsequent runs
preserve existing provenance only when the input archive hash matches.

Conversion imports the FBX, retains the original UVs and separate key/button meshes,
centers it horizontally, puts the bottom at Z=0, and scales it to 48 units wide.
It converts the original color/normal textures to mipmapped DDS and adapts PBR
materials to two legacy NIF materials. Metalness and roughness are not represented.
The model is static; keys are not animated when playing.

Validation reads the actual NIF, checks triangles, collision and relative texture
paths, reimports it to verify bounds, and renders `working/keyboard-nif-preview.png`.
Packaging checks NIF/DDS headers, creates notices and a SHA-256 manifest, and writes
an assets-only ZIP. The sibling `.gitattributes` preserves bundle bytes across Git
checkouts so line-ending conversion cannot invalidate the manifest.

Use the fixture's two batch launchers after setup. `-LiveProbe` enables the existing
scripted two-client editor test; rerun setup without it before interactive use.
`-VanillaSynth` explicitly selects the vanilla lute fallback. Stop the fixture's
server, clients and crash-monitor processes before rerunning setup.

## Combined Yamaha keyboard and X-stand

The default fixture now uses `meshes/fetcher/synth/keyboard-stand.nif`.
The older Casio commands above remain a separate conversion workflow.
Source downloads: `yamaha-keyboard.zip` and `keyboard-x-stand.zip`, supplied by
user, with original source files and metadata kept outside the repository at
`C:\serena_workspaces_directory\synth-assets\yamaha-stand\original`.
Source identities, verified CC BY 4.0 license URIs and ZIP hashes are recorded in
the runtime bundle's `stage-provenance.json`; credit both model authors.

Extract each download into its own named directory under `original`; extract the
Yamaha's nested `source/exportpaino.zip` into `source/exportpaino`. Keep root texture
folders intact. `original/yamaha-keyboard/provenance.json` and
`original/keyboard-x-stand/provenance.json` contain the official model metadata.
See `combine_stage.py` for the validated source filenames and expected identities.

```powershell
$stage = 'C:\serena_workspaces_directory\synth-assets\yamaha-stand'
& 'C:\Program Files\Blender Foundation\Blender 4.5\blender.exe' --background --factory-startup --disable-autoexec --python-exit-code 1 --python scripts/procedural-synth-assets/combine_stage.py -- --source "$stage\original" --output "$stage\converted" --working "$stage\working"
python scripts/procedural-synth-assets/package_stage.py --source "$stage\original" --converted "$stage\converted" --working "$stage\working" --output "$PWD\files\server\optional-assets\fetcher.procedural-synth" --zip "$stage\fetcher-synth-keyboard-stand.zip"
```

The stand's ASCII FBX uses the restricted static reader `read_static_fbx.py`;
unsupported transforms or layouts fail rather than silently dropping geometry.
The keyboard uses its OBJ export and four mapped material roles. Conversion sets
floor Z=0 and key height near 89, creates seven collision boxes, exports NIF
4.0.0.2, then reimports it and validates texture paths, triangles and bounds.
The report counts 12,189 visual triangles after one zero-area source face is
removed; collision adds 84. Packaging retains the older Casio files, updates the
SHA-256 manifest and writes an assets-only ZIP. Source downloads are not bundled.


## Inventory icon

`generate_orchestra.py` creates the original orchestral models and icons. Bowed
models use a closed tapered neck/heel supporting the ebony fingerboard; validation
reads the exported NIF vertices and rejects a gap. The provisional standing cello
uses a longer endpin paired with its raised performance profile. Regenerate the
asset manifest after changing these meshes, preserving existing model credits.
Use `--only trumpet` to regenerate one model/icon while retaining the other
models' report entries. The trumpet's folded tubing occupies Y/Z, its valve row
runs along Y, and its pistons press along Z. Export validation checks actual NIF
geometry/transforms for these independent axes.

`icons/fetcher/synth/keyboard.dds` is a 128 x 128 DXT5 icon with alpha and a
complete mip chain. It is rendered from the actual combined keyboard/stand NIF;
its source model attribution is retained in the optional asset bundle.

```powershell
$assets = "$PWD/files/server/optional-assets/fetcher.procedural-synth"
& 'C:/Program Files/Blender Foundation/Blender 4.5/blender.exe' --background --factory-startup --disable-autoexec --python-exit-code 1 --python scripts/procedural-synth-assets/render_inventory_icon.py -- --assets $assets --output "$PWD/build/keyboard-inventory-icon.png"
magick build/keyboard-inventory-icon.png -resize 128x128 -alpha on -strip -define dds:compression=dxt5 -define dds:mipmaps=7 "$assets/icons/fetcher/synth/keyboard.dds"
```

Re-run `package_stage.py` after changing bundle bytes to regenerate the manifest
and asset ZIP. Setup verifies the manifest before installing either model or icon.
