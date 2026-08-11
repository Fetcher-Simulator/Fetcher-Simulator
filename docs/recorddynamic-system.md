# Server-authoritative runtime records

This multiplayer branch treats load-time content and player-created runtime
records as different systems.

`openmw.content` remains an ordinary deterministic load-script API. Its changes
are part of the resolved content view; they do not allocate `$custom_*` IDs and
are never written to runtime-record tables. The server independently loads its
configured OpenMW content and load scripts, then the handshake compares ordered
base files, configured Lua script paths and hashes, content/API versions, the
runtime wire version, and a canonical post-load SHA-256.

Resolved-content identity is explicitly versioned. `ContentManifestVersion = 5`
and the `OMRC` v5 fingerprint cover the eight typed runtime-record stores
(Potion, Enchantment, Weapon, Armor, Clothing, Book, Dialogue/INFO, and Script)
plus the load-time
records that currently feed authoritative crafting mechanics: Ingredient,
Apparatus, MagicEffect, GameSetting, Skill, Class, Creature, and NPC. The
mechanics-only records use their own stable fingerprint tags and deterministic
little-endian field encodings; they are not being promoted to runtime-create
DTOs. Any future authoritative mechanic that consumes another load-script-mutable
record kind must add that record kind to the resolved fingerprint and bump the
content/fingerprint version. Runtime `$custom_*` records remain excluded from
resolved-content identity. Dialogue and Script overlays do not replace the
static baseline in that fingerprint: an override is a server-authoritative
bootstrap layer delivered only after the static manifest has matched.

The server derives the content, Lua-script, and resolved-record manifests from
its own `[content] openmw_cfg`; `Config.RESOLVED_CONTENT_FINGERPRINT` is only an
optional startup assertion. Multiplayer protocol negotiation also carries the
independent server-Lua-package manifest/API versions described below; those
versions do not alter OMDR or the resolved-content fingerprint.

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

## Typed server content: Dialogue/INFO and SCPT

Trusted server Lua may submit typed Dialogue and Script definitions with a
fixed ID and an explicit authoring mode:

- `new` requires that the ID does not collide with a static record.
- `override` requires a static Dialogue or Script with the same ID and is
  accepted only at the bootstrap activation boundary, before clients are in
  the session.
- `generated` remains the mode for the six player/runtime-created DTO kinds
  and is invalid for Dialogue and Script.

A Dialogue definition is one atomic aggregate containing the DIAL fields and
the complete ordered INFO sequence. INFO identity is `(dialogueId, infoId)`;
ESM `mPrev` and `mNext` links are derived from vector order during insertion
and are not persisted or sent. An override must retain every INFO ID referenced
by any durable journal entry. Dialogue and Script definitions are durable
content and are not garbage-collected through gameplay-state links.

A Script definition contains record flags and canonical UTF-8 source only.
Editor bytecode, variable tables, compiled programs, locals, and interpreter
state are not part of OMDR. The server compiles submitted source as validation;
clients insert the SCPT definition, invalidate cached program/locals, and
compile locally before execution. Installing or replicating a Script never
executes it. Optional whole-content compilation in multiplayer is deferred
until the runtime-definition bootstrap has completed.

Typed dependencies are extracted centrally from DTO fields and explicit
declared dependency IDs. SQLite stores those edges, and reconnect bootstrap
uses deterministic dependency-first order with persisted sequence as its
tie-breaker. The headless server also installs persisted Dialogue/Script
definitions into its effective store after restart while static-only resolved
fingerprinting remains unchanged.

Runtime record definitions remain separate from player/world gameplay state.
A dialogue result script is never replayed on another client to reproduce a
journal result; the resulting journal entry/index is the semantic state that is
persisted and replicated.

