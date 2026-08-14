# Server-authoritative awareness audit

Status: Phase 4A.2 design decision, 2026-08-14

This report audits the current source at `1a8b706d55782d3a169923514434661e775f99a0`.
It is intentionally limited to perception infrastructure. It does not design or
implement `CrimeIntent`, protocol 9, arrest, or broad native crime networking.

## Decision

The correct current classification is:

```text
SERVER-AUTHORITATIVE LOS + NARROWLY DELEGATED AWARENESS INPUTS
```

The server can own candidate selection, static content, GMSTs, awareness rolls,
collision, freshness checks, and the final observation decision. It cannot yet
truthfully call the complete decision server-authoritative because several live
mechanics inputs are either actor-authority delegated or not synchronized at all.

Full server authority remains practical without running a full actor simulation.
It requires the server to own or validate a small, typed mechanics snapshot. It
must not accept a client-computed `crimeSeen` or witness list.

## 1. Vanilla awareness and crime call graph

The public mechanics interface is declared in
`apps/openmw/mwbase/mechanicsmanager.hpp`. The implementation is in
`apps/openmw/mwmechanics/mechanicsmanagerimp.cpp`.

Crime entry points include:

- sleeping in an owned bed: `sleepInBed()` -> `commitCrime()` at line 944;
- trespass while unlocking: `unlockAttempted()` -> `commitCrime()` at line 962;
- confiscation/theft helpers at lines 1043 and 1132;
- assault: `actorAttacked()` -> `commitCrime()` at line 1515;
- murder: `actorKilled()` -> `commitCrime()` at line 1598;
- OpenMW Lua: `apps/openmw/mwlua/types/player.cpp`, `_runStandardCommitCrime`;
- pickpocket detection: `apps/openmw/mwmechanics/pickpocket.cpp`, which performs
  its own uncached skill roll before the later crime-reporting path.

The current witnessed-crime flow is exactly:

```text
crime-producing client mechanic
    -> MechanicsManager::commitCrime(player, victim, type, faction, arg, victimAware)
        -> reject when the offender is not the local player
        -> assault forces victimAware = true
        -> Actors::getObjectsInRange(player position, fAlarmRadius)
        -> append an out-of-radius victim
        -> MechanicsManager::getActorsSidingWith(player)
        -> for each neighbor
            -> canReportCrime(neighbor, victim, followers)
                -> reject local player
                -> reject non-NPC
                -> reject dead
                -> reject combat with victim
                -> reject knocked down
                -> reject a recursively resolved player follower with Follow package
            -> perceive when any path succeeds
                -> neighbor is victim and victimAware
                -> murder and neighbor is not victim (hearing special case)
                -> World::getLOS(player, neighbor)
                   and awarenessCheck(player, neighbor, useCache=true)
            -> theft/pickpocket witness may say "thief"
            -> set aggregate crimeSeen
        -> if crimeSeen
            -> reportCrime(...)
        -> otherwise, assault has a guard/combat fallback
```

`reportCrime()` is a separate reaction/reporting pass. It gathers the same alarm
radius again, computes offense bounty/disposition/Fight values, and determines
`reported` from eligible actors whose base Alarm is at least 100. It then applies
disposition changes, guard pursuit, combat, crime IDs, faction expulsion, and the
local player's bounty. Therefore `crimeSeen`, `reported`, and bounty mutation are
three distinct states.

Special paths that must be preserved:

- Assault always treats the victim as aware.
- The caller-provided `victimAware` flag can guarantee victim perception for
  other offenses, including OpenMW Lua and pickpocket paths.
- An eligible non-victim NPC inside alarm range can report murder without LOS or
  `awarenessCheck()`; this is the existing hearing rule.
- Perception does not itself mean reporting. Base Alarm >= 100 is evaluated later.
- Remote human player proxies must not enter the server's NPC candidate set.

### Exact awareness formula

`MechanicsManager::awarenessCheck()` is at
`apps/openmw/mwmechanics/mechanicsmanagerimp.cpp:1601`.

It first rejects a dead or disabled observer. It then computes:

