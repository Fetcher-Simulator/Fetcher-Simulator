# Client-side OpenMW Lua in Multiplayer

**Status updated:** July 24, 2026

This document summarizes the current state of Fetcher's work to make ordinary client-side OpenMW Lua mods behave sensibly in multiplayer without requiring each mod to be rewritten or each server owner to maintain a separate custom server script.

This README is intended to stand on its own for players, mod authors, and server operators. It describes the implemented compatibility features, current limitations, authority model, testing targets, and remaining work.

For the dedicated server Lua API and tutorials, see [Dedicated Server Lua Scripting](README.md).
For server-distributed, session-owned OpenMW Lua gameplay code, see
[Server-supplied OpenMW Lua packages](../../../docs/server-lua-packages.md).

## The goal

The target is not a second, multiplayer-only Lua modding API. The target is for an unmodified OpenMW Lua mod to continue using normal OpenMW APIs while the multiplayer fork transparently provides the persistence, identity, authority, synchronization, and diagnostics needed around it.

The compatibility design has three layers:

1. **Transparent engine behavior** for features the engine can handle safely and generically, such as PLAYER script lifecycle persistence, character-scoped settings, stable inventory identity, and common authority-owned mutations.
2. **A centrally distributed compatibility registry** for known mods whose events and mutations can be described with exact, versioned rules.
3. **Curated adapters** for mechanics whose meaning cannot safely be inferred from a generic Lua call, such as transformations, quest choreography, shared stronghold state, or complex spawned-world systems.

A mod-specific client shim can still be useful for diagnostics or unusual adapters, but it is not the default compatibility strategy. Requiring existing mods to import a Fetcher module would defeat the main goal.

## Client Lua and server Lua have different jobs

Client-side OpenMW Lua remains the correct place for:

- HUDs, menus, settings, and input;
- camera, shader, sound, music, and presentation effects;
- local prediction and responsive visual feedback;
- ordinary PLAYER script state that belongs to one character;
- mod logic that does not change authoritative multiplayer state.

Dedicated server Lua remains the correct place for:

- validating requests from clients;
- authoritative character and shared-world state;
- inventory rewards and gameplay-sensitive persistence;
- actor, object, quest, and world mutations that every client must agree on;
- moderation, permissions, policy, and rate limits;
- coordinating results among players.

A multiplayer feature commonly uses both sides. The client gathers input and presents the result; the server decides whether the gameplay action is valid and applies or broadcasts the authoritative outcome.

## Current implementation state

| Area | Current state |
| --- | --- |
| Client/server Lua event bridge | Available. Client Lua can use `openmw.mp.sendToServer(...)`; server Lua receives the named event with authenticated sender information. Server Lua can reply with `mp.send`, `mp.broadcast`, or `mp.broadcastToCell`. |
| PLAYER script lifecycle | Implemented and live-tested. Character-bound `player_scripts.bin` checkpoints restore normal PLAYER `onSave`/`onLoad` behavior across multiplayer relogs instead of always starting with `onInit`. |
| Character isolation | Live-tested. PLAYER script state and player storage are separated by server namespace and character key. |
| Permanent player storage migration | Defaults-plus-overlay rebinding is implemented. Current engine/mod defaults are restored first and old character values are overlaid, preventing older storage files from deleting newly added settings. The old-camera-storage acceptance case remains part of the final validation set. |
| Stable inventory instance identity | Implemented in the client/server source and packet/database paths. Server-issued `instanceId` values allow Lua references to distinguish otherwise identical stacks and survive inventory/world transfers. Broader paired-client acceptance is still in progress. |
| Native Lua mutation audit | Implemented and building. Multiplayer mutations can be attributed to the exact active script, operation, target category, stable identity, and current authority route through `[MPAUDIT]` diagnostics. Real-session reports and script-bundle fingerprints still need broader coverage. |
| Generic inventory/container/world routes | Initial routes are implemented. Same-inventory moves preserve handles, Lua drops use normal world-object placement, known world pickups retain identity, and non-player container transfers emit authoritative deltas. Acceptance with original unmodified inventory mods and multiple clients is still pending. |
| Generic teleport and movement routing | Planned next. Safe self-movement needs prediction plus server validation and correction rather than blindly trusting a client-side `teleport` call. |
| Generic actor/shared-world mutation routing | Partial infrastructure exists through ActorSync, WorldObjectSync, ObjectSync, and server `IntentPolicy`; broad automatic classification and policy enforcement are not complete. |
| Authoritative character Lua storage | Existing server SQLite storage and bindings are available to server scripts, but a generic capability-scoped client proposal protocol is not complete. Local UI settings must not be mirrored to the server automatically. |
| Compatibility registry and curated profiles | Designed but not yet the normal deployment path. Exact script-bundle fingerprints, signed/versioned rules, and initial profiles remain future work. |
| Warn/enforce policy | Audit groundwork exists. Full `audit`, `warn`, and `enforce` progression with hardened validation remains future work. |

