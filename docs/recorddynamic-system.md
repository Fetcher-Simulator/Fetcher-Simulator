# Server-authoritative runtime records

This multiplayer branch treats load-time content and player-created runtime
records as different systems.

`openmw.content` remains an ordinary deterministic load-script API. Its changes
are part of the resolved content view; they do not allocate `$custom_*` IDs and
are never written to runtime-record tables. The server independently loads its
configured OpenMW content and load scripts, then the handshake compares ordered
base files, configured Lua script paths and hashes, content/API versions, the
runtime wire version, and a canonical post-load SHA-256.

Resolved-content identity is explicitly versioned. `ContentManifestVersion = 3`
and the `OMRC` v3 fingerprint cover the six typed runtime-record stores
(Potion, Enchantment, Weapon, Armor, Clothing, and Book) plus the load-time
records that currently feed authoritative crafting mechanics: Ingredient,
Apparatus, MagicEffect, GameSetting, Skill, and Class. The mechanics-only
records use their own stable fingerprint tags and deterministic little-endian
field encodings; they are not being promoted to runtime-create DTOs. Any
future authoritative mechanic that consumes another load-script-mutable record
kind must add that record kind to the resolved fingerprint and bump the
content/fingerprint version. Runtime `$custom_*` records remain excluded from
resolved-content identity.

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

Generic definitions are not a safe substitute for crafting rules. `enchanting`
operations are rejected until a headless server mechanics validator can
independently verify soul/gold state, capacity, costs, success, and randomness.
Connected native enchanting therefore performs no mutation and reports that
authoritative enchanting is unavailable. Enchanting is Stage 4 work; no part of
the Stage 3 alchemy implementation enables it.

Native `alchemy` is fully server-authoritative for connected multiplayer; see
"Native alchemy" below.

The headless content registry fingerprints the resolved Ingredient,
Apparatus, MagicEffect, GameSetting, Skill, and Class data required by the
authoritative alchemy validator, in addition to the six runtime DTO record
kinds. This prevents load-script changes to crafting inputs from passing the
content handshake merely because the underlying runtime-created output types
still match.

## Native alchemy

Connected multiplayer alchemy is a semantic server-authoritative transaction.
The client sends only player choices; every calculated value is derived on the
server and committed atomically.

### Architecture

```text
Native Alchemy UI
        |
        | semantic request (PacketAlchemyRequest)
        v
Client multiplayer alchemy coordinator (mwmp::AlchemyCreationManager)
        |
        v
Server AlchemyService / authoritative mechanics
        |   - validates player/inventory revision
        |   - validates exact source item instances
        |   - resolves ingredients/apparatus through ServerContentRegistry
        |   - obtains authoritative character statistics
        |   - runs shared OpenMW mechanics (components/alchemy)
        |   - performs authoritative RNG
        |   - creates/reuses canonical Potion records via DynamicRecordService
        |   - atomically commits gameplay state
        v
PacketAlchemyResult + RecordDynamic + authoritative inventory/stats
        |
        v
client applies authoritative definitions/state, then the native UI completes
```

### Semantic request contents

The request (`records::AlchemyRequest`) carries only genuine player choices:

- protocol/schema version and a stable client-generated request ID
- the expected inventory revision
- selected ingredient inventory instance IDs in slot order (2 to 4)
- selected apparatus inventory instance IDs (type derived from content)
- the requested potion name and the attempt count

The request never contains calculated effects, magnitudes, durations, weight,
value, success, skill gain, a canonical potion ID, or a potion definition.
The packet decoder is bounded (maximum slot counts, name length, attempt
count, strict no-trailing-bytes) and rejects malformed or oversized payloads.

### Exact inventory-instance validation

The server resolves every referenced source instance in the character's
authoritative inventory mirror by `instanceId` and verifies it exists, is
still present with count > 0, is not duplicated anywhere in the request, and
resolves to a loaded content record of the expected type (Ingredient for
ingredient slots, Apparatus for apparatus slots). Apparatus types must be
valid and unique (the native UI has one slot per type). A known content record
of the wrong type maps to `invalid_ingredient`/`invalid_apparatus`; an unknown
id maps to `content_mismatch`.

### Inventory revision behavior

Every request carries the client's expected inventory revision. The server
rejects any request whose revision does not match the authoritative revision
with `stale_inventory_revision`, without mutation, and journals the rejection.
The atomic commit re-checks the revision inside the SQLite transaction, so a
concurrent inventory change can never be overwritten by a stale request.

For ordinary client inventory mutations, only one revision-bearing snapshot may be in
flight at a time. Later local changes are coalesced until the authoritative reply
arrives. If that reply is semantically identical to the snapshot just sent, the
client treats it as a revision acknowledgement and skips the expensive live-store
reconciliation path; real server corrections still take the full authoritative
apply path. Small enchantment-recharge-only drift is coalesced to approximately
one update per second, while structural inventory changes, condition changes, and
meaningful charge drops still synchronize immediately. Respawn/full-sync inventory
uses the same one-in-flight revision gate.

### Authoritative character-state inputs

