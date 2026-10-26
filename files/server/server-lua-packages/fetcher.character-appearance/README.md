# Character appearance editor

A server-distributed Lua package for changing an existing character's identity race,
body race, sex, head, hair, skin parts, animation rig and tail. Type **`/appearance`** in chat while standing on solid ground and
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

The **Character / Identity** page chooses identity race (voice catalogue and racial
powers/abilities), sex, head and hair. **Body / Rig** chooses the body race (default
skin and proportions), automatic/humanoid/beast animation rig, tail visibility,
and individual skin meshes from any installed playable race/sex. Select a body
slot, then its skin mesh; Automatic restores that slot's body-race default.
Hands, arms and legs apply to both sides. Neck, chest, groin and clavicles are also
available. Equipped armor and clothing retain their normal coverage priorities.

Identity and visual body are independent: a Breton identity can use an Argonian
body and beast animations. Tail visibility does not create a mesh: choose a tail
skin explicitly if the body race has none. Tail meshes require a compatible tail
bone; select Beast rig if a humanoid skeleton lacks one. Mixing meshes from
different bodies or mods can produce seams/clipping and is not automatic retargeting.

Applying an identity replaces the previous race's powers/abilities, retaining
shared powers and birthsign powers. Learned spells, earned attributes, skills,
level, class, birthsign, inventory and equipment are preserved. Character creation
and starting-stat calculations are not rerun. Previews do not grant racial powers.
Voice playback remains checked against server-confirmed identity on every use.

Previews are local and excluded from outgoing full player syncs. Cancel restores
the complete original body, rig and tail as well as race/head/hair. The server
validates every selected record, mesh kind and model, saves identity and body in
one character-database update, then broadcasts the result to all clients.
Reconnects and late joiners receive the same presentation. Unsolicited base-info
packets cannot overwrite it. Opening clears chat keyboard focus; dropdowns focus
their search fields and release focus when closed.

## Installation

Use matching native client/server builds with OpenMW Lua API **144**. This package
needs the extended player appearance API, body-part slot metadata and server appearance API.
The multiplayer wire protocol is 25; older native clients cannot join this build.

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
`requiredOpenMWLuaApi` to 144 when upgrading the matching client/server build;
the bundled package manifests already use that revision.

The dedicated-server script owns sessions and saves. The automatically delivered
global/player scripts forward messages and display each client's menu/camera.
No Bardcraft or Studio dependency is required. Set
`Config.CHARACTER_APPEARANCE_ENABLED = false` to disable opening the editor.

## Native API

- Client `types.Player.getAppearance(self)` returns a copy of the rendered selection.
- `types.Player.previewAppearance(self, selection)` queues a local preview; `nil`
  restores the latest server-confirmed baseline. Requires completed multiplayer
  character creation. The four existing fields are `race`, `head`, `hair`, `isMale`.
  Optional fields are `bodyRace` (empty means identity), `rig` (0 automatic, 1 humanoid,
  2 beast), `tail` (boolean), and `parts` (a map of skin IDs; empty means automatic).
  Part keys: `neck`, `chest`, `groin`, `hands`, `wrists`, `forearms`, `upperarms`,
  `feet`, `ankles`, `knees`, `upperlegs`, `clavicles`, `tail`.
- `types.BodyPart.records` exposes `part`, `isFirstPerson` and `PART` constants for
  all BODY mesh kinds. Whole-body choices exclude first-person-only meshes.
- Dedicated-server players expose `race`, `head`, `hair`, `isMale`,
  `charGenComplete` and `appearance` (bodyRace/rig/tail/parts).
  `player:setAppearance(selection, requestId)` queues a validated, character-bound
  save. Internal `AppearanceEditorApplyResult` reports guid, characterId, requestId,
  ok and error. Client attempts to forge that completion are ignored.
- `ui.setFocus(element)` focuses an element's real input widget; `nil` clears focus.

Other player scripts can use `openmw.interfaces.FetcherAppearance.open()` and
`.close()`. `.state()` returns a copy of the current draft plus its `pending` flag,
the `allRaces` filter flag and page name, or nil when closed. `.setAllRaces(boolean)` selects
the same matching/all-races filter as the UI. `.page("character"|"body")` switches pages. `.preview(selection)` validates and changes that draft locally;
`.apply()` submits it through the same server session. Closing is disabled while
a save is awaiting confirmation.

## Verification

Run `lua scripts/test_character_appearance.lua .` from the source root for catalogue,
session validation, dropdown, controller and camera restoration checks. Native
`PlayerAppearance.*` component tests verify record validation, packet round trips and
SQLite persistence alongside the existing inventory/spellbook persistence tests.

Local client/server integration also checks preview/cancel, apply and reconnect,
unchanged base attributes/skills, restored camera state and a 2,000-entry selector.
The Lua checks cover model-centred horizontal/vertical orbit geometry, bounded zoom,
rendered facing direction at different actor headings, disjoint menu/drag bounds,
camera-offset restoration, cycling/wraparound and mixed-part catalogue persistence.