## What already works well without special handling

Purely local mods are the easiest category. UI layout, settings windows, local keybinds, music routing, shaders, camera effects, and non-authoritative visual behavior should continue to run locally without generating multiplayer traffic.

PLAYER scripts now have substantially better compatibility because their normal save payloads can survive a multiplayer relog. This directly addresses mods that keep hotbars, framework state, glider state, or similar character-local data in `onSave`/`onLoad` rather than in permanent storage.

Permanent `storage.playerSection` settings are also safer across updates. A stored file missing a newly introduced key should receive the current default instead of replacing the whole registered settings table with an older incomplete table.

## What is only partially generic today

Inventory and container operations are the most advanced generic mutation path. Stable server-issued instance identity and the first Lua-driven pickup, drop, split, and container-transfer routes now exist in the engine. These changes are intended to support original unmodified mods such as inventory extenders and hotkey systems, but the full two-client and late-join acceptance matrix is not yet complete.

The mutation audit system can identify calls such as object creation, removal, movement, teleporting, scaling, enable/disable, script attachment, inventory changes, actor state changes, and quest/player journal writes. Audit does not by itself make an operation authoritative; it provides the evidence needed to determine which operations can be routed generically and which require a profile.

## What cannot be made safe by blindly forwarding calls

A local or global OpenMW Lua event is not automatically a multiplayer event. Many single-player mods use `core.sendGlobalEvent` to move logic from a PLAYER script to a client GLOBAL script. Forwarding every such event to the server would expose arbitrary mod-defined calls as an unvalidated network API.

Likewise, the engine cannot infer the complete gameplay meaning of every call to:

- `teleport`;
- `createObject` or object placement;
- `remove`;
- actor movement or transformation;
- quest and shared-world changes;
- inventory, spell, stat, or reward changes;
- dynamic script attachment.

The target is conservative generic support for operations whose ownership and limits are clear, plus centrally shipped rules or adapters for ambiguous mechanics. Unsupported or uncertain mutations should produce useful diagnostics rather than silently diverging between clients.

## Authority model

The compatibility work follows this ownership split:

| State | Default owner |
| --- | --- |
| UI, keybinds, camera, audio, and local presentation | Client |
| Character-local PLAYER script save payload | Client engine, scoped to server and character |
| Gameplay-sensitive character state | Server |
| Shared world, actors, quests, and persistent objects | Server |
| Responsive local movement prediction | Client prediction with server confirmation/correction |

Client data is never treated as trustworthy merely because it came from Lua. The server receives the authenticated connection identity separately and injects sender information into inbound event payloads. Server handlers must use that trusted identity and validate permissions, ownership, ranges, counts, targets, cooldowns, and current world state.

## Data flow

A typical compatible action follows this path:

