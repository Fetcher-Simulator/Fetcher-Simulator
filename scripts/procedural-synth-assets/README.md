# Fetcher Studio instrument assets

## Credited orchestral and stage models (package 41)

The runtime bundle replaces the generated violin/bow, cello, trumpet, flute,
clarinet, timpani and beater with CC BY 4.0 Sketchfab models. A compact piano
stool supports seated keyboard/cello NPCs, and non-keyboard NPCs use a music
stand. Open sheet booklets are joined into the holders. The existing credited
Yamaha/X-stand and vanilla lute/drum remain. `sketchfab-sources.json` pins the
ten source identities and manually downloaded GLB filenames; the runtime
`sketchfab-models.json` records creators, source/license URLs, source hashes and
adaptations. Preserve `ATTRIBUTION.txt`, `ORCHESTRA-ASSETS.txt` and the CC license
when redistributing. Future third-party models should use documented CC0 or CC BY
sources, with provenance/credits recorded before conversion.

Requires Python, Blender 4.5 with `io_scene_mw`, and ImageMagick. Keep source GLBs,
official API metadata and working files outside the repository. The preparation
step verifies official downloadable/CC BY metadata and hashes local downloads;
it does not authenticate or download through a user's browser.

```powershell
$source = 'C:/serena_workspaces_directory/synth-assets/sketchfab-orchestra-2026-10-28'
$assets = "$PWD/files/server/optional-assets/fetcher.procedural-synth"
python scripts/procedural-synth-assets/prepare_sketchfab_sources.py --downloads 'C:/Users/REPTILE/Downloads' --output $source
& 'C:/Program Files/Blender Foundation/Blender 4.5/blender.exe' --background --factory-startup --disable-autoexec --python-exit-code 1 --python scripts/procedural-synth-assets/convert_sketchfab_orchestra.py -- --source $source --output $assets --working "$source/converted"
& 'C:/Program Files/Blender Foundation/Blender 4.5/blender.exe' --background --factory-startup --disable-autoexec --python-exit-code 1 --python scripts/procedural-synth-assets/make_seated_keyboard.py -- $assets
& 'C:/Program Files/Blender Foundation/Blender 4.5/blender.exe' --background --factory-startup --disable-autoexec --python-exit-code 1 --python scripts/procedural-synth-assets/dress_music_holders.py -- $assets
python scripts/procedural-synth-assets/finalize_sketchfab_bundle.py --assets $assets
& 'C:/Program Files/Blender Foundation/Blender 4.5/blender.exe' --background --factory-startup --disable-autoexec --python-exit-code 1 --python scripts/procedural-synth-assets/validate_sketchfab_bundle.py -- $assets
```

Conversion retains UVs, adapts PBR materials to legacy NIF, limits textures to
1024 pixels with DDS mipmaps, and renders inventory icons. The violin/bow split
retains every source triangle exactly once. The percussion beater is a wooden
drumstick, not a felt-headed mallet. `--only violin` can reconvert one source;
always finalize and validate afterward. The seated Yamaha lowers the stand by
24 units while retaining key/body thickness and all 12,189 visual triangles.
It omits collision boxes because it is a visual attachment. The holder-dressing
step joins one 212-triangle sheet booklet to each holder, preserving all original
geometry, materials and collision. It replaces its named subtree on repeated
runs. The seated conversion removes that subtree before lowering the keyboard,
so a complete repeated pipeline does not accumulate pages. Sheet-music credits
retain cScott and the upstream notebook author Neo_minigan.

The sheet-page texture uses the approved master
`texture-overrides/cc_sheet-music_2.png`. Conversion applies this override instead
of the original GLB's printed page image. It removes lyrics and printed credits
and replaces the dedication/title with FETCHER, retaining notation and aged paper.
The master retains the original mirrored UV orientation; flop it horizontally
for a readable preview. See `texture-overrides/README.md` for the editing prompt
and provenance. All three holders reference the same DDS texture.
The Yamaha booklet is centered on the rest at X=3.115, tilted 76 degrees and
scaled to 86 percent to fit behind its lip and below its top edge. Its origin is
(3.115, 9.28, 94.5) standing, or Z=70.5 seated; the separate music stand keeps its
own fit.

Validation reads actual NIF/DDS bytes, checks source-report triangle counts,
relative resolved texture paths, finite vertices, mipmaps, credits and manifest
hashes. Native in-game views are still required for material/grip acceptance.
`generate_orchestra.py` is retained as a historical generator; it refuses to
overwrite a bundle containing the new Sketchfab registry. Use a separate output
directory for historical experiments.

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

The Sketchfab conversion renders orchestral/stage icons from converted geometry.
Use the pipeline above when replacing those files and refresh the manifest.

`icons/fetcher/synth/keyboard.dds` is a 128 x 128 DXT5 icon with alpha and a
complete mip chain. It is rendered from the actual combined keyboard/stand NIF;
its source model attribution is retained in the optional asset bundle.

```powershell
$assets = "$PWD/files/server/optional-assets/fetcher.procedural-synth"
& 'C:/Program Files/Blender Foundation/Blender 4.5/blender.exe' --background --factory-startup --disable-autoexec --python-exit-code 1 --python scripts/procedural-synth-assets/render_inventory_icon.py -- --assets $assets --output "$PWD/build/keyboard-inventory-icon.png"
magick build/keyboard-inventory-icon.png -resize 128x128 -alpha on -strip -define dds:compression=dxt5 -define dds:mipmaps=7 "$assets/icons/fetcher/synth/keyboard.dds"
```

After changing the combined bundle, run `finalize_sketchfab_bundle.py` and its
validator. The older `package_stage.py` is specific to the original Yamaha/stand
conversion; preserve the new registry/models when packaging a complete bundle.
