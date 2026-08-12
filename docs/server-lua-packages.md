# Server-supplied OpenMW Lua packages

This multiplayer feature distributes session-owned OpenMW Lua gameplay packages
from the dedicated server to clients. It is a separate executable-policy system:
packages are not RecordDynamic/OMDR content definitions and are not authoritative
player or world state. A package may call existing typed multiplayer APIs, but
package transport never means “execute this command” and never replays semantic
gameplay results as Lua calls.

## Implementation status

The safe bootstrap path is implemented through package contracts, server
discovery/transfer, client staging, temporary Lua/VFS overlay activation, and
the combined pre-world gate. The current wire contract uses
`MultiplayerProtocolVersion = 8`, `ServerLuaPackageManifestVersion = 2`, and
`MultiplayerLuaApiVersion = 1`. Manifest version 2 adds explicit compatibility
overrides. Multiplayer protocol versions 5, 6, and 7 add crime, known-topic,
and faction-state transport respectively; those semantic services do not use
package transport. Package sets are immutable for a connection.

Transactional runtime hot reload/state handoff is intentionally not
implemented. Typed semantic authority services build beside the package/API
layer without changing executable package transport into a gameplay-state
channel.

Crime, known-topic, and faction services are documented in
[Semantic gameplay services](semantic-gameplay-services.md). Trusted dedicated
server Lua may enter the typed crime and faction services. Distributed client
Lua packages still cannot write authoritative SQLite state, and package
transport remains executable policy rather than gameplay-state replication.

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
manifestVersion: 2
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

`global`, `player`, and `custom` are the registration flags. A global
registration cannot be combined with a local flag. Every registration must name
a declared Lua file. Manifest paths must already use canonical lowercase
forward-slash spelling.

The OpenMW Lua API revision is an exact compatibility requirement; `139` is the
revision in the current source tree. Distributed scripts can require the existing
`mp` package and inspect `mp.API_VERSION`, currently `1`.

## Compatibility overrides

Manifest version 2 supports a second, explicit package capability: temporary
replacement of an existing third-party OpenMW Lua source. The base file is not
modified. A typical compatibility package declares the complete replacement as
a package file and maps it to an existing VFS target:

```yaml
manifestVersion: 2
packageId: fetcher.inventoryextender-fix
packageVersion: 1
requiredOpenMWLuaApi: 139
requiredMultiplayerLuaApi: 1
dependencies: []
files:
  - overrides/item.lua
scripts: []
overrides:
  - target: scripts/inventoryextender/item.lua
    source: overrides/item.lua
    acceptedBaseHashes:
      - e5007f2fc3db106b2fa9b329c0a00beb4107f8b0a00ca87cc88169f89a26d918
```

`acceptedBaseHashes` is the safe default: staging reads and hashes the original
base VFS source, not an already overlaid source, and fails closed unless one of
the declared SHA-256 values matches. An author who deliberately accepts every
installed base version must write `basePolicy: any`. A manifest cannot combine
that policy with accepted hashes, and omitting both is invalid.

Targets and sources must be canonical relative lowercase `.lua` paths. The
target must already exist in the base VFS, must not have been compiled, and
must not be below `scripts/omw/` or `scripts/multiplayer/`. Sources must be
declared package files and pass the same size, UTF-8, hash, and syntax checks as
additive scripts. Assets, native code, configuration, `.omwscripts`, absolute
paths, traversal, and filesystem writes remain forbidden. One package set may
have only one owner for a target; duplicate targets fail deterministically
rather than depending on package order.

The first operator package corrects Inventory Extender's item-local `onInactive`
handler. The local object script cannot access a player-container interface, so
the complete replacement sends `self.recordId` as the item record ID. The
tested installed base hash is the one shown above. This package is an operator
fixture outside the core source tree, not a modification to the user's physical
Inventory Extender file.

## Identity, hashing, and limits

