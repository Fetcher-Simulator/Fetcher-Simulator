# Fetcher Hotkeys

A server-delivered Lua hotbar and voice browser, styled with Morrowind's MWUI
text, borders and button frames. No Bardcraft, Studio or Inventory Extender
dependency. The server sends the client Lua automatically when players connect.
Browser windows use the original inventory/chat title-block textures, with matching
filled scrollbar handles. Their title bars sit flush against the outer and inset
body borders, using the native Morrowind window margins. Paging uses the options menu's solid textured arrows.
The HUD hotbar keeps its plain page label and has no textured title bar.

## Playing and assigning

- Ten slots use **1–9, 0** by default. Item shortcuts run the normal item-use
  action (equip, drink, read, etc.). They reference inventory records and do not
  move or consume anything when assigned. Missing items remain assigned.
- Open inventory, pick up an item on the cursor, then drop it onto a hotbar slot.
  Both the vanilla inventory and the optional Inventory Extender are supported.
  **`/hotbar`** also opens a searchable inventory palette with drag assignment.
- **`/voicelines`** opens a searchable, scrollable list drawn from the server's
  actual `/s` catalogue for your current race/sex, plus its globally available
  lines. Only files installed in your effective VFS are shown. Click **Preview**
  or a row to hear it locally. With **Auto: On**, scrolling or using the arrow
  keys auditions the selected line after a short debounce; previews replace
  each other. **Stop**, Close or Escape stops the preview.
- Drag a voice row down into a hotbar slot. Press its slot key during gameplay
  to play it from your character through the existing multiplayer speech path.
  Nearby players hear it; it obeys the same server cooldown as `/s`. The server
  rechecks access each time, so changing race/sex cannot retain an old voice's
  playback access. Reopen the browser to see the current list.
- Click the small **x** on a slot to clear it. Drag between slots to copy an assignment.
  Escape closes the browser. An inventory window that was already open stays open.

## Options → Scripts → Fetcher Hotkeys

There are **three pages by default**, adjustable from 1 to 16. **Page Up / Page
Down** changes the base page. Holding **Left Ctrl + Left Shift** shows the page
one ahead; holding **Left Ctrl + Left Alt** shows the page two ahead. Release
the combination to return to the base page. Pages wrap, and +2 takes precedence
when both combinations are held. These are temporary offsets, not key-repeat
page cycling. Paging also works while assigning items, outside text fields.

The settings page lets you rebind both paging keys, each key in both two-key
combinations, and all ten slot keys. Click a binding, then press its new key;
Escape cancels. It also controls HUD size, vertical offset and enable/disable.
When enabled this hotbar replaces native quick-key activation to prevent both
systems from firing together. Disabling it or leaving the server restores the
native quick keys. Hotkeys never activate items/voices while typing chat or
while another menu is open.

Assignments, page count and base page persist **per character in the server's
Lua storage** (`FetcherHotkeys`, keyed by the native character ID). Reducing the
page count hides excess pages without deleting their assignments. Device key
bindings and HUD preferences use local permanent player storage.

## Server installation

Requires matching client/server builds with **OpenMW Lua API 143**, multiplayer
Lua API 12. Install this folder in `server-lua-packages/fetcher.hotkeys` and ship
the current `hotkeys.lua`, `hotkeys_commands.lua`, `speech_commands.lua` and the
stock command-router updates in `server-scripts`. Add `GLOBAL: hotkeys.lua` to
`server.omwscripts`. The stock release already contains these entries. A custom
router can call `require('hotkeys_commands').handleChat(player,data,env)` for
`/hotbar` and `/voicelines`; permit both commands in any command allowlist.

Upgrade exact `requiredOpenMWLuaApi` pins in existing packages to 143 along with
the matching client/server binaries. Restart the server and reconnect clients
after installation. No public-server deployment happens merely by building this
package locally.

The native bridge adds player-context `ui.getDraggedItem()` (only a stack already
owned by the local player), `ui.cancelItemDrag(expectedItem)` (cancel only if the
same stack is still dragged), and `ui.setQuickKeysEnabled(boolean)`; it never
transfers items. `ui.getMousePosition()` returns GUI pixel coordinates for
captured drag releases. `input.isTextInputActive()` guards native chat/text entry.
Native quick-key suppression resets on world clear and Lua disconnect/inactivity.

Other player scripts can use `I.FetcherHotkeys.openVoices()`, `.openItems()`,
`.close()`, `.state()`, `.catalogue()`, `.bind(index,binding)`, `.activate(slot)`,
`.cycle(delta)`, `.setPageCount(count)`, `.previewVoice(id)` and `.stopPreview()`.
Assignments are `{kind='item'|'voice',
id=recordId|cataloguePath}`; indices are 1–160. The server derives the caller from
the authenticated packet sender, binds sessions to the character and rate-limits
requests. No chat commands, script strings, URLs or arbitrary sound paths can be
bound as actions.

Run `lua scripts/test_hotkeys.lua .` for model, server, GUI and controller checks.
