# Consuming Animated multiplayer presentation

This server-selected package decorates the installed OpenMW Consuming Animated
2.0.2.1 PLAYER and CUSTOM scripts. It does not include copies of those scripts or
assets. The original files remain unchanged. The existing package registry checks
their base hashes, distributes this package, and clears its overlay on disconnect.
A changed upstream entry point requires compatibility review and updated accepted
hashes; an absent mod is optional and the adapter is inert.

The server counterpart is `apps/openmw-server/scripts/consuming_animated_relay.lua`,
registered separately in the dedicated server's release manifest. Selection uses
the normal `[client_lua] packages` / `OPENMW_SERVER_LUA_PACKAGES` root. There is no
new downloader, persistent presentation store, or release archive.

## Runtime contract

Multiplayer Lua API 11 adds `loadOriginalScript(moduleProxies)` to an overridden
entry point's sandbox. It executes that entry point's installed VFS source once,
with an isolated environment and require cache. Proxies replace modules only in
that environment. It accepts neither a path nor executable source, gives no new
native permissions, and is absent for ordinary scripts and package-only scripts.
Callbacks retain the original script's engine identity and lifecycle. Native
userdata methods (notably `async:callback`) must retain their original receiver.

The mod's `I.PotionAnim` version 3 API exposes animation status and record ID but
not resolved mesh/bone/speed or random sound selection. Consequently the adapter
wraps its real animation/VFX/sound calls, including public `playForItem` calls.
Its resolver, random choice, sound suppression, settings, controls, text keys and
timers continue to execute in the original source. Receiver timing follows those
source operations over the reliable ordered Lua bridge; it does not independently
randomize audio or estimate text-key times.

`PotionAnim_ConsumePresentation` is a bounded semantic operation stream: `start`,
`vfx`, `removeVfx`, `sound`, `stopSound`, `finish`. Each action has a token and a
30-second maximum lifetime. The server supplies authenticated player identity,
validates canonical actor instance ID/cell/type/death/authority, rejects out-of-range speed
(0.25–4), duration (0.1–30), sound volume (0–1) and pitch (0.5–2), and limits traffic
and operations. VFX and audio use whitelisted keys resolved against local mod
tables/content records; paths and arbitrary animation groups never cross the wire.

Supported groups: `potionl`, `eatingr`, `drinkbone`, `bugmusk2`, `skoomapipe`,
`smokepipe1`, `smoke1r`. User and API item mappings to these groups work. Other
custom groups or override meshes have no observer relay until a corresponding
explicit policy is added. This restriction does not change the source animation.

Players use `mp_remote_<guid>` identity. Multiplayer Lua API 12 adds the
canonical `mp.getActorInstanceId(actor)` / `mp.getActorByInstanceId(id)` path for
NPCs, covering both placed content references and server-spawned actors without
colliding their 32-bit IDs. Only the authority executes NPC consume callbacks. A
lost authority cannot write equipment from a delayed original callback. Receivers
resolve the same canonical actor instance ID and never consume items, apply effects,
change equipment, override controls, or emit network events. Their animation mask
contains left arm and torso, plus right arm for groups other than `potionl`.
Equipment visibility relies on the existing authoritative equipment path.

Only observers with the actor cell loaded at start receive operations. Newly
loaded/late-joining observers do not replay partial actions; inactive actors drop
events. Finish, death, inactivity, disconnect and a timeout clean up visuals.
One-shot shatter sounds finish naturally; source pipe stop events stop pipe audio.

## Validation

From the repository root:

```powershell
lua scripts/test_consuming_animated_relay.lua
lua scripts/test_consuming_animated_client.lua '<installed OpenMWConsumingAnimated directory>'
cmake --build MSVC2022_64 --config RelWithDebInfo --target components-tests openmw-tests openmw-server openmw --parallel 3
$env:OPENMW_DEBUG_LEVEL = 'VERBOSE'
& MSVC2022_64/RelWithDebInfo/components-tests.exe
& MSVC2022_64/RelWithDebInfo/openmw-tests.exe
git diff --check
```

The Lua client harness executes the actual installed original scripts without
editing them. It verifies resolution, sound choice/timing, masks, disabled/offline
behavior, NPC authority gating and receiver cleanup. Native LuaState tests cover
the original loader, sandbox separation and overlay removal. These tests do not
replace paired-client visual, inventory/effect, shield/torch or handoff acceptance.