```text
if target is sneaking:
    sneakTerm = fSneakSkillMult * targetSneak
              + 0.2 * targetAgility
              + 0.1 * targetLuck
              + targetBootWeight * fSneakBootMult
else:
    sneakTerm = 0

distTerm = fSneakDistanceBase
         + fSneakDistanceMultiplier * distance(target, observer)

x = sneakTerm * distTerm * targetFatigueTerm + targetChameleon
if targetInvisibility > 0:
    x += 100

observerTerm = observerSneak
             + 0.2 * observerAgility
             + 0.1 * observerLuck
             - observerBlind

if the observer has a base node/facing:
    if target is more than 90 degrees behind observer:
        y = observerTerm * observerFatigueTerm * fSneakNoViewMult
    else:
        y = observerTerm * observerFatigueTerm * fSneakViewMult
else:
    y = 0

threshold = x - y
detected = awarenessRoll >= threshold
```

Boot weight applies only to a sneaking NPC target that `World::isOnGround()`
reports on ground. `isSneaking()` first requires the CreatureStats sneak stance,
then accepts either the character-controller sneak state or the actor being
airborne while neither swimming nor flying.

`CreatureStats::getFatigueTerm()` computes:

```text
normalised = floor(modifiedMaximumFatigue) == 0
    ? 1
    : max(0, currentFatigue / modifiedMaximumFatigue)

fatigueTerm = fFatigueBase - fFatigueMult * (1 - normalised)
```

Client LOS first requires both actors enabled and active. Physics casts from each
actor collision-object position plus 90% of its vertical half-extent, against
world, terrain, and door collision. The client physics subsystem separately
caches unordered actor-pair LOS results and refreshes them during physics work;
that cache is not the awareness-roll cache.

## 2. Awareness input authority matrix

Authority labels in this report mean:

- **SERVER OWNED**: selected or derived from server state/content;
- **SERVER VALIDATED**: client-originated but checked against a server invariant;
- **ACTOR-AUTHORITY DELEGATED**: accepted only from the current cell/actor
  authority, but not mechanics-validated;
- **PLAYER-CLIENT DELEGATED**: accepted from the authenticated player's client;
- **CLIENT PRESENTATION ONLY**: useful visual flag, not mechanics evidence;
- **NOT AVAILABLE**: absent from current server state.

