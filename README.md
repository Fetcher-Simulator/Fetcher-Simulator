# Fetcher Simulator

Fetcher Simulator is an experimental native multiplayer fork of [OpenMW](https://gitlab.com/OpenMW/openmw) `0.52.0`. It combines an integrated multiplayer client, a dedicated server, persistent multiplayer state, and server-side Lua scripting in a modern OpenMW codebase.

A legally obtained copy of Morrowind is required. Game data is not included.

> **Project status:** playable and under active public testing. Recent two-client tests have covered actor border crossings, owner disconnects, live cell resets, combat, and temporary spell effects. Longer sessions and larger groups still need validation. This is not an upstream OpenMW or TES3MP release; multiplayer protocol, database, scripting, and save compatibility may change between builds.

- [Downloads and releases](https://github.com/Fetcher-Simulator/Fetcher-Simulator/releases)
- [Project Discord and public testing](https://discord.gg/wXqQeSWRZF)
- [Multiplayer server setup guide](files/server/server-setup-guide.md)
- [Dedicated server Lua guide](apps/openmw-server/scripts/README.md)
- [Client-side Lua multiplayer compatibility](apps/openmw-server/scripts/CLIENT_LUA_MULTIPLAYER.md)
- [Actor identity and authority architecture](docs/architecture/actor-authority.html)

## Current State

### Available now

- Native multiplayer client integration and a standalone `openmw-server` dedicated server.
- GameNetworkingSockets transport with direct connection, basic server-list flows, account login, key linking, and persistent characters.
- Synchronization for player movement, appearance, equipment, inventory, stats, spells, chat, death, and cell changes.
- Actor synchronization with stable server-wide identities, per-actor simulation ownership, prepared authority handoffs, movement and combat presentation, death and corpses, and exterior crossings that preserve actor state.
- Coordinated live cell resets that clear persisted world state and invalidate connected clients' actor and cell caches without requiring a relog.
- Temporary actor spell effects and movement changes preserved through tested border crossings and owner handoffs.
- Persistent shared state for containers, doors, placed objects, dynamic records, spawned actors, dead vanilla actors, accounts, and characters.
- Server-authoritative Lua scripting, persistent Lua storage, commands, administration services, gameplay systems, and client/server Lua events.
- A compatibility foundation for ordinary client-side OpenMW Lua mods, including PLAYER script persistence, storage handling, stable object identity, and initial generic synchronization routes.
- Optional Source-style surfing mechanics with multiplayer-aware movement handling.
- Release workflows for Windows, Ubuntu 24.04, and macOS, including the dedicated server and its curated Lua runtime files where supported.

### Still being worked on

- Longer multi-client and modded-world tests of actor ownership, visibility, movement, combat, spells, knockout, death, and visual parity.
- Recovery and compatibility testing across disconnects, authority changes, exterior travel, server restarts, and long-running sessions.
- Broader generic support for client-side OpenMW Lua mods through validated server-authoritative requests and shared-state routes.
- More complete quest, dialogue outcome, and world-progression synchronization.
- Server browser and master-server polish, latency reporting, administration tooling, and automated regression tests.
- Larger multiplayer stress tests, protocol migration handling, and stronger compatibility guarantees between releases.

## Releases

Release packages are published through [GitHub Releases](https://github.com/Fetcher-Simulator/Fetcher-Simulator/releases). Depending on the platform, packages may include:

- the `openmw` multiplayer client;
- `openmw-launcher`;
- the `openmw-server` dedicated server;
- the required server Lua runtime and documentation;
- `Build-OpenMWServerAuthority.py` and `server-setup-guide.md` for self-hosting and authoritative-content generation.

The Ubuntu 24.04 amd64 package can be installed with:

```sh
sudo apt install ./fetcher-simulator-ubuntu-24.04-amd64.deb
```

## Dedicated Servers and Lua

The dedicated server lives under `apps/openmw-server`. Shared multiplayer protocol code lives under `components/openmw-mp`, and client multiplayer integration lives under `apps/openmw/mwmp`.

Release packages ship `Build-OpenMWServerAuthority.py` and `server-setup-guide.md`. The Python builder creates the portable `content-authority/` input used by `ServerContentRegistry` from a known-good client's effective OpenMW content stack. In a packaged release, open `server-setup-guide.md` beside this README; the source copy is the [multiplayer server setup guide](files/server/server-setup-guide.md). It covers port forwarding, firewall configuration, `server.cfg`, public listing, authority generation, deployment, updates, and mismatch troubleshooting.

Server Lua can implement authoritative commands, moderation, persistence, shared gameplay systems, spawning, records, inventory operations, travel, world changes, and validated client requests.

See the [dedicated server Lua guide](apps/openmw-server/scripts/README.md) for scripting, security, persistence, packaging, and event examples.

## Source-Style Surfing

Fetcher Simulator includes optional Source-style surfing. Surf behavior is server-configurable and edge cases are still being tuned.

![Fetcher Simulator surfing gameplay](docs/media/fetcher-simulator-surfing.gif)

## Repository Layout

- `apps/openmw/mwmp` — multiplayer client integration, networking, UI, and synchronization systems.
- `apps/openmw-server` — dedicated server, database, Lua runtime, administration service, and server scripts.
- `components/openmw-mp` — shared packet types, protocol structures, serialization, and multiplayer utilities.
- `.github/workflows` and `CI` — build, package, verification, and release automation.

## Development

Fetcher Simulator is a fork of OpenMW `0.52.0`, not a fork or continuation of the TES3MP repository. Its GameNetworkingSockets transport, packet framing, dedicated server, SQLite persistence, authentication, server-side Lua runtime, and current ActorSync systems were implemented in this repository. TES3MP was used as a technical reference for a limited set of multiplayer-specific engine techniques: remote actor ground and jump state, suppressing local AI on network-controlled proxies, synchronizing chosen attack and cast animation variants, `refNum`/`mpNum` identity conventions, and actor authority handoff across cells.

### How actor synchronization differs from TES3MP

TES3MP and its default CoreScripts persist actor state; this project does not introduce persistence where TES3MP had none. The main difference is **what owns a live actor and when its simulation owner changes**:

| Aspect | TES3MP with default CoreScripts | Fetcher Simulator |
| --- | --- | --- |
| Live actor storage | The server groups actor lists and one simulation authority by loaded cell. CoreScripts saves and reloads cell data. | A server-wide registry keeps each actor's identity and state; exterior cells index actors by location. |
| Simulation ownership | One client is authoritative for ordinary actors in a loaded cell. The default scripts select and replace that client. | Each eligible actor has its own simulator. A capable owner can keep that actor across cell borders and changes in cell publisher. |
| Handoffs | A cell authority change switches the client's actor roles for that cell. | A per-actor Prepare/Ready/Commit/Applied exchange checks that the replacement can simulate the actor and transfers its final state before the new owner starts. |
| Border crossing and reset | Actors move between cell-managed lists; CoreScripts persists the move and coordinates cell resets through scripts and client reset packets. | Crossing moves an actor's spatial index entry without replacing its server record. A live reset clears SQLite world state and coordinates registry and client-cache invalidation. |

This adds protocol and bookkeeping complexity, but addresses freezes, duplicates, missing actors, and lost combat or temporary spell state encountered during this project's border, reset, and disconnect testing. It does not guarantee that every mod or long-running session is free of these bugs. TES3MP also supports actor rosters, active-spell synchronization, and configurable server-side Lua policy; the comparison above describes its default CoreScripts setup, not every TES3MP server. See the [actor architecture chapter](docs/architecture/actor-authority.html) for this project's data structures and transfer flow.

AI-assisted development tools are used in parts of the project. Contributors remain responsible for reviewing, testing, integrating, and distributing all changes.

Credits
-------

This project is based on [OpenMW](https://gitlab.com/OpenMW/openmw), an open-source engine for Morrowind.

Thanks to [TES3MP](https://github.com/TES3MP/TES3MP) and its contributors for their pioneering OpenMW multiplayer work.

Copyright (c) 2016-2022, David Cernat & Stanislav Zhukov

GameNetworkingSockets is used for multiplayer transport:

- [ValveSoftware/GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)

Thanks to Panzer and Arblarg for allowing their surf maps to be ported for this project.

See `AUTHORS.md` for OpenMW contributor credits.

License
-------

This project is distributed under GPLv3. The limited TES3MP-adapted material remains subject to the additional terms reproduced in `LICENSE`.

Font licenses and other bundled third-party notices remain in their existing files, including:

- `files/data/fonts/DejaVuFontLicense.txt`
- `files/data/fonts/DemonicLettersFontLicense.txt`
- `files/data/fonts/MysticCardsFontLicense.txt`
- `extern/GameNetworkingSockets/LICENSE`

Binary/package distributions should include the applicable license and notice files for OpenMW, the limited TES3MP-adapted material, GameNetworkingSockets, and other bundled dependencies.
