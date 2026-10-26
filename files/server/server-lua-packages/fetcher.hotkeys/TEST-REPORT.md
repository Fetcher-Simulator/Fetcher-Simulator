# Hotkeys verification — 2026-10-25

- Built the native Windows client and dedicated server with Lua API 143.
- `lua scripts/test_hotkeys.lua .`: **473 checks passed**. Covers page wrap and
  held-key precedence, preserved hidden pages, server sender/character binding,
  catalogue precedence and race/sex access, cooldowns, rejected actions/paths,
  drag hit testing, virtualized search/scroll, local settings, chat/menu guards,
  repeated-key suppression, normal item-use dispatch and UI-mode restoration.
- Native package/transfer/registry tests: **28 passed**. Shipped-package registry
  checks now use the current API pin and Inventory Extender package version 25.
- Existing appearance checks: **255 passed**. Existing inventory drag-routing
  and authoritative partial/full cursor tests passed.
- Real client/server integration on the existing **127.0.0.1:25567** fixture:
  server-delivered package loading, `/voicelines`, populated native GUI, local
  sound preview, item/voice assignment, page count changes, hidden-page retention,
  browser switching, voice playback through `speech_commands.playPath`, menu
  activation guard and restoration of the original page/slots all passed.
- A second integration check saved a voice shortcut, shut down the dedicated
  server cleanly, restarted it, reconnected and verified that the shortcut
  persisted. The test then restored the original empty slot.
- Client logs from the final run contained no Lua errors. Physical mouse/key
  gestures are covered by simulated input callbacks; manual play remains useful
  for judging layout and input feel.
- Temporary QA package/scripts were removed. The original local server config
  was restored byte for byte. SQLite integrity is `ok`; character inventory,
  equipment, attributes, skills and spellbook tables match the pre-test snapshot.
- Final local fixture contains the existing Studio and appearance packages plus
  `fetcher.hotkeys`. Public client releases and both VPS servers were unchanged.

Detailed local logs and JSON results are under `build/hotkeys-qa/` (ignored).

## Window texture update — 2026-10-26

Hotkeys v2 adds inventory/chat title blocks to browser windows, matching filled
scrollbar handles and options-menu arrow icons. The HUD retains its plain label
without a textured title bar. Appearance v7 uses the same window/scrollbar/arrow
assets. All 473 hotkey and 255 appearance checks pass. Native integration opened
both browsers and the appearance editor without Lua or missing-texture errors;
the final HUD-only title-bar removal also passed the existing hotbar checks.
One unrelated RigGeometry/skeleton warning occurred during character loading.

The follow-up Hotkeys v3 / Appearance v8 update adds the native inset body frame
and moves window captions to `(4,4)`, height 20, meeting the inner border at y=24.
The HUD remains unchanged. Both existing Lua suites still pass (728 checks total).
