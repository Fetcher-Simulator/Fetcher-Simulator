# Dedicated Server Lua Scripting

This directory contains the dedicated-server Lua scripts used by `openmw-server` in this multiplayer-enabled OpenMW build.

This guide is written for server owners and script authors. It explains the server Lua runtime, how to configure and extend it, and the security and operational concerns involved in running custom scripts. Exact bindings and bundled feature scripts can vary between builds, so treat the files and C++ bindings shipped with your server as the source of truth.

> Client-side OpenMW Lua is a separate runtime responsible for mod UI, input, presentation, PLAYER script state, and client-originated gameplay requests. See [Client-side OpenMW Lua in Multiplayer](CLIENT_LUA_MULTIPLAYER.md) for the current state of generic multiplayer compatibility support for existing mods.

Server Lua is the authoritative scripting layer between the multiplayer network server, connected players, persistent server state, and client-side OpenMW Lua. It is mainly intended for multiplayer rules and services that must be controlled by the server rather than trusted to an individual client.

Typical uses include:

- chat commands and administration;
- authentication, moderation, and access checks;
- shared world or minigame state;
- validating client requests;
- server-controlled spawning, records, inventory, time, weather, and travel;
- persistent account or character features;
- coordinating a client-side Lua feature across multiple players;
- sending targeted or broadcast Lua events to clients.

Server Lua is generally **not** the right place for rendering, HUD layout, camera effects, per-frame input handling, or purely local visual behavior. Those belong in client OpenMW Lua scripts. A common multiplayer feature uses both sides: the client handles presentation and input, while the server validates requests and owns the authoritative state.

## Server owner quick start

Before opening the server to players:

1. Confirm that the complete `server-scripts` directory was installed with the server binary.
2. Review `server.omwscripts` and remove global entries for features you do not intend to run.
3. Review `config.lua`, replace placeholder credentials, and disable administrative interfaces you do not need.
4. Ship every module and generated data file required by the enabled global scripts, but exclude development harnesses, backup files, and editor artifacts.
5. Make sure client-side scripts and content required by a client/server feature are distributed consistently to players.
6. Start the server and check the log for the selected script directory, parsed manifest, loaded global scripts, and Lua errors.
7. Back up the player database and `server-lua-storage.bin` whenever server Lua owns persistent gameplay or world state.

Make changes on a test server first. A syntactically valid script can still grant excessive permissions, duplicate rewards, corrupt persistent state, or trust data supplied by a modified client.

## Directory overview

The important files are:

- `server.omwscripts` — declares the independently loaded global server scripts.
- `core.lua` — common server lifecycle, authentication, command dispatch, and event routing.
- `custom_scripts.lua` — central registry for deployment-specific gameplay and administration modules used by `core.lua`.
- `config.lua` — operator-editable server configuration, exposed as `require("config")`.
- additional feature-specific global scripts, which may run independently for a particular mod or service.
- `recordstore.lua` and `intent_policy.lua` — shared server infrastructure.
- the remaining `.lua` files — modules loaded with `require(...)` by one of the global scripts.

Some server packages also include generated manifests or data modules. These can be runtime dependencies even though they should not be edited manually. Check comments in each generated file and preserve it when packaging the feature that consumes it.

`test_callbacks.lua` is a development harness and should not be loaded or shipped as a production script.

## How scripts are loaded

At startup, the server:

1. Locates a `server-scripts` directory.
2. Adds that directory to the server Lua virtual filesystem and module search path.
3. Finds and parses every `.omwscripts` manifest in the directory.
4. Loads each script declared with `GLOBAL:`.
5. Starts the dedicated Lua tick thread.

The exact manifest depends on the server package and enabled features. A minimal installation might contain:

```text
GLOBAL: core.lua
```

An independently loaded feature can add another entry:

```text
GLOBAL: optional_feature.lua
```

Inspect the `server.omwscripts` file shipped with your server instead of assuming every bundled feature is required.

A `GLOBAL:` script is independently loaded by the engine and may return `eventHandlers`, a public interface, or both.

A normal module loaded with `require("module_name")` is different. It is cached and returned to its caller, but it does not automatically become an independent event listener. This is why feature modules are registered in `custom_scripts.lua` and then explicitly called by `core.lua`.

Do not add every module to `server.omwscripts`. Use `GLOBAL:` only for scripts that genuinely need their own event-handler table or public interface.

## The server script shape