Server-supplied OpenMW Lua packages are now implemented as a separate
executable-policy system with their own manifest/API versions, deterministic
package-set hashing, bounded transfer/staging, a temporary multiplayer Lua/VFS
overlay, and a pre-world activation gate. They remain deliberately outside
RecordDynamic: Lua source packages are never encoded as OMDR definitions, and
package transport does not represent authoritative gameplay state. World entry
now requires both the runtime-definition bootstrap and the required server Lua
package set to be ready. See
[Server-supplied OpenMW Lua packages](server-lua-packages.md). Runtime package
hot reload remains deferred because the current `reloadlua` path cannot provide
package-set transactional rollback.

The first implemented authoritative gameplay-state example is `CrimeService`;
see [Semantic gameplay services](semantic-gameplay-services.md). Its revisioned
bounty/crime tuple is persistent character state sent by `PacketPlayerBounty`.
It is never an OMDR definition or `RecordDynamic` action.

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

Generic definitions are not a safe substitute for crafting rules. Both native
crafting flows are now server-authoritative for connected multiplayer; see
"Native alchemy" and "Native enchanting" below.

Native `alchemy` is fully server-authoritative (Stage 3) and native
`enchanting` (Stage 4) is fully server-authoritative, including the
Enchantment + owning-item record pair as one atomic transaction.

The headless content registry fingerprints the resolved Ingredient,
Apparatus, MagicEffect, GameSetting, Skill, Class, Creature, and NPC data
required by the authoritative crafting validators, in addition to the six
runtime DTO record kinds. This prevents load-script changes to crafting inputs
from passing the content handshake merely because the underlying
runtime-created output types still match.

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

## Native enchanting

Connected multiplayer enchanting is a semantic server-authoritative
transaction. The client sends only player choices; every calculated value is
derived on the server and committed atomically together with the
Enchantment + owning-item record pair.

### Architecture

```text
Native Enchanting UI
        |
        | semantic request (PacketEnchantingRequest)
        v
Client multiplayer enchanting coordinator (mwmp::EnchantingCreationManager)
        |
        v
Server EnchantingService / authoritative mechanics
        |   - validates player/inventory revision
        |   - validates exact target/soul-gem inventory instances
        |   - resolves item/soul/enchanter content through ServerContentRegistry
        |   - obtains authoritative character statistics
        |   - runs shared OpenMW mechanics (components/enchanting)
        |   - performs authoritative RNG (self-enchant only)
        |   - prepares the Enchantment + owning-item pair via DynamicRecordService
        |   - atomically commits gameplay state
        v
PacketEnchantingResult + RecordDynamic + authoritative inventory/stats
        |
        v
client applies authoritative definitions/state, then the native UI completes
```

### Semantic request contents

The request (`records::EnchantingRequest`) carries only genuine player choices:

- protocol/schema version and a stable client-generated request ID
- the expected inventory revision
- the exact target item inventory instance ID
- the exact soul gem inventory instance ID
- the selected cast style
- the custom item name
- self-enchant vs paid NPC service, with the enchanter's server-issued actor
  net ID for paid services
- the selected effects in UI order (effect ID, range, magnitude min/max,
  duration, area, and the target skill/attribute for TargetSkill/
  TargetAttribute effects)

The request never contains calculated effects costs, enchantment points,
charge, success, skill gain, gold price, canonical record IDs, or record
definitions. The packet decoder is bounded (effect count, name/ID lengths,
cast-style/range/magnitude/duration/area ranges, strict no-trailing-bytes) and
rejects malformed or oversized payloads.

### Exact inventory-instance validation

The server resolves the target and soul gem by exact `instanceId` in the
character's authoritative inventory mirror and verifies both exist with
count > 0, are not the same instance, and are owned by the requesting
character. The target must resolve to an enchantable content record: Weapon,
Armor, Clothing, or a scroll-type Book. The soul gem must be a Miscellaneous
record whose authoritative inventory `soul` field names a known Creature
record; the creature's soul value becomes the gem charge. A known content
record of the wrong type maps to `invalid_target_item`/`invalid_soul_gem`; an
unknown id maps to `content_mismatch`; an empty or unresolvable soul maps to
`empty_soul`/`invalid_soul`.