```text
Unmodified client Lua mod
    -> normal OpenMW API or client Lua event
    -> multiplayer classifier/audit layer
    -> local-only execution, or a typed server intent
    -> server Lua IntentPolicy and native validation
    -> PlayerSync / ActorSync / WorldObjectSync / ObjectSync
    -> authoritative result sent to affected clients
```

Client-to-server custom events use the client `openmw.mp` package:

```lua
local mp = require("openmw.mp")

mp.sendToServer("Example_Request", {
    targetId = "example_target",
})
```

The server receives `Example_Request` as an event handler. The runtime injects authenticated sender fields such as `pid` and, when available, `characterId` into a table payload. The handler should validate the request and send back an explicit result.

This explicit API is useful for native multiplayer mods and curated adapters. The generic compatibility project aims to reduce how often an ordinary existing mod must be changed to call it directly.

## Compatibility targets from the installed-mod audit

Current compatibility testing uses real installed mods as acceptance targets rather than relying only on synthetic examples:

- **Zerkish Hotkeys Improved:** PLAYER save/load restoration and stable identity for distinct item stacks.
- **Inventory Extender:** handle-preserving same-inventory moves, world drops and pickups, split stacks, and container transfers.
- **StatsWindow and settings mods:** local settings persistence and safe defaults migration.
- **SkillFramework:** PLAYER lifecycle now; NPC state must follow authority rather than being restored independently by every client.
- **Tamriel Data passwall and OpenMW Hookshot:** validated player movement, with actor/object targets routed according to authority.
- **ErnGlider and surf maps:** local presentation plus validated movement and shared collision/world state.
- **Tamriel Rebuilt strongholds:** server-owned shared progression and object state.
- **Bardcraft:** dedicated persistence remains appropriate while generic lifecycle and identity support reduce its custom patch surface.
- **Complex quest/transformation mods:** curated server-authoritative adapters where generic inference would be unsafe.

## Guidance for mod authors

Continue using normal OpenMW Lua APIs. Keep local presentation local, use normal `onSave`/`onLoad` for character-local PLAYER state, and avoid assuming that a client GLOBAL script owns shared world truth.

For a mod designed specifically for multiplayer, prefer small typed requests through `openmw.mp.sendToServer` rather than sending raw desired state. For example, request “use ability X on target Y” rather than “set my stat to 500” or “create arbitrary record Z.” The server can then validate the intent and decide the authoritative operation.

Do not use local-only storage for rewards or state that affects other players. Do not expect client-created actors, collision objects, quest changes, or inventory grants to become authoritative merely because they worked in single player.

## Guidance for server operators

Matching client and server builds matter whenever the multiplayer packet format or identity model changes. The stable inventory-instance work in particular requires paired client/server deployment.

Server operators should not patch supported client mods by hand as the normal compatibility strategy. Generic engine behavior and centrally distributed profiles are intended to keep compatibility consistent across servers. Until a mutation route or profile has passed acceptance, use audit output and explicit server scripts for gameplay-sensitive features.

Content validation currently verifies configured content files, including `.omwscripts`, but an `.omwscripts` manifest does not prove the contents of every referenced or imported Lua file. Exact compatibility profiles therefore need a resolved Lua bundle fingerprint or a signed package manifest, not just a filename.

## Near-term work

The immediate validation and implementation sequence is:

1. Test matching client/server builds with the original unmodified inventory scripts across two clients and late join.
2. Verify identity migration for old database rows, split versus whole stacks, duplicate record instances, and server-created custom records.
3. Finish corrupt PLAYER-state fallback and the old settings-file migration acceptance cases.
4. Gather `[MPAUDIT]` reports from representative movement, actor, object, quest, and transformation mods.
5. Attach verified script-bundle fingerprints to diagnostics.
6. Implement the narrow validated player-movement intent with prediction and correction.
7. Add curated profiles only where audit evidence shows that ownership and intent cannot be inferred safely.

The practical objective is broad compatibility for unmodified client mods while keeping authoritative gameplay bounded, observable, and controlled by the server.