A minimal global server script returns a table containing named event handlers:

```lua
local mp = require("mp")

return {
    eventHandlers = {
        OnServerInit = function(_data)
            mp.log("Tutorial script started")
        end,

        OnPlayerConnect = function(data)
            local player = mp.getPlayer(data.guid)
            if not player then
                return
            end

            player:sendMessage("Welcome, " .. player.name .. "!")
            mp.log(player.name .. " connected")
        end,
    },
}
```

Save this as `tutorial.lua` and add the following line to `server.omwscripts` only when it should be an independent global script:

```text
GLOBAL: tutorial.lua
```

For ordinary server features, prefer the module pattern described below instead of adding another global entry point.

## Adding a custom module

Create `hello_command.lua`:

```lua
local M = {}

function M.handleChat(player, data, env)
    local message = tostring(data.message or "")
    local command = (env.commandPrefix or "/") .. "hello"

    if message ~= command then
        return nil
    end

    player:sendMessage("Hello from server Lua, " .. player.name .. "!")
    return false
end

return M
```

Register it in `custom_scripts.lua`:

```lua
return {
    -- Existing modules...
    helloCommand = require("hello_command"),
}
```

Then wire it into the appropriate place in `core.lua`:

```lua
local handled = customScripts.helloCommand.handleChat(player, data, {
    commandPrefix = COMMAND_PREFIX,
})
if handled ~= nil then
    return handled
end
```

The convention used by the command modules is:

- return `nil` when the module does not recognize the message;
- return `false` after handling a command so the original text is not relayed as normal chat.

`custom_scripts.lua` centralizes module inclusion, but it intentionally does not guess which events or helper functions each feature requires. Event routing remains explicit in `core.lua`, making handler order and security dependencies visible.

## Common server events

The server currently emits events including:

- `OnServerInit`
- `OnServerTick`
- `OnPlayerConnect`
- `OnPlayerDisconnect`
- `OnPlayerCellChange`
- `OnPlayerSendMessage`
- `OnDoorState`
- `OnWorldWeather`
- `OnActorSpawned`
- `OnActorDeath`

Clients may also send custom event names. Those names arrive in the same `eventHandlers` table, such as `Tutorial_Request`, `MinigameTrigger`, or any event defined by a client/server feature.

Event payloads are Lua tables. Examples:

```lua
OnPlayerDisconnect = function(data)
    mp.log(string.format(
        "%s disconnected: %s",
        tostring(data.name),
        tostring(data.reason)))
end
```

```lua
OnPlayerCellChange = function(data)
    mp.log(string.format(
        "%s moved from %s to %s",
        tostring(data.name),
        tostring(data.oldCell),
        tostring(data.newCell)))
end
```

```lua
OnServerTick = function(data)
    local dt = tonumber(data.dt) or 0
    -- Accumulate dt and run infrequent work rather than doing heavy work here.
end
```

Normal event-handler return values are not a general-purpose cancellation mechanism. For example, `core.lua` suppresses a command by choosing not to call `mp.relayChat`; it does not depend on the engine interpreting `return false` from an asynchronous event.

## The `mp` package

Server scripts access multiplayer functionality with:

```lua
local mp = require("mp")
```

The exact binding list is defined in:

- `apps/openmw-server/bindings/ServerBindings.cpp`
- `apps/openmw-server/bindings/PlayerBindings.cpp`

Common functions include:

```lua
mp.log("message")
mp.getPlayerCount()
mp.getPlayer(guid)
mp.getPlayers()
mp.getWorldTime()
mp.setWorldTime(12.0)
mp.broadcast("Server announcement")
mp.broadcastToCell(cellId, "Message for this cell")
mp.send(guid, "CustomEvent", { value = 123 })
mp.broadcast("CustomEvent", { value = 123 })
mp.broadcastToCell(cellId, "CustomEvent", { value = 123 })
```

Several functions are overloaded. For example, `mp.broadcast(text)` sends server chat, while `mp.broadcast(eventName, data)` sends a serialized Lua event to every client.

A player object provides methods such as:

```lua
local player = mp.getPlayer(guid)
if player then
    player:sendMessage("Private server message")
    player:setNickname("Example")
end
```

Always check that `mp.getPlayer(...)` returned a player. A disconnect can occur between the original network event and Lua processing.

## How Lua communicates with the C++ server

Server Lua runs on a dedicated Lua thread. The configured tick rate is read from `Config.LUA_TICK_RATE`; check the `config.lua` supplied with your server for the active value.

