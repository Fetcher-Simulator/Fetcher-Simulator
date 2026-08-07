# Server-authoritative runtime records

This multiplayer branch treats load-time content and player-created runtime
records as different systems.

`openmw.content` remains an ordinary deterministic load-script API. Its changes
are part of the resolved content view; they do not allocate `$custom_*` IDs and
are never written to runtime-record tables. The server independently loads its
configured OpenMW content and load scripts, then the handshake compares ordered
base files, configured Lua script paths and hashes, content/API versions, the
runtime wire version, and a canonical post-load SHA-256.

Resolved-content identity is explicitly versioned. `ContentManifestVersion = 2`
and the `OMRC` v2 fingerprint cover the six typed runtime-record stores
(Potion, Enchantment, Weapon, Armor, Clothing, and Book) plus the load-time
records that currently feed authoritative crafting mechanics: Ingredient,
Apparatus, MagicEffect, and GameSetting. The mechanics-only records use their
own stable fingerprint tags and deterministic little-endian field encodings;
they are not being promoted to runtime-create DTOs. Any future authoritative
mechanic that consumes another load-script-mutable record kind must add that
record kind to the resolved fingerprint and bump the content/fingerprint
version. Runtime `$custom_*` records remain excluded from resolved-content
identity.

The server derives the content, Lua-script, and resolved-record manifests from
its own `[content] openmw_cfg`; `Config.RESOLVED_CONTENT_FINGERPRINT` is only an
optional startup assertion.

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

Explicit server-owned record IDs are identity-preserving. When a trusted
server-Lua call supplies a fixed record ID, `DynamicRecordService` does not
deduplicate that request onto a different existing ID merely because the
definitions have the same fingerprint. Content-addressed deduplication remains
the normal policy for generated client/runtime records without a fixed identity.

## Permissions and validation

Generic client creation is default-deny. Configure exact script paths in
`Config.RUNTIME_RECORD_CAPABILITIES`; only Potion, Enchantment, Weapon, Armor,
Clothing, and Book have typed runtime-create DTO support. Every referenced static
ID and VFS asset is validated against the server's loaded content registry.
Generated-looking references are accepted only when present in the authoritative
catalog. Requests are rate- and quota-limited. The old
`Config.RUNTIME_RECORD_CONTENT_IDS` and `Config.RUNTIME_RECORD_ASSETS` lists are
deprecated and ignored.

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

The headless content registry now fingerprints the resolved Ingredient,
Apparatus, MagicEffect, and GameSetting data required by the upcoming semantic
alchemy/enchanting validators, in addition to the six runtime DTO record kinds.
This prevents load-script changes to crafting inputs from passing the content
handshake merely because the underlying runtime-created output types still match.

## Trusted server-Lua compatibility

Potion, Enchantment, Weapon, Armor, Clothing, and Book server-Lua writes use the
canonical parser/DTO/`DynamicRecordService` path. That path owns persistence,
provenance, validation, fingerprinting, idempotency, and canonical record data.

Historically trusted server scripts also create broader `RecordDynamic` types
such as NPCs and spells; Bardcraft depends on this for generated NPC definitions.
Those server-only writes remain supported through an explicit legacy
compatibility path until the corresponding typed OMDR DTOs exist. The
compatibility records retain the historical serialized Lua payload, are marked
with `creation_source = server_lua_legacy` and schema/validation version 0, and
remain readable across restart. They are never accepted from client record
proposals and do not weaken the client capability/default-deny boundary.

Unsupported legacy rows are kept intact and may retain a migration diagnostic
indicating that typed OMDR coverage is still unavailable. As broader record
types gain typed DTO support they should be removed from the legacy path and
routed through `DynamicRecordService`.

The legacy compatibility surface is intentionally not presented as the final
transaction model: calls such as `mp.setDynamicRecordDependencies` remain
separate from a legacy record upsert. Canonical typed bundles already support
atomic record/dependency transactions and are the required model for newly
typed record kinds.

## Upstream-facing surface

Generic engine changes are narrow: a central Lua creation facade, typed dynamic
store erasure with static fallback restoration, record-change notifications
with pending authority snapshots, and a headless-safe content-load boundary.
Multiplayer DTOs, packets, persistence, permissions, and client transaction
assembly live in multiplayer directories. Native alchemy/enchanting contain only
the connected-session safety hook until their reusable mechanics are extracted.