The server reads the character's authoritative statistics (Alchemy skill,
Intelligence, Luck — modified values, including fortify/drain) from its own
player state, never from the packet. The alchemy factor, success roll, and
potion statistics therefore use exactly what the server believes about the
character.

### Server content lookup

Ingredient effects/weights, apparatus quality, MagicEffect configuration
(base cost, flags), and every GMST used by the mechanics are resolved from the
server's `ServerContentRegistry` store. Skill and Class records feed the
skill-progression award. Potion models/icons and effect references are
validated against the server VFS and content before commit.

### Shared OpenMW mechanics

`components/alchemy/AlchemyMechanics` is a pure extraction of the native
`MWMechanics::Alchemy` calculation: ingredient effect matching, apparatus
quality application, magnitude/duration/value computation, ready-status
checks, potion weight/naming, and the per-attempt success roll. The
single-player window and the server both call it, so the formulas cannot
diverge. Callers supply resolved records, authoritative statistics, GMSTs, and
an injected RNG.

### Authoritative RNG

The server constructs a fresh generator per request (or a fixed seed when a
caller opts in for deterministic testing) and rolls every attempt itself. The
transaction's durable terminal result captures every roll, so a retry replays
the exact original outcome instead of rerolling.

### Persistent idempotency

Every alchemy request is journaled in the durable `craft_requests` table with
its account, character, request ID, canonical semantic request hash, and
terminal result. A retry with the same request ID and hash replays the exact
stored result (accepted or rejected) and can never consume ingredients again,
create another potion, grant skill again, advance the revision again, roll
RNG again, or allocate a second record. A retry with the same ID but a
different hash is rejected with `duplicate_request_conflict`; a pending entry
(only possible from legacy/crash states) reports `request_pending`.

### Atomic persistence

The whole attempt commits in one SQLite transaction: potion record
creation/reuse, ingredient consumption, the granted potion inventory stacks,
inventory revision advancement, skill progression, and the terminal journal
row. Any failure rolls everything back; no partial state (ingredients consumed
without a result, a definition without a grant, skill without a journal row,
etc.) can survive. The record/inventory/stats writes reuse
`PlayerDatabase::commitDynamicRecordRequest`.

### Potion DTO conversion and DynamicRecordService

Each successful attempt is converted to a typed `records::Potion` DTO,
canonicalized, validated, and fingerprinted through the canonical record
layer (`DynamicRecordService::prepareSingleRecord`). Native single-player
`getRecord` semantics are preserved: an existing dynamic Potion with the same
name/script/weight/value/flags/effects is reused even when the randomized
model/icon differs. Otherwise the record is allocated as `$custom_potion_<n>`
by the canonical ID service; identical definitions deduplicate to one reusable
record. Two players brewing the same recipe share one definition while
getting independent inventory grants.

### Client pending state and result barrier

The client's `AlchemyCreationManager` sends the request, marks it pending, and
prevents duplicate submissions until it completes. Nothing is consumed or
created locally while pending. On the result, completion waits until every
canonical potion ID resolves in the local `ESMStore` and the authoritative
inventory revision is visible, so the native UI never reports a result before
the record and inventory state are usable. Packet arrival order is
irrelevant; the barrier assembles the conditions. Disconnect completes pending
requests with an error and creates nothing locally.

### Disconnect/reconnect semantics

A disconnect before the server receives the request mutates nothing. After
receipt, the server completes or rejects the request atomically. After a
commit, the durable result replays on retry with the same request ID; the
dynamic potion definition is persisted, loaded at server startup, and sent to
the client before/with the restored authoritative inventory, so a potion held
during reconnect always resolves.

### Skill progression

The server awards the vanilla player skill-use progression for each
successful attempt (progress += skill use value / ((base+1) * class bonus
factors), level-up at progress >= 1, level progress per major/minor class
skills), using authoritative Skill/Class records and GMSTs. The updated
statistics are committed in the same transaction and pushed to the client;
a short-lived server guard rejects stale client stats snapshots that would
otherwise overwrite the award. The vanilla level-up dialog's attribute/skill
increase counters are not representable in the synced player state and remain
a pre-existing multiplayer limitation.

### Error handling

Machine-readable `records::AlchemyError` codes distinguish invalid requests,
unsupported protocol versions, pending/conflicting duplicates, stale
revisions, missing/unowned/invalid/wrong-type/duplicate ingredients,
missing/invalid apparatus, content mismatch, mechanics validation failures,
rate limits, quotas, and server errors. Rejections are journaled so retries
replay them.

### Logging

Every request produces a structured server log line correlating the player,
request ID, expected revision, selected ingredient/apparatus instance IDs,
attempt/success counts, the canonical potion ID, reused/new status, resulting
revision, replay flag, and error code, so a gameplay report can be traced back
to the exact request.

### Current limitations

- The vanilla level-up dialog attribute counters are not synced (see above).
- Skill-progression formulas reproduce the default (unmodded) player
  skillhandlers; overridden player skill scripts are not replicated server-side.
- Alchemy requests are rate-limited together with runtime record creation.
- Multiplayer enchanting remains disabled (Stage 4).

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