The normal communication path is queue based:

```text
network/game thread
    -> enqueue named event and serialized payload
Lua tick thread
    -> deserialize payload
    -> run matching eventHandlers
    -> mp.* call queues an outbound server action
network/game thread
    -> drains and applies outbound actions
```

This separation prevents Lua from directly mutating most live server objects from the Lua thread. Calls such as `mp.send`, spawning, inventory changes, kicks, broadcasts, and world changes usually queue an action that is applied by the server afterward.

Consequences for script authors:

- An `mp.*` mutation may not be visible synchronously in the same Lua statement sequence.
- A player may disconnect before a queued action is applied.
- Long Lua handlers delay other queued Lua events.
- `OnServerTick` must stay lightweight.
- Avoid busy loops, sleeps, blocking I/O, and large repeated serialization work.

The server also has a special immediate-intent path for operations that need a rapid authoritative answer. It wakes the Lua thread and waits only up to `Config.IMMEDIATE_INTENT_TIMEOUT_MS`. Intent evaluators must therefore be small, deterministic, and free of slow work.

## Client-to-server Lua events

In a multiplayer client script, the client-side `mp` package exposes:

```lua
local mp = require("mp")

mp.sendToServer("Tutorial_Ping", {
    message = "hello",
})
```

The event travels in a multiplayer Lua-event packet and is delivered to the matching server event handler:

```lua
local mp = require("mp")

return {
    eventHandlers = {
        Tutorial_Ping = function(data)
            -- The server injects the authenticated sender identity.
            local player = mp.getPlayer(data.pid)
            if not player then
                return
            end

            local message = tostring(data.message or "")
            if #message > 100 then
                return
            end

            mp.log(string.format(
                "Tutorial_Ping from %s: %s",
                player.name,
                message))

            mp.send(data.pid, "Tutorial_Pong", {
                accepted = true,
                message = message,
            })
        end,
    },
}
```

For table payloads received from a client, the server injects:

- `data.pid` — authenticated connection GUID;
- `data.characterId` — authenticated database character ID, when available.

These injected fields replace any same-named values supplied by the client. Use them as the sender identity. Do not trust a client-provided player name, GUID in another field, account name, character ID, permission flag, price, inventory count, position, or claimed result without server-side verification.

Server-to-client events use:

```lua
mp.send(guid, "EventName", data)                    -- one player
mp.broadcast("EventName", data)                    -- every player
mp.broadcastToCell(cellId, "EventName", data)      -- players in one cell
```

The receiving client handles `EventName` in its own OpenMW Lua `eventHandlers` table.

Keep payloads composed of serializable Lua values: booleans, numbers, strings, and tables containing supported values. Keep packets reasonably small and split large data into bounded chunks when necessary.

## Chat commands

`OnPlayerSendMessage` receives chat before `core.lua` decides whether to relay it.

The normal flow is:

1. Retrieve the authenticated player with `mp.getPlayer(data.guid)`.
2. Check whether the message begins with `Config.COMMAND_PREFIX`.
3. Offer it to command handlers in a deliberate order.
4. Require administrator status for privileged commands.
5. Send feedback with `player:sendMessage(...)`.
6. Return without relaying when a command was handled.
7. Call `mp.relayChat(data.guid, message)` for ordinary chat.

A command handler should validate argument count, types, ranges, referenced players, referenced records, and permissions. Do not place trust decisions only in a client-side command UI.

## Persistent global storage

The server exposes OpenMW global Lua storage through `mp.storage`:

```lua
local mp = require("mp")
local section = mp.storage.globalSection("Tutorial")

local total = section:getCopy("joinCount") or 0
section:set("joinCount", total + 1)
```

Use a unique section name for each feature. `getCopy` returns a copy suitable for modification; write the modified value back with `set`.

Persistent global storage is saved by the server to `server-lua-storage.bin` in the server working directory. Back up this file together with the player database when it contains important world state.

For runtime-only data:

```lua
local section = mp.storage.globalSection("TutorialRuntime")
section:setLifeTime(mp.storage.LIFE_TIME.GameSession)
```

Game-session storage is useful for authenticated-admin flags, caches, cooldowns, and other values that should reset on restart.

## Character storage

The server also provides database-backed storage tied to a character:

```lua
local progress = mp.getCharacterStorage(guid, "Tutorial", "progress") or {}
progress.completed = true
mp.setCharacterStorage(guid, "Tutorial", "progress", progress)
```

