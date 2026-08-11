# Server-supplied OpenMW Lua packages

This multiplayer feature distributes session-owned OpenMW Lua gameplay packages
from the dedicated server to clients. It is a separate executable-policy system:
packages are not RecordDynamic/OMDR content definitions and are not authoritative
player or world state. A package may call existing typed multiplayer APIs, but
package transport never means “execute this command” and never replays semantic
gameplay results as Lua calls.

## Implementation status

The safe bootstrap path is implemented through package contracts, server discovery/transfer, client staging, temporary Lua/VFS overlay activation, and the combined pre-world gate. The current wire contract uses `MultiplayerProtocolVersion = 5`, `ServerLuaPackageManifestVersion = 1`, and `MultiplayerLuaApiVersion = 1`. Package sets are immutable for a connection. Protocol version 5 adds authoritative crime-state transport; it does not change either Lua package contract version.

Transactional runtime hot reload/state handoff is intentionally not implemented. The next multiplayer architecture phase can build typed semantic authority services (for example crime/bounty) on top of the existing package/API layer without changing package transport into a gameplay-state channel.

That first service now exists as `CrimeService`; see
[Semantic gameplay services](semantic-gameplay-services.md). Trusted dedicated
server Lua may use `player:setBounty(value, requestId)` and
`player:modifyBounty(delta, requestId)`. Both enter the typed service and its
atomic transaction. Distributed client Lua packages still cannot write
authoritative crime state, and package transport remains executable policy
rather than gameplay-state replication.

## Server configuration

`server.cfg` selects a package root relative to the server configuration file:

```ini
[client_lua]
packages = server-lua-packages
```

Each immediate child directory is one package and must contain `manifest.yaml`.
The server validates the complete directory at startup. A malformed package,
missing source, dependency cycle, unsupported API, or unsafe path fails startup.
A missing package root is an explicit valid empty package set.

```text
server-lua-packages/
  fetcher.gameplay/
    manifest.yaml
    main.lua
    modules/rules.lua
```

```yaml
manifestVersion: 1
packageId: fetcher.gameplay
packageVersion: 12
requiredOpenMWLuaApi: 139
requiredMultiplayerLuaApi: 1
dependencies: []
files:
  - main.lua
  - modules/rules.lua
scripts:
  - path: main.lua
    flags: [global]
```

`global`, `player`, and `custom` are the v1 registration flags. A global
registration cannot be combined with a local flag. Every registration must name
a declared Lua file. Manifest paths must already use canonical lowercase
forward-slash spelling.

The OpenMW Lua API revision is an exact compatibility requirement; `139` is the
revision in the current source tree. Distributed scripts can require the existing
`mp` package and inspect `mp.API_VERSION`, currently `1`.

## Identity, hashing, and limits

Package IDs use lowercase ASCII letters, digits, `.`, `-`, and `_`; they cannot
start/end with `.` or contain `..`. Identity is `(packageId, packageVersion,
packageHash)`. The SHA-256 covers the manifest version, identity/version, API
requirements, sorted dependencies, normalized paths, declared sizes, per-file
hashes and source bytes, plus sorted registration metadata. The package-set
SHA-256 covers sorted package identities; its first 64 bits form the explicit
generation used to reject stale chunks. Filesystem enumeration and packet arrival
order cannot change either hash.

V1 accepts Lua source only. It does not distribute content files, assets, native
libraries, or executables. Enforced limits are:

- 64 packages per set;
- 256 files, 128 registrations, and 64 dependencies per package;
- 96 bytes per package ID and 192 bytes per relative path;
- 4 MiB per file, 16 MiB per package, and 64 MiB per set;
- 48 KiB per network chunk.

UTF-8, NULs, absolute/drive paths, `..`, empty path components, mixed-separator
traversal, duplicates, and case-normalization collisions are rejected. Received
source is visible only below:

```text
scripts/multiplayer/<package-id-with-dots-as-slashes>/
```

Therefore a package cannot shadow `scripts/omw/*` or arbitrary mod files.

## Bootstrap and activation

The handshake negotiates `MultiplayerProtocolVersion = 4`,
`ServerLuaPackageManifestVersion = 1`, `MultiplayerLuaApiVersion = 1`, and the
exact OpenMW Lua API revision independently.

The server sends an explicit manifest, bounded source chunks, and an explicit
completion marker. The client allocates only manifest-declared buffers.
Out-of-order chunks and identical duplicates are accepted; stale, foreign,
out-of-range, or conflicting chunks fail. Completion requires every byte and
validates file, package, and package-set SHA-256 values.

The verified set then moves into Lua staging. Staging compiles every source as
text but does not add it to module lookup, register it with a live script
container, or execute it. Only after the ordered RecordDynamic bootstrap and
`RuntimeContentBootstrapComplete` succeed does the client install the Lua-only
source overlay and append structured registrations to OpenMW's normal
`ESM::LuaScriptsCfg`. Existing local configuration remains first, preserving
normal interface-extension/override ordering.

`CharacterData` may arrive on another lane and is retained until both independent
requirements are true:

```text
runtime content definitions ready
    AND
server Lua package set activated
    -> release CharacterData
    -> enter the world
```

Malformed/incomplete data, incompatible APIs, hash mismatch, syntax compilation
failure, or registration/activation failure disconnects the client. There is no
fallback that enters gameplay without required policy.

## Trust, lifetime, and unsupported behavior

Joining a server means trusting its gameplay code within the existing OpenMW Lua
sandbox. The package mechanism adds no host filesystem, process, native-library,
shell, or unrestricted socket access. It never writes Lua files to Data Files,
rewrites `.omwscripts`, or changes `openmw.cfg`.

The overlay, staged buffers, registrations, and generation belong to one network
session. Disconnect tears down gameplay script containers on the main thread,
then removes the overlay and restores ordinary local Lua configuration. Reconnect
begins with empty transfer/readiness state, so code from server A cannot remain
registered for server B. With no multiplayer packages, ordinary single-player VFS
and `.omwscripts` behavior is unchanged.

Runtime hot reload is not implemented. OpenMW's current `reloadlua` path saves
live state and then mutates caches, configuration, and script containers in place;
failure has no package-set transaction or rollback capable of keeping the old
generation wholly active. Package sets are therefore immutable for a connection,
and a changed set requires reconnecting.

This subsystem does not distribute or execute trusted dedicated-server Lua; that
is a different runtime. Crime, bounty, arrest, quest services, arbitrary remote
commands, MWScript replacement, and general asset/mod distribution are outside
this feature.