| Input | Current client source | Server currently has? | Current authority | Freshness/revision | Required change |
|---|---|---:|---|---|---|
| observer canonical identity | live `MWWorld::Ptr` | yes for ActorSync actors | SERVER OWNED identity | actor migration generation | Reuse deterministic `ActorInstanceId` |
| observer actor kind | `Class::isNpc()` | derivable from authoritative content | SERVER OWNED when derived | content manifest/dynamic-record revision | Add explicit Player/Npc/Creature result; never trust proxy class |
| observer cell | active `CellStore` | yes | SERVER OWNED migration commit; actor list alone is not migration | migration generation | Use canonical `actorLocations`/cell bucket |
| observer position | `RefData::Position` | yes | ACTOR-AUTHORITY DELEGATED | high-rate sequence/server receipt time; migration generation | Add mechanics snapshot freshness and bounds checks |
| observer facing | base-node attitude | yaw/rotation exists | ACTOR-AUTHORITY DELEGATED | position stream sequence/time | Normalize forward vector; reject missing/stale facing |
| observer enabled | `RefData::isEnabled()` | no | NOT AVAILABLE | none | Typed mechanics snapshot or server-owned object state |
| observer dead | `CreatureStats::isDead()` | yes | server protects some death transitions, otherwise delegated | actor snapshot time; durable for persistent deaths | Reuse canonical death state with freshness |
| observer knocked down/unconscious | `getKnockedDown()` | only coarse movement flags; knockout inferred from fatigue | CLIENT PRESENTATION ONLY | presentation stream | Add explicit mechanics-grade conscious state |
| observer Sneak skill | runtime modified skill | no for NPC/creature | NOT AVAILABLE | none | Typed modified-value snapshot; creatures need resolved stealth substitute semantics |
| observer Agility | runtime modified attribute | no for NPC/creature | NOT AVAILABLE | none | Typed modified-value snapshot |
| observer Luck | runtime modified attribute | no for NPC/creature | NOT AVAILABLE | none | Typed modified-value snapshot |
| observer fatigue | `CreatureStats::getFatigue()` | yes | ACTOR-AUTHORITY DELEGATED | `ActorStatsDynamic` receipt timestamp, no field revision | Put in coherent mechanics snapshot |
| observer Blind magnitude | runtime MagicEffects | no | NOT AVAILABLE | none | Typed relevant-effect magnitudes or server-owned effect state |
| target authenticated identity | local player object | yes | SERVER OWNED identity | connection/character lifetime | Reuse GUID + character identity |
| target cell | player world state | yes | PLAYER-CLIENT DELEGATED except scripted teleports | packet sequence/receipt time | Validate cell/position coherence |
| target position | `RefData::Position` | yes | PLAYER-CLIENT DELEGATED; `validateMovement()` currently always returns true | sample timestamp/packet sequence | Movement validation or explicit delegated classification |
| target sneak stance | CreatureStats + controller/world movement | player animation flags exist | PLAYER-CLIENT DELEGATED / partly presentation | high-rate packet | Add mechanics-grade stance and locomotion context |
| target on-ground/swim/fly state | client physics/world | no mechanics-grade state | NOT AVAILABLE | none | Synchronize narrow locomotion context or derive from server collision |
| target Sneak skill | runtime modified skill | yes for player | PLAYER-CLIENT DELEGATED, persisted server mirror | stats packet; no observation snapshot generation | Include in coherent snapshot; validate semantic stat changes |
| target Agility | runtime modified attribute | yes for player | PLAYER-CLIENT DELEGATED, persisted server mirror | stats packet | Same |
| target Luck | runtime modified attribute | yes for player | PLAYER-CLIENT DELEGATED, persisted server mirror | stats packet | Same |
| target fatigue | `CreatureStats::getFatigue()` | yes | PLAYER-CLIENT DELEGATED | stats packet | Same |
| target boots/weight | equipped slot and content weight | equipment ID yes; on-ground missing | equipment PLAYER-CLIENT DELEGATED, weight SERVER OWNED | equipment revision exists for player inventory lane | Resolve weight from authoritative content; never accept weight float |
| target Chameleon magnitude | runtime MagicEffects | no | NOT AVAILABLE | none | Typed relevant-effect magnitude or server-owned effect state |
| target Invisibility magnitude | runtime MagicEffects | no | NOT AVAILABLE | none | Same |
| distance | positions | derivable | mixed from endpoint authority | endpoint snapshot generations | Derive server-side from accepted endpoints |
| front/behind relation | base-node facing and positions | derivable | mixed from endpoint authority | endpoint snapshot generations | Derive server-side, handle zero-length vectors deterministically |
| GMST values | ESMStore | yes | SERVER OWNED | resolved-content fingerprint | Read from `ServerContentRegistry` |
| LOS geometry | client physics | benchmark proves server backend | SERVER OWNED when productionized | collision-cell generation | Per-cell collision lifecycle and dynamic blockers |
| LOS eye endpoints | actor collision half-extents | not exactly; benchmark uses fixed +128 | NOT AVAILABLE exactly | none | Resolve/query actor half-extents or synchronize validated eye height |
| awareness roll | world PRNG / observer CreatureStats cache | no server cache yet | can be SERVER OWNED | five-second observer cache | Dedicated server roll source/cache |

The static NPC/creature record contains base attributes and AI data, but that is
not a substitute for the modified runtime values used by `awarenessCheck()`.
Autocalculation, magic effects, scripted changes, damage, and live fatigue matter.

## 3. ActorSync state matrix

The DTO is `components/openmw-mp/Base/BaseActor.hpp`. The deterministic identity,
compact position stream, authority generations, and migration helpers are in
`components/openmw-mp/Base/ActorSyncProtocol.hpp`. Server storage is
`MPServer::ActorRegistryRecord` and `CellActorState` in
`apps/openmw-server/Server.hpp`.