When operating on an offline or explicitly identified character, use the `ForCharacter` variants with a database character ID:

```lua
local value = mp.getCharacterStorageForCharacter(characterId, "Tutorial", "progress")
mp.setCharacterStorageForCharacter(characterId, "Tutorial", "progress", value)
```

Setting a character-storage value to `nil`, or calling the matching delete function, removes it.

Use global storage for server/world state. Use character storage for progression or preferences that belong to one saved character.

## Configuration

`config.lua` is loaded by the C++ runtime and exposed as a sandboxed module:

```lua
local config = require("config")
local prefix = config.COMMAND_PREFIX or "/"
```

Keep operator-editable values in `config.lua` instead of scattering them through feature modules. Provide safe defaults and validate configuration values before using them.

Never deploy with active secrets committed to the script directory or with a placeholder administrator password. Review `config.lua` before first launch, replace values such as `"changeme"`, and disable password-based administration entirely when it is not needed.

## Sandbox and filesystem access

Server Lua can use only packages and functions explicitly exposed by the server runtime. It does not receive unrestricted `os`, process execution, arbitrary sockets, or arbitrary filesystem access.

Some builds may expose tightly constrained, feature-specific file operations with server-owned destinations. These are narrow C++ APIs, not general filesystem access.

When a feature needs a new privileged operation, add a narrow validated C++ binding instead of exposing a general shell, filesystem, or network API to Lua.

## Security checklist

For every client-originated event or command:

- identify the sender using injected `pid`/`characterId` or the server chat event GUID;
- retrieve current server-side player state;
- validate every field and limit string/table sizes;
- enforce permissions on the server;
- verify ownership and existence of referenced objects or records;
- rate-limit actions that can be spammed;
- avoid broadcasting private data;
- log security-relevant failures without logging passwords or secrets;
- design handlers so duplicate or delayed packets do not corrupt state.

The client should be treated as a request source, not as an authority.

## Performance guidelines

- Keep event handlers short.
- Accumulate `data.dt` and run periodic work at a lower frequency instead of performing scans every tick.
- Cache derived data when safe.
- Avoid repeatedly copying very large storage tables.
- Chunk large network payloads.
- Do not serialize unnecessary fields.
- Do not use unbounded loops over players, actors, records, or storage.
- Keep immediate-intent evaluators well below their timeout.
- Use `mp.log` sparingly in hot paths.

## Errors and debugging

Use:

```lua
mp.log("[my_feature] useful diagnostic")
```

Startup logs report the selected script directory, parsed manifests, loaded global scripts, Lua tick rate, and script errors.

Common failures:

- **No server script directory found** — the release did not install `server-scripts` beside or in a supported data location.
- **No GLOBAL scripts configured** — the manifest is missing or empty.
- **module not found** — a required `.lua` file was not shipped or the `require` name is wrong.
- **handler never runs** — the module was required but not wired into a global script's `eventHandlers` or dispatch path.
- **client event has no handler** — the client and server event names differ, including capitalization.
- **changes do not appear** — restart the server; required modules are cached and there is no general production hot-reload path.

When available, syntax-check scripts before launching:

```text
luac -p core.lua
luac -p custom_scripts.lua
luac -p my_feature.lua
```

Use a Lua 5.1/LuaJIT-compatible parser for the closest match to this runtime. A syntax check does not validate server-only APIs or event behavior.

## Source files for deeper technical details

The implementation is the final source of truth:

- `apps/openmw-server/LuaServerContext.cpp` — script discovery, manifests, packages, storage, event queues, Lua thread, ticks, immediate intents, and outbound actions.
- `apps/openmw-server/bindings/ServerBindings.cpp` — functions exposed through server `require("mp")`.
- `apps/openmw-server/bindings/PlayerBindings.cpp` — player object properties and methods.
- `apps/openmw-server/Server.cpp` — multiplayer packet handling and application of server actions.
- `apps/openmw/mwmp/MpNetworkBridge.cpp` — client `mp.sendToServer(...)` and client-side Lua-event transport.
- `apps/openmw-server/scripts/core.lua` — current command and event-routing examples.
- other feature-specific global scripts in this directory — larger examples of client/server events, persistence, and independent event handling.

When adding a new binding, document its arguments, return value, thread/queue behavior, authority assumptions, and failure behavior here or next to its C++ registration.
