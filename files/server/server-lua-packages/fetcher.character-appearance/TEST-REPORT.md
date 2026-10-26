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

## Public deployment — 2026-10-26

The Windows Experimental workflow run `40395180394` compiled and packaged native
commit `ab140eecd6ba4f572369d8394d212623f3596d3b`, then failed during GitHub release
publication with HTTP 403. Its original artifact was recovered; all 691 managed
client files and 26 embedded tester-tool files matched their recorded inventories.
No replacement native compilation was needed. The publishing workflow now preserves
the existing release and publishes successfully before moving the release tag.

The recovered client passed private VPS tests with both actual content profiles.
Save/reopen, racial powers, server restart and reconnect passed with approved
base-game body meshes. A client-only mesh selection was correctly rejected by the
server; the QA harness now requires an explicit successful save response before
checking persistence. Original private-test appearances were restored afterward.

With the operator's save/restart approval, both public servers were gracefully
stopped at zero players. Fresh SQLite and Lua-storage backups were taken before
activating the reviewed server/runtime overlays. Both servers became healthy and
advertise protocol 25. All 26 protected player-data tables per server matched their
pre-cutover values: 53 Experimental characters and 4 Vanilla characters retained.
SQLite integrity checks pass. Server settings and content profiles are unchanged.
Experimental has 12 delivered Lua packages; Vanilla has 3, including Studio.
Appearance package 9 and hotkeys package 5 match the reviewed source on both.
No private QA package was installed in either public runtime.

Published release/tag: `Fetcher-Simulator-Experimental`; asset: `fetcher-simulator.zip`.
The superseded Vehicles compatibility release/tag was removed at the operator's
request. The canonical release contains the verified client from native commit
`ab140eecd6ba4f572369d8394d212623f3596d3b`.

- Client ZIP SHA-256: `c2345907e3ece14dd9a3420fff7b9a357b9b50a93f57251fe338b774b014538b`
- Server SHA-256: `ac29556aa5b3714bf6daa29af82021a2136e4b16fae3a8b5e8d39ed62ac6025d`
- Defaults SHA-256: `fb6a74897ab2672ab1383991338a0982b4796e08645fcc360fe8e208e92268b4`

The normal local installation updated through Fetcher Launcher, verified all 691
managed files and preserved all 131 checked personal settings/save/key files. It
joined both public servers through launcher discovery, accepted their handshakes,
activated server Lua and restored existing character state. No appearance saves
were made on public characters. The test clients were closed and the previous
launcher selection restored. Existing unrelated mod errors in the heavily modded
profile remain outside this validation.

Private evidence: local `build/appearance-v9-rollout`, recovered-artifact inventory
and private-client reports under `build/appearance-v9-vps-review`; VPS
`/srv/fetcher-release-staging/20261026-appearance-body/{cutover-report,final-audit}.json`.
Fresh rollback data is retained under
`/srv/fetcher-release-backups/20261026-appearance-body/cutover`.

Existing testers should close the game and choose **Updates → Install updates**
in Fetcher Launcher before joining either server.