| Remote actor field | Present? | Classification | Notes |
|---|---:|---|---|
| deterministic instance identity | yes | SERVER OWNED | Packed vanilla refNum or spawned mpNum |
| refId | yes | SERVER VALIDATED/CONTENT RESOLVED | Generated equipment IDs are catalog-checked |
| actor kind | not explicit | SERVER DERIVABLE | Resolve refId against NPC/Creature stores; Player is a separate GUID namespace |
| canonical cell | yes | SERVER OWNED migration | `ActorCellChange` owns migration; `ActorPositionV2` is not canonical migration |
| position | yes | ACTOR-AUTHORITY DELEGATED | Stored after authority/lease and identity checks |
| rotation/facing | yes | ACTOR-AUTHORITY DELEGATED | Included in full and compact position state |
| velocity | yes | ACTOR-AUTHORITY DELEGATED | Primarily interpolation/presentation |
| dead state | yes | MIXED | Delegated transition with server persistence/tombstone protections |
| knocked down | coarse flag | CLIENT PRESENTATION ONLY | Not an authoritative mechanics field |
| knocked out | coarse flag | CLIENT PRESENTATION ONLY | Sender infers from fatigue; no explicit semantic state |
| enabled | no | NOT AVAILABLE | Required by vanilla LOS/awareness eligibility |
| health/magicka/fatigue | yes | ACTOR-AUTHORITY DELEGATED | Server stores and persists spawned-actor dynamic stats |
| attributes | no | NOT AVAILABLE | Base content exists, modified runtime state does not |
| skills/creature stealth | no | NOT AVAILABLE | Required by awareness |
| active magic effects | no | NOT AVAILABLE | Chameleon/Invisibility/Blind cannot be reconstructed from spell IDs |
| AI package and target | yes | ACTOR-AUTHORITY DELEGATED | Follow/Escort/Combat/Pursue/Travel/Wander only |
| Alarm/Fight/Flee/Hello | static base in content only | SERVER OWNED base, live modified unavailable | Reporting uses base Alarm; reaction mutates Fight later |
| recursive follower/alliance relation | partial | ACTOR-AUTHORITY DELEGATED | Current AI target/package is not a complete siding graph |
| combat with victim | partial | ACTOR-AUTHORITY DELEGATED | Current single AI target does not represent every combat relation |
| equipment | yes | ACTOR-AUTHORITY DELEGATED IDs; content weight SERVER OWNED | Spawned-actor DB currently does not persist equipment |
| cell authority generation | yes | SERVER OWNED | Cell-level authority epoch |
| actor authority generation/lease | yes server-side | SERVER OWNED | Not a mechanics snapshot revision |
| migration generation | yes | SERVER OWNED | Wrap-safe comparison helper exists |
| snapshot sequence/timestamp | yes batch-level | SERVER OWNED on accepted relay | Separate packets do not form one atomic mechanics snapshot |

`validateActorUpdate()` verifies that the sender owns the cell or a valid
per-actor authority lease. That is provenance validation, not validation of the
reported gameplay values.

The registry is already suitable for candidate lookup: `actorCells` buckets by
canonical cell and `actorLocations` indexes canonical actor location. Exterior
neighbor cells should be selected only when the alarm-radius sphere crosses a
cell boundary. A second actor registry would be redundant.

### Remote human player identity

`RemotePlayer::ensureMechanicsRegistration()` in
`apps/openmw/mwmp/sync/RemotePlayer.cpp` adds the local NPC proxy to the client
`MechanicsManager`. Vanilla `canReportCrime()` only sees that the proxy is an NPC.
The server must not copy that behavior. Human players live in the authenticated
player registry and are `ObservationActorKind::Player`; ActorSync NPC/creature
records use `ActorInstanceId`. Only canonical `Npc` candidates participate in
vanilla crime reporting. Creatures already fail vanilla `canReportCrime()` and
must not be silently promoted to witnesses.

## 4. RNG and cache analysis

The awareness cache is in `MWMechanics::CreatureStats`:

- `mAwarenessRoll` starts at `-1`;
- `getAwarenessRoll()` lazily draws `Misc::Rng::roll0to99(world PRNG)` and stores it;
- `updateAwareness(duration)` accumulates per-observer time;
- at five seconds it resets the timer to zero and invalidates the roll;
- the cached roll is keyed only by observer because it is a CreatureStats field;
- all cached awareness checks against all targets reuse that observer roll;
- overshoot above five seconds is discarded rather than carried forward;
- `useCache=false` draws immediately from the world PRNG and does not update the
  cached roll; AI combat and pursuit use this path;
