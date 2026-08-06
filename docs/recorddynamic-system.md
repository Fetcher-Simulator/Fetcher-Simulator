# Server-authoritative runtime records

This multiplayer branch treats load-time content and player-created runtime
records as different systems.

`openmw.content` remains an ordinary deterministic load-script API. Its changes
are part of the resolved content view; they do not allocate `$custom_*` IDs and
are never written to runtime-record tables. The handshake compares ordered base
files, configured Lua script paths and hashes, content/API versions, the runtime
wire version, and a canonical SHA-256 over the resolved Potion, Enchantment,
Weapon, Armor, Clothing, and Book stores. A server that enables client record
capabilities must pin `Config.RESOLVED_CONTENT_FINGERPRINT`.

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
validated, fingerprinted, and have dependencies rebuilt at startup. Legacy
server-Lua payloads remain readable but are not accepted through client proposals.

## Permissions and validation

Generic client creation is default-deny. Configure exact script paths in
`Config.RUNTIME_RECORD_CAPABILITIES`; only Potion, Enchantment, Weapon, Armor,
Clothing, and Book have DTO support. Every referenced static ID and VFS asset
must appear in `Config.RUNTIME_RECORD_CONTENT_IDS` or
`Config.RUNTIME_RECORD_ASSETS`. Generated-looking references are accepted only
when present in the authoritative catalog. Requests are rate- and quota-limited.

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

The remaining convergence work is to route native semantic calculation and the
legacy server-Lua table API through the typed service. Trusted server Lua may use
the legacy administrative API in the meantime; authorized client Lua already
uses the canonical service.

## Upstream-facing surface

Generic engine changes are narrow: a central Lua creation facade, typed dynamic
store erasure with static fallback restoration, and record-change notifications
with pending authority snapshots. Multiplayer DTOs, packets, persistence,
permissions, and client transaction assembly live in multiplayer directories.
Native alchemy/enchanting contain only the connected-session safety hook until
their reusable mechanics are extracted.
