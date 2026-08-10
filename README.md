# Fetcher Simulator

Fetcher Simulator is an experimental native multiplayer fork of [OpenMW](https://gitlab.com/OpenMW/openmw) `0.52.0`. It combines an integrated multiplayer client, a dedicated server, persistent multiplayer state, and server-side Lua scripting in a modern OpenMW codebase.

A legally obtained copy of Morrowind is required. Game data is not included.

> **Project status:** playable and actively tested, but still under development. This is not an upstream OpenMW or TES3MP release, and multiplayer protocol, database, scripting, and save compatibility may change between builds.

- [Downloads and releases](https://github.com/Fetcher-Simulator/Fetcher-Simulator/releases)
- [Project Discord and public testing](https://discord.gg/wXqQeSWRZF)
- [Multiplayer server setup guide](files/server/server-setup-guide.md)
- [Dedicated server Lua guide](apps/openmw-server/scripts/README.md)
- [Client-side Lua multiplayer compatibility](apps/openmw-server/scripts/CLIENT_LUA_MULTIPLAYER.md)

## Current State

### Available now

- Native multiplayer client integration and a standalone `openmw-server` dedicated server.
- GameNetworkingSockets transport with direct connection, basic server-list flows, account login, key linking, and persistent characters.
- Synchronization for player movement, appearance, equipment, inventory, stats, spells, chat, death, and cell changes.
- Actor synchronization covering stable identities, authority handoff, movement, presentation, attacks, death, corpses, and exterior-cell transitions.
- Persistent shared state for containers, doors, placed objects, dynamic records, spawned actors, dead vanilla actors, accounts, and characters.
- Server-authoritative Lua scripting, persistent Lua storage, commands, administration services, gameplay systems, and client/server Lua events.
- A compatibility foundation for ordinary client-side OpenMW Lua mods, including PLAYER script persistence, storage handling, stable object identity, and initial generic synchronization routes.
- Optional Source-style surfing mechanics with multiplayer-aware movement handling.
- Release workflows for Windows, Ubuntu 24.04, and macOS, including the dedicated server and its curated Lua runtime files where supported.

### Still being worked on

- Actor authority edge cases, movement smoothing, and combat, spell, knockout, death, and visual parity between clients.
- Persistence recovery during disconnects, authority changes, exterior transitions, server restarts, and long-running sessions.
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
