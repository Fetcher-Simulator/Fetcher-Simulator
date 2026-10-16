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