- the cache is not serialized and therefore resets with runtime actor state.

Crime uses the default cached path. An actor-pair cache or event-derived roll per
crime would change gameplay by removing the shared observer luck window.

Recommended server implementation:

1. Maintain one transient cache entry per canonical observer identity.
2. Store roll, expiry/elapsed state, and the observer authority generation used.
3. Invalidate on observer removal, identity-generation change, or five seconds of
   the service's authoritative monotonic update time. Movement/state changes do
   not reroll in vanilla and should not do so here.
4. Draw from a dedicated server-owned roll source, not an unrelated client PRNG.
5. Make clock and roll source injectable for deterministic tests.
6. Persist the terminal semantic observation/crime result under its request ID
   later. Do not persist the transient roll solely for restart equivalence;
   vanilla does not.

Exact client PRNG stream equivalence is neither attainable nor desirable in an
authoritative multiplayer server. Formula and cache lifetime compatibility are
the relevant guarantees.

## 5. ObservationService design

`ObservationService` is a generic server domain service. It does not own crimes,
Alarm reactions, bounty, dialogue scripts, AI, actor simulation, or executable
Lua distribution.

```text
canonical actor/player registries
        +
resolved content and GMSTs
        +
typed mechanics snapshots
        +
ServerCollision backend
        +
server awareness-roll cache
        |
        v
ObservationService::observe(query)
        |
        v
validated semantic ObservationResult
```

### Identity

Use a tagged identity:

- `Player { guid }`;
- `Npc { ActorInstanceId }`;
- `Creature { ActorInstanceId }`.

The tag is server-resolved. Player GUID zero and invalid ActorInstanceId values
are rejected. An actor's refId is content metadata, not sufficient instance
identity.

### Query

A query should contain:

- request/event ID and observation timestamp;
- observer and target/event identities;
- victim identity when a victim-aware path is possible;
- canonical worldspace/cell;
- observer and target/event endpoints and facing;
- actor migration, actor-authority, and mechanics snapshot generations;
- expected collision-cell generation(s);
- requested path: normal LOS+awareness, guaranteed victim awareness, or murder
  hearing;
- the typed numeric awareness inputs and their provenance/freshness.

The service should not accept `clientCrimeSeen`, a client witness list, or a
scripting-language command.

### Result

A result should contain:

- observable yes/no;
- LOS outcome and whether LOS was bypassed;
- awareness outcome, roll, and threshold when evaluated;
- path/reason such as observer-ineligible, stale actor snapshot, collision
  generation mismatch, victim-aware, murder-hearing, blocked LOS, or awareness
  failure;
- identity and all generations used;
- freshness metadata;
- explicit authority classification, including every delegated input source.

The result becomes input to a later reporting/reaction service. It does not
itself apply Alarm, disposition, Fight, pursuit, faction, crime IDs, or bounty.

### Freshness and invalidation

- Reject, rather than guess, when a required mechanics snapshot is absent or
  older than the configured event-time tolerance.
- Reject snapshots whose actor authority or migration generation no longer
  matches the registry.
- A cached LOS result, if added after profiling, must key endpoint snapshot
  generations plus all traversed collision-cell generations.
- Actor movement changes endpoint generation. A door or dynamic blocker update
  increments its collision-cell generation.
- The awareness roll cache is independent from LOS and actor snapshot caches.

### Candidate selection

Use `mWorld.actorCells` and `mWorld.actorLocations` as the source. For an interior,
inspect one canonical cell bucket. For an exterior, include adjacent buckets only
where the alarm-radius sphere crosses a cell edge. Apply cheap filters before LOS:

1. canonical kind is NPC, never Player or Creature;
2. cell/worldspace eligibility;
3. distance;
4. enabled, alive, and conscious;
5. victim combat and recursive follower exclusions;
6. snapshot freshness/generations;
7. special victim/hearing path or LOS;
8. awareness formula.

## 6. Remaining delegated inputs

The current minimal delegated set is larger than desirable:

- actor-authority client: live NPC transform/facing, fatigue, conscious state,
  modified attributes/skill, relevant effects, and AI relation state;
- offender's authenticated client: player transform, sneak/locomotion state,
  modified stats, fatigue, equipment state, and relevant effects.