### Inventory revision behavior

Identical to alchemy: the request carries the expected revision, stale
requests are rejected without mutation and journaled, and the atomic commit
re-checks the revision inside the SQLite transaction. The client's
one-in-flight inventory mutation gate and the authoritative fast-path apply
unchanged.

### Authoritative content validation

Item fields (enchant capacity, weapon type, armor/clothing parts, book
scroll flag), magic-effect records (base cost, `AllowEnchanting`, CastSelf/
Touch/Target, TargetSkill/TargetAttribute flags), skills, classes, GMSTs, and
creature soul values are all resolved from `ServerContentRegistry`. Every
effect must exist, permit enchanting, use a range the effect supports
(constant effects are Self-only), and carry a valid target skill/attribute
when the effect requires one. Model/icon/script/body-part references of the
created item records are validated against the server VFS and content before
commit, like the alchemy path.

### Shared OpenMW mechanics

`components/enchanting/EnchantingMechanics` is a pure extraction of the
native `MWMechanics::Enchanting` calculation: per-effect costs (including the
vanilla running-total accumulation and the constant-effect duration
multiplier), enchantment points, cast cost, effective cast cost, capacity
check, success chance, projectile/ammo count and type multiplier, the
cast-style cycle, the barter-price core, and the success roll. The
single-player window and the server both call it, so the formulas cannot
diverge. The weapon-class column of the hardcoded weapon-type table is kept
in the shared layer for the same reason. Callers supply resolved records,
authoritative statistics, GMSTs, and an injected RNG.

### Authoritative RNG

The server constructs a fresh generator per request (or a fixed seed for
deterministic tests) and rolls only self-enchant attempts; paid services
never roll, exactly like native. The durable terminal result captures the
roll so retries replay the exact original outcome.

### Self-enchant vs paid NPC enchanting

The two native paths are modeled explicitly. Self-enchanting rolls the
player's chance (Enchant skill, Intelligence, Luck, fatigue term) and awards
player skill progression on success. Paid enchanting always succeeds, never
rolls, and never awards the player skill; the server validates the enchanter
actor identity, that its cell is loaded by the requester, and that it offers
the Enchanting service (from the NPC/Creature content record, or the class
record for autocalc NPCs). The price is the native barter formula evaluated
with authoritative inputs: player Mercantile/Luck/Personality and fatigue
from the synced player state, enchanter statistics from the NPC content
record, and a derived disposition (base disposition plus the race,
personality, and crime/bounty modifiers; faction-reaction, disease,
weapon-drawn, and charm terms are not representable in the sync model and
are omitted). Creature merchants keep the native base-price special case.
Player gold is checked and deducted authoritatively. The enchanter's gold
pool is not tracked by the sync model, so the native "gold added to the
NPC's pool" step is skipped; NPC runtime skill/attribute state is not synced,
so the enchanter's record base values are authoritative.

### Success/failure semantics

Mirrors native exactly: the soul gem is consumed on every accepted outcome
(and the Azura Star exception re-adds a fresh star); the target item is
consumed only on success; gold is charged only on success; skill progresses
only on successful self-enchants; a failed roll creates no records. A failed
roll is still an accepted, committed outcome (one revision). Zero-cost or
invalid setups are rejected before the roll, like the native UI pre-checks.

### Atomic pair creation

The successful transaction creates the `Enchantment` record and the owning
item record (Weapon/Armor/Clothing/Book-scroll) as one bundled preparation:
`DynamicRecordService::prepareSingleRecord` allocates the enchantment first,
its canonical `$custom_enchantment_<n>` ID is written into the owning item's
enchantment reference, and only then is the item's definition canonicalized,
fingerprinted, deduplicated, and allocated as `$custom_<type>_<n>`. Both
entries are committed in one SQLite transaction (dependency-first:
enchantment row before item row) together with inventory, gold, stats,
revision, and the journal. A partial state (enchantment without item, item
without enchantment, consumed gem without records) is impossible.

