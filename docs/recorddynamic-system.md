# Server-authoritative runtime records

This multiplayer branch treats load-time content and player-created runtime
records as different systems.

`openmw.content` remains an ordinary deterministic load-script API. Its changes
are part of the resolved content view; they do not allocate `$custom_*` IDs and
are never written to runtime-record tables. The handshake compares ordered base
files, configured Lua script paths and hashes, content/API versions, the runtime
wire version, and a canonical SHA-256 over the resolved Potion, Enchantment,
Weapon, Armor, Clothing, and Book stores. The server derives all three manifests
from its own `[content] openmw_cfg`; `Config.RESOLVED_CONTENT_FINGERPRINT` is only
an optional startup assertion.

`world.createRecord` remains synchronous in single-player. A connected client
gets a descriptive error instead of inserting a local record. Draft constructors
remain local and are suitable for previews.

## Runtime request flow

Authorized client scripts use `require('openmw.mp').records.request`. The engine
derives package identity from the hidden synchronized script ID. Requests contain
typed DTO drafts, temporary keys, dependencies, an inventory revision, and an
idempotency key. The server validates and canonicalizes the bundle, resolves
dependencies, fingerprints definitions, reuses exact equivalents, allocates
`$custom_<type>_<n>` IDs, and commits definitions, catalog metadata, dependency
links, optional inventory state, and the terminal result in SQLite.

Definitions are broadcast before the result. The client also holds an accepted
result until every returned ID resolves in `ESMStore` and the result's inventory
revision is visible. Disconnect completes pending callbacks with an error and
creates no client-local definitions.

The durable wire/database payload is the versioned, little-endian `OMDR` codec;
it is independent of Lua tables and C++ layout. Typed records are canonicalized,
validated, fingerprinted, and have dependencies rebuilt at startup. New trusted
server-Lua writes for supported DTO types use the same service and a durable
server-request journal. On restart, legacy Lua-table definitions are backed up
and migrated to OMDR when conversion and authoritative reference validation
succeed. Failed or unsupported migrations leave the readable original intact
and record a durable diagnostic.

## Permissions and validation

Generic client creation is default-deny. Configure exact script paths in
`Config.RUNTIME_RECORD_CAPABILITIES`; only Potion, Enchantment, Weapon, Armor,
Clothing, and Book have DTO support. Every referenced static ID and VFS asset is
validated against the server's loaded content registry. Generated-looking
references are accepted only when present in the authoritative catalog. Requests
are rate- and quota-limited. The old `Config.RUNTIME_RECORD_CONTENT_IDS` and
`Config.RUNTIME_RECORD_ASSETS` lists are deprecated and ignored.

Validation covers bounded payloads, UTF-8, finite/nonnegative numbers, effect
and enum ranges, VFS path traversal, temporary-key integrity, duplicate edges,
and dependency cycles. Provenance records creator, source, timestamps, schema,
validation version, and fingerprint. Lifetime remains reference-based through
inventory, equipment, world, container, actor, and record-dependency links.

## Native crafting status

Generic definitions are not a safe substitute for crafting rules. `alchemy` and
`enchanting` operations are rejected until a headless server mechanics validator
can independently verify ingredients, source instances, character statistics,
soul/gold state, capacity, costs, success, and randomness. Connected native
crafting menus therefore perform no mutation and report that authoritative
crafting is unavailable. No client-local crafted ID, consumed resource, or skill
gain can leak into multiplayer state.

The legacy server-Lua table API is now only a compatibility parser boundary for
Potion, Enchantment, Weapon, Armor, Clothing, and Book; persistence, provenance,
dependencies, validation, fingerprinting, and idempotency are owned by
`DynamicRecordService`. New writes for broader world-authority types such as NPC,
Creature, or Container are rejected clearly, while already persisted legacy rows
remain readable. The remaining convergence work is native semantic alchemy and
enchanting calculation.

## Upstream-facing surface

Generic engine changes are narrow: a central Lua creation facade, typed dynamic
store erasure with static fallback restoration, and record-change notifications
with pending authority snapshots. Multiplayer DTOs, packets, persistence,
permissions, and client transaction assembly live in multiplayer directories.
Native alchemy/enchanting contain only the connected-session safety hook until
their reusable mechanics are extracted.
