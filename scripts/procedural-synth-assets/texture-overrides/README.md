# Sheet-music texture adaptation

`cc_sheet-music_2.png` is the approved 1024 x 1024 page-color master, in the
original mesh's horizontally mirrored UV orientation. The two other material
textures are unchanged. The converter uses this master on subsequent imports.

Source: [Sheet Music by cScott](https://sketchfab.com/3d-models/sheet-music-d444261c68464623b1b2e08ec8ae62f3),
CC BY 4.0, with upstream notebook credit to Neo_minigan. Full notices remain in
the runtime bundle. Source GLB SHA-256:
`7b7a8fec4c798f3efae26f683668e4ce85a018a4f9e384c64cb8b639028bd7d6`.

Edited October 29, 2026 using Codex's built-in OpenAI image-generation tool
(not the fallback CLI). The original page atlas was horizontally flopped to a
readable view for editing; the selected square output was resized to 1024 x
1024 and flopped back for the existing UVs. No meshes or UV coordinates changed.
The runtime image is DXT1 DDS with mipmaps. The texture is decorative notation,
not a playable transcription or score used by the sequencer.

## Final editing prompt

```text
Use case: text-localization.
Asset type: existing 1024x1024 game texture of two aged sheet-music pages, exact UV layout must remain.
Input image 1 is the EDIT TARGET. Edit this exact image, keeping canvas square, both page edges, fold, paper color, stains and every staff, note, rest, clef, key signature, articulation and dynamic mark at exactly the same positions.
Remove all prose wording: dedication "To my Mother.", title "LULLABY.", composer "By Margaret Tuggle.", all eight lines of sung lyrics between the staves, copyright line at bottom left, small footer "Lullaby. 3", and page numbers. Heal each removed word with matching locally aged paper without touching notation.
Replace the old dedication/title block at upper left with the single exact word "FETCHER" (F E T C H E R), centered at x267 y126, roughly the same size and dark antique serif style as the original LULLABY title. No dedication, no subtitle, no other new text. Retain musical dynamics such as mf, p, f, mp and tempo markings as notation.
Constraints: This is a surgical texture edit, NOT a new image of a book. Keep exactly the existing flat two-page composition, geometry, alignment, texture details and notation. Do not crop, reframe, rotate, mirror, add perspective, move or invent musical notes. Preserve existing notation lines untouched and paper matched.
```