The server currently validates sender identity/authority, content IDs, and actor
generation invariants around these values. It does not validate the mechanics
values themselves. A mechanics snapshot packet would narrow and version this
trust but would not eliminate it.

Eliminating delegation requires server-owned semantic mutations for relevant
stats/effects/stances or a small server simulation of those values. It does not
require rendering, navigation, animation, character controllers, or full combat
AI. Until then, every result must expose delegated provenance.

## 7. Collision production plan

### Reusable benchmark code

Keep:

- `ServerContentRegistry`, `ResourceSystem`, `WorldModel`, and ESMStore setup;
- `BulletShapeManager` and query-only `btCollisionWorld` ownership;
- static blocker selection and terrain heightfield construction;
- physical collision masks used by OpenMW LOS;
- `hasLineOfSight()` ray logic;
- benchmark scenario/reporting support and `--collision-benchmark`.

Move out of the production core or leave benchmark-only:

- RSS/proc parsing;
- CSV and stdout formatting;
- synthetic actor sample collection and fixed `+128` endpoints;
- synthetic pair creation, throughput loops, and scenario definitions;
- global `load()`/`clear()` assumptions.

### Cell lifecycle

Introduce a canonical collision-cell key and `CellCollisionState` containing:

- refcount;
- monotonically increasing generation retained across unload/reload;
- static collision entries;
- terrain entry;
- dynamic blocker handles keyed by canonical object identity;
- load/error state and diagnostics.

`acquireCell()` loads only on refcount transition 0 -> 1. `releaseCell()` removes
Bullet objects on 1 -> 0. The server's player/actor interest-cell calculation
should own acquisitions. The collision service must not invent a second world
interest policy.

### Doors and dynamic blockers

The current `DoorEntry` is insufficient for exact live transforms. Production
work should:

1. make the server's accepted final door state/revision authoritative;
2. resolve the door's canonical identity and closed transform from content/cell;
3. apply a deterministic final transform to its Bullet object;
4. update its AABB;
5. increment that cell's collision generation;
6. invalidate observations using the old generation.

Intermediate moving-door transforms should be added only with a server-owned
timeline or explicit authoritative transform. A boolean from an arbitrary client
must not be presented as stronger authority than it has. Missing meshes should be
diagnostic and fail open for that blocker, matching the benchmark's resilience,
while retaining counters suitable for operations.

### Threading

Initial observation queries can remain synchronous on the server event thread:
measured event-driven cost is negligible. Cell mutation and ray queries must have
one documented owner or a lock around the Bullet world. Do not introduce worker
complexity before profiling production traffic.

## 8. Implementation phases

### Phase 4A.4 implementation status

Protocol 9 now carries one versioned, atomic `MechanicsSnapshot` representation
for authenticated players and actor-authority NPCs/creatures. The server derives
the canonical subject/kind/cell/generations, validates sender entitlement and a
strictly increasing snapshot sequence, and replaces the accepted transient
snapshot only after complete validation. Freshness uses server receipt time;
these volatile snapshots are intentionally not written to SQLite.

Live observation now follows this path:

```text
protocol-9 MechanicsSnapshot
    -> MechanicsSnapshotRegistry
    -> current actor/player registry generation check
    -> mWorld.actorCells candidate bucket
    -> canonical NPC + distance + eligibility filters
    -> current collision-cell generations
    -> ObservationService
    -> diagnostic ObservationResult
```

Connected players own collision cells derived from their authenticated current
cell and accepted position. Interiors own one cell. Exterior ownership adds only
cells whose rectangle intersects the `fAlarmRadius` circle; reported loaded-cell
lists and actor-authority leases do not retain collision geometry. Set-difference
transitions acquire before release, and final-owner release unloads the cell
while preserving generation history.

Door state is also versioned under protocol 9. Client proposals require an exact
next durable revision, a fresh player mechanics snapshot, a unique static door
instance in an owned collision cell, and server-checked activation distance.
Duplicate state, stale revision, lock mutation, unknown identity, and dynamic
doors without authoritative collision geometry are rejected. Accepted state is
persisted to `world_doors`, echoed/broadcast as a semantic final state, and then
applied through `ServerCollisionWorld::setDoorOpen`, which advances the cell
generation. Existing rows migrate to revision 1.