Package IDs use lowercase ASCII letters, digits, `.`, `-`, and `_`; they cannot
start/end with `.` or contain `..`. Identity is `(packageId, packageVersion,
packageHash)`. The SHA-256 covers the manifest version, identity/version, API
requirements, sorted dependencies, normalized paths, declared sizes, per-file
hashes and source bytes, sorted registration metadata, and all canonical
override meaning: target, source, base policy, and accepted hashes. The
package-set SHA-256 covers sorted package identities; its first 64 bits form the
explicit generation used to reject stale chunks. Filesystem enumeration and
packet arrival order cannot change either hash.

The current contract accepts Lua source only. It does not distribute content
files, assets, native libraries, or executables. Enforced limits are:

- 64 packages per set;
- 256 files, 128 registrations, 128 overrides, and 64 dependencies per package;
- 32 accepted base hashes per override;
- 96 bytes per package ID and 192 bytes per relative path;
- 4 MiB per file, 16 MiB per package, and 64 MiB per set;
- 48 KiB per network chunk.

UTF-8, NULs, absolute/drive paths, `..`, empty path components, mixed-separator
traversal, duplicates, and case-normalization collisions are rejected. Received
source is visible only below:

```text
scripts/multiplayer/<package-id-with-dots-as-slashes>/
```

Additive sources cannot shadow `scripts/omw/*` or arbitrary mod files.
Compatibility overrides are the only path that can map a package source onto an
existing third-party Lua target, and they remain subject to the restrictions
above.

## Bootstrap and activation

The handshake negotiates `MultiplayerProtocolVersion = 8`,
`ServerLuaPackageManifestVersion = 2`, `MultiplayerLuaApiVersion = 1`, and the
exact OpenMW Lua API revision independently.

The server sends an explicit manifest, bounded source chunks, and an explicit
completion marker. The client allocates only manifest-declared buffers.
Out-of-order chunks and identical duplicates are accepted; stale, foreign,
out-of-range, or conflicting chunks fail. Completion requires every byte and
validates file, package, and package-set SHA-256 values.

The verified set then moves into Lua staging. Staging compiles every source as
text, validates compatibility targets against the immutable base source view,
and constructs the complete prospective overlay. It does not add that overlay
to module lookup, register it with a live script container, or execute it. Any
failure discards the entire prospective generation, so a set cannot partially
mount one override while retaining another registration from a different
generation.

Only after the ordered RecordDynamic bootstrap and
`RuntimeContentBootstrapComplete` succeed does the client atomically install
the source overlay and append structured registrations to OpenMW's normal
`ESM::LuaScriptsCfg`. Compatibility targets are rejected if already compiled,
which prevents replacing a script after its old source has begun executing.
Existing local configuration remains first, preserving normal
interface-extension/override ordering.

`CharacterData` may arrive on another lane and is retained until all independent
requirements are true:

```text
complete package set staged and base-validated
    AND
runtime content definitions ready
    AND
source overlay and package registrations activated
    AND
authoritative semantic player state received
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

The overlay, staged buffers, overrides, registrations, and generation belong to
one network session. Disconnect tears down gameplay script containers on the
main thread, then removes the overlay and restores ordinary local Lua
configuration. The original base source becomes effective again and no Data
Files source is changed. Reconnect begins with empty transfer/readiness state,
so code from server A cannot remain registered for server B. With no multiplayer
packages, ordinary single-player VFS and `.omwscripts` behavior is unchanged.

Runtime hot reload is not implemented. OpenMW's current `reloadlua` path saves
live state and then mutates caches, configuration, and script containers in place;
failure has no package-set transaction or rollback capable of keeping the old
generation wholly active. Package sets are therefore immutable for a connection,
and a changed set requires reconnecting.

This subsystem does not distribute or execute trusted dedicated-server Lua;
that is a separate executable-code runtime. Semantic crime, faction, topic, and
journal results use their typed state services instead of package messages.
Arbitrary remote commands, MWScript replacement, general asset/mod
distribution, and Phase 4 crime/arrest behavior are outside this feature.
