# Appearance expansion validation — 2026-10-26

Matching local native client/server: OpenMW Lua API 144, multiplayer Lua API 12,
wire protocol 25. Appearance package 9; hotkeys package 5.

- Native client, dedicated server and component-test targets compile successfully
  with MSVC 2022, RelWithDebInfo.
- 31 component tests pass: appearance record validation, complete body data in
  base-info/reconnect packets, SQLite persistence without resetting progression,
  package validation/transfer/registry and handshake checks.
- `lua scripts/test_character_appearance.lua .`: 301 checks pass.
- `lua scripts/test_hotkeys.lua .`: 497 checks pass.
- Private server-delivered QA on the existing port-25567 fixture passes actual
  native body/rig/tail preview and cancel, skin selection, voice-search keyboard
  focus, focus release, GUI lifecycle, authoritative apply and racial power swaps.
- A server restart and client reconnect restore Breton identity, Argonian body,
  beast rig, hidden tail and selected skin meshes. Racial powers are present after
  reconnect. The original Synth A appearance is then restored through the normal
  server appearance action.
- Inventory, equipment, attribute, skill and learned-spell rows match the pre-test
  snapshot exactly. SQLite integrity check passes. Private QA package and routing
  helper are removed; original server script manifest and player settings remain.
- A final native build smoke test passes preview/cancel, rig/tail state, search
  focus and closing behavior before cleanup.

These checks exercise native renderer loading and UI callbacks; they do not claim
pixel inspection or automatic seam-free compatibility between arbitrary mod meshes.
The pre-existing initial character-loading RigGeometry warnings remain outside the
feature checks. Tail meshes need a compatible skeleton; use beast rig when needed.