The disabled-by-default `OBSERVATION_DIAGNOSTICS_ENABLED` switch exposes the
bounded in-game command `/observe <actorNetId|0> [targetPlayerGuid]`. It reports
identity, distance, snapshot/authority age and generations, collision
generations, LOS, awareness, semantic reason, and provenance. It does not mutate
crime, bounty, Alarm, AI, dialogue, or any player/world gameplay state.

The accurate authority classification remains:

```text
SERVER-AUTHORITATIVE LOS + VALIDATED ATOMIC DELEGATED MECHANICS SNAPSHOTS
```

Player/NPC position and facing, enabled/alive/conscious flags, sneak/on-ground
state, modified Sneak/Agility/Luck, fatigue, Chameleon, Invisibility, and Blind
remain delegated to the authenticated player or current actor authority. The
server owns identity, canonical kind, cell/generation expectations, freshness,
candidate selection, static content and GMSTs, boot-weight derivation,
collision, awareness rolls, formula evaluation, and the final observation
result. Live humanoid LOS endpoints currently use a server-owned 128-unit eye
height pending content-derived actor collision bounds.

1. **Audit and pure domain foundation**: this report, typed identity/authority
   metadata, exact formula, observer roll cache, special observation paths, and
   focused tests. No protocol/database change.
2. **Collision lifecycle**: split benchmark instrumentation from reusable
   per-cell backend; refcounts, generations, and regression tests; preserve CLI.
3. **Door correctness**: authoritative accepted revision/final transform and
   collision invalidation; persistence migration as needed.
4. **Mechanics snapshot lane**: add only the audited inputs, coherent generation
   and freshness, protocol bump, validation, client producers, and reconnect
   bootstrap.
5. **Registry integration**: canonical kind resolution, candidate lookup, dynamic
   record compatibility, and live ObservationService queries.
6. **Crime semantics later**: `CrimeIntent`, witnessed result, reporting/reaction,
   and durable `CrimeService` transition. Preserve victim and murder special cases.

## 9. Required tests

Foundation tests:

- exact formula boundaries and fatigue term;
- sneaking versus non-sneaking target;
- front/behind multipliers;
- Chameleon, Invisibility, and Blind;
- boot weight only under the vanilla target conditions;
- dead/disabled/unconscious observer rejection;
- victim-aware and murder-hearing paths bypass LOS for the correct identities;
- remote Player kind rejected as a vanilla NPC witness;
- one roll shared by multiple targets for one observer;
- reroll at five seconds, no movement-triggered reroll, and generation invalidation;
- explicit mixed/delegated authority result.

Collision tests:

- duplicate acquire increments refcount without duplicate Bullet objects;
- release does not unload before refcount reaches zero;
- unload/reload and door transforms increment generation;
- stale generation queries are rejected;
- missing mesh handling and terrain lifetime;
- door open/close affects representative LOS;
- benchmark mode remains functional.

Integration tests after a snapshot wire exists:

- reconnect receives current actor identity and fresh perception snapshots;
- restart restores persistent actors and final door states, then rebuilds
  collision generations without reusing stale cache entries;
- duplicate actor records/identities do not create duplicate witnesses;
- spawned actors referencing dynamic NPC/equipment records resolve after
  deterministic dynamic-record bootstrap;
- actor migration rejects stale-source observations;
- remote human player proxy is never selected as an NPC witness;
- victim-aware assault and pickpocket paths remain distinct from normal LOS;
- murder hearing remains LOS-independent for eligible non-victim NPCs;
- observed but low-Alarm NPCs do not imply reported crime;
- journal restoration still succeeds when its quest/topic/dialogue definitions
  were supplied dynamically before world entry;
- dialogue result scripts are not replayed on other clients when the semantic
  journal/crime result is restored or replicated.

## Deferred systems

Server Lua packages remain a separate executable-code distribution and
compatibility-override system. They must not be stored in RecordDynamic or treated
as runtime record definitions. Existing trusted legacy server-Lua records remain
compatible until typed replacements exist.

Crime packets, arrest, guard-specific architecture, respawn bounty convergence,
MWScript crime opcode routing, and werewolf exposure remain separate follow-up
work.
