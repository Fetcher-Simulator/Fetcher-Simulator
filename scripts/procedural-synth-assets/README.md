# Casio keyboard asset conversion

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
