# Character appearance editor

A server-distributed Lua package for changing an existing character's race, sex,
head and hair. Type **`/appearance`** in chat while standing on solid ground and
outside other menus. The same command closes an open editor; **Escape** also cancels.
F6 retains its normal chat-display binding.
The character faces the camera beside a panel styled with Morrowind's MWUI borders,
text colours and original button textures. Each selector has a searchable dropdown
with a filled textured scrollbar handle. The editor and selector title bars reuse
the inventory/chat header blocks, flush with the outer and inset body borders.
Left/right cycling buttons use the options
menu's solid textured arrows. **Left-drag the model view** to orbit horizontally or
vertically around the character's head/upper-body pivot, opposite to the drag
direction on both axes; **mouse wheel** zooms toward
or away from that pivot. The camera derives its look-at direction from the head's
actual position throughout the orbit. Its drag surface ends at the menu's left
edge, so raising that surface cannot cover any controls. Wheel over a dropdown scrolls its list instead. Panel/list
interactions do not start camera gestures. **Cancel / Escape** restores the original
appearance, camera and control switches. **Apply** waits for a server-confirmed save.

The editor reads the effective installed race/body-part records. It lists playable
races and playable, non-vampire skin heads/hair. **Heads / hair: Matching race**
filters parts by race and sex. Switch it to **All races** to freely combine any
playable head and hair, regardless of their source race or sex; those choices show
their source race/sex in the list. Returning to matching mode chooses compatible
parts. Saved mixed combinations reopen in All races mode and keep their parts.
The current race is retained even when a mod marks it non-playable. There is no
hardcoded race/mod list and no fixed number of choices. Dropdowns virtualize eight
rows, rather than creating one widget per record. Mods must provide normal TES3
race/body-part records and their assets; model-only transformations are not races.

Changes are cosmetic. The editor preserves attributes, skills, spells, class,
birthsign, inventory, equipment, saved location and other progression. It does not
rerun chargen or replace racial bonuses. Previews are local and are excluded from
outgoing full player syncs. The server validates applied race/body-part IDs against
its authoritative content, updates only four character columns, and broadcasts the
confirmed appearance to the owner and other players. Late base-info/chargen packets
cannot overwrite a confirmed edit. Camera/UI changes affect only the editing client.

## Installation

Use matching native client/server builds with OpenMW Lua API **143**. This package
needs the new player preview API, body-part slot metadata and server appearance API.

1. Install this folder as `server-lua-packages/fetcher.character-appearance`.
2. Install `apps/openmw-server/scripts/character_appearance.lua` in `server-scripts`.
3. Add `GLOBAL: character_appearance.lua` to the server's `server.omwscripts`, retaining
   its existing globals. The stock server manifest already includes that entry.
   The bundled `core.lua`/`custom_scripts.lua` command router registers
   `appearance_commands.lua` and `/help` lists `/appearance`. For a custom router,
   dispatch the command to that module's `handleChat(player, data, {commandPrefix='/'})`.
4. Restart the server after players save, then reconnect matching clients. The server
   delivers the package automatically; players do not install a separate Lua mod.

Existing server packages pin the exact OpenMW Lua revision. Update their
`requiredOpenMWLuaApi` to 143 when upgrading the matching client/server build;
the bundled package manifests already use that revision.

The dedicated-server script owns sessions and saves. The automatically delivered
global/player scripts forward messages and display each client's menu/camera.
No Bardcraft or Studio dependency is required. Set
`Config.CHARACTER_APPEARANCE_ENABLED = false` to disable opening the editor.

## Native API

- Client `types.Player.previewAppearance(self, {race=..., head=..., hair=..., isMale=...})`
  queues a local visual preview. Passing `nil` restores the latest server-confirmed
  baseline and exits preview mode. It requires an active, completed multiplayer
  character; it never commits to the server or recalculates progression.
- `types.BodyPart.records` exposes numeric `part`, with `types.BodyPart.PART.Head`
  and `.Hair` constants.
- Dedicated-server players expose `head`, `hair` and `charGenComplete` in addition
  to `race` and `isMale`. `player:setAppearance(selection, requestId)` queues a
  validated, character-bound save. The internal `AppearanceEditorApplyResult` event
  contains `guid`, `characterId`, `requestId`, `ok` and `error` after native processing.
  The package ignores client attempts to forge that internal completion event.

Other player scripts can use `openmw.interfaces.FetcherAppearance.open()` and
`.close()`. `.state()` returns a copy of the current draft plus its `pending` flag,
and the `allRaces` filter flag, or nil when closed. `.setAllRaces(boolean)` selects
the same matching/all-races filter as the UI. `.preview(selection)` validates and changes that draft locally;
`.apply()` submits it through the same server session. Closing is disabled while
a save is awaiting confirmation.

## Verification

Run `lua scripts/test_character_appearance.lua .` from the source root for catalogue,
session validation, dropdown, controller and camera restoration checks. Native
`PlayerAppearance.*` component tests verify record validation and cosmetic-only
SQLite persistence alongside the existing inventory/spellbook persistence tests.

Local client/server integration also checks preview/cancel, apply and reconnect,
unchanged base attributes/skills, restored camera state and a 2,000-entry selector.
The Lua checks cover model-centred horizontal/vertical orbit geometry, bounded zoom,
rendered facing direction at different actor headings, disjoint menu/drag bounds,
camera-offset restoration, cycling/wraparound and mixed-part catalogue persistence.