Deduplication follows the canonical layer: identical enchantment definitions
reuse one record, and identical owning-item definitions (same fields and
canonical enchantment reference) reuse one record. Because the item
fingerprint includes the canonical enchantment ID, items carrying different
enchantments never collapse onto each other. The native single-player
behavior of always inserting a fresh item record is deliberately tightened
to canonical dedup; the native getRecord-equivalent reuse of identical
dynamic enchantments is preserved through the fingerprint.

### Atomic gameplay commit

The success transaction atomically includes: the enchantment record, the
owning item record, the original target item removal (the whole enchanted
count for ammo/thrown stacks), the soul gem consumption (Azura Star
exception), the granted enchanted item stack, the gold deduction for paid
services, the skill progression award, the new player stats, the inventory
revision advancement, and the durable terminal journal row. Any failure
rolls everything back.

### Persistent idempotency

Identical to alchemy: the request is journaled in `craft_requests` with its
account, character, request ID, canonical semantic request hash, and terminal
result. Retries replay the exact stored result and can never consume a second
gem or item, deduct gold twice, grant another item pair, award skill twice,
advance the revision twice, or roll RNG twice. A reused request ID with a
different payload is rejected with `duplicate_request_conflict`.

### Result protocol and client barrier

`PacketEnchantingResult` carries the request ID, accepted/rejected status,
machine-readable error, success/failure, the canonical enchantment and item
record IDs, reused/new flags, the resulting inventory revision, and the
commit sequence. The client's `EnchantingCreationManager` holds completion
until the returned item record resolves in the local `ESMStore`, its
enchantment reference points at the returned enchantment ID, the
enchantment record itself resolves, and the authoritative inventory revision
is visible. Packet arrival order is irrelevant. Nothing is consumed or
created locally while pending; a disconnect fails pending requests cleanly
with an error.

### Reconnect/restart behavior

Identical to alchemy: durable results replay, and the persisted definition
pair is loaded at startup and re-sent before/with the restored inventory, so
an enchanted item held during reconnect resolves with its enchantment.

### Skill progression

On successful self-enchants the server awards the vanilla
`Enchant_CreateMagicItem` skill-use progression using authoritative
Skill/Class records and GMSTs, committed in the same transaction and pushed
to the client with the same stale-stats guard as the alchemy award.

### Error handling

Machine-readable `records::EnchantingError` codes distinguish invalid
requests, unsupported protocol versions, pending/conflicting duplicates,
stale revisions, missing/unowned/invalid targets, missing/unowned/invalid
soul gems, empty/invalid souls, duplicate source instances, invalid or
not-allowed effects, invalid magnitudes/durations/areas, exceeded capacity,
invalid cast styles, insufficient gold, invalid/unavailable enchanters,
mechanics validation failures, content mismatch, rate limits, quotas, and
server errors. Rejections are journaled so retries replay them.

### Logging

Every request produces a structured server log line correlating the player,
request ID, expected revision, target/gem instance IDs, enchantment mode
(self/paid) and cast style, effect count, success, the canonical
enchantment/item IDs, reused/new status, resulting revision, replay flag,
and error code.

### Current limitations

- NPC runtime statistics (fortified attributes, temporary disposition,
  diseases, charm) are not part of the sync model; paid-enchant pricing uses
  the enchanter content record's base values and the disposition subset
  described above.
- The enchanter's gold pool is not tracked; the native gold-pool credit is
  skipped.
- Autocalc NPC enchanters use their content-record fields (which are empty
  for autocalc stats); their class-derived service bit is honored.
- The native multi-stack ammo edge case (enchanted count clamped by the
  selected stack instead of the total refId count) is deliberately fixed to
  avoid minting items.
- Enchanting requests are rate-limited together with runtime record
  creation.
- The vanilla level-up dialog attribute counters are not synced (see
  alchemy).

## Trusted server-Lua compatibility

Potion, Enchantment, Weapon, Armor, Clothing, Book, Dialogue, and Script server-Lua writes use the
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
