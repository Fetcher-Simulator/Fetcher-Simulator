# Server-authoritative crime semantics

## Status and boundary

The semantic crime engine, production live-witness source, and native placed-
world Theft producer are active. Protocol 10 adds generation-bound witness state
and typed authoritative world-item take request/result packets. MWScript and
OpenMW Lua producers remain disconnected.

The authoritative pipeline is:

```text
validated gameplay cause
    -> CrimeIntent
    -> objective identity/cause validation
    -> witness eligibility
    -> ObservationService
    -> perceived / crimeSeen
    -> reporting stage
    -> bounty and/or currentCrimeId transition
    -> CrimeService
    -> durable PlayerCrimeState + semantic request journal
```

Runtime record definitions, player/world gameplay state, and executable server
Lua packages remain separate systems.

## Cause, perception, reporting, and state

`CrimeIntent` is a typed canonical cause. It carries the event identity, source,
crime type, cell, offender observation snapshot, optional canonical victim,
victim-awareness flag, crime value, server observation time, freshness limit,
and authoritative collision generations. The authenticated server context owns
the account, character, and player GUID. Evaluation rejects an offender identity
that does not match that context.

Neither `CrimeIntent` nor any wire format contains client-authored `crimeSeen`,
`reported`, or witness lists. Later producers obtain witness candidates through
`CrimeWitnessBuilder`, not a client-authored list.

The result retains separate facts for every witness and for the aggregate event:

- candidate inclusion by `fAlarmRadius`, with an out-of-radius victim retained;
- canonical/actor/relationship eligibility;
- the typed `ObservationResult` and its provenance;
- whether that witness perceived the cause;
- whether its Alarm value makes it report-capable;
- whether the reporting stage actually made it a reporter;
- aggregate `crimeSeen`, reporting-stage execution, bounty application, bounty
  delta, crime-ID advancement, and final `PlayerCrimeState`.

This intentionally follows the two native passes. Perception by any eligible
witness starts `reportCrime` semantics. The report pass then considers all
eligible candidates in its set, so the NPC that perceives and the NPC whose
`Alarm >= 100` applies bounty need not be the same actor. Running the report pass
advances `currentCrimeId` even when nobody reaches Alarm 100 and bounty remains
unchanged. An unseen accepted cause is durably journaled without changing the
crime-state revision.

## Live witness construction

`MPServer::buildLiveCrimeWitnesses` materializes a narrow live world view from
`mWorld.actorCells`. `CrimeWitnessBuilder` then resolves mechanics through
`MechanicsSnapshotRegistry` and produces the existing
`CrimeWitnessCandidate` DTO consumed by `CrimeSemanticService`.

The candidate-cell set reuses `collisionCellsForPlayer`, which is also the
Phase 4A collision-interest geometry. Interiors inspect only the event cell.
Exteriors always inspect the canonical event cell and add a neighboring cell
only when the alarm-radius circle intersects that cell's bounds. The server does
not scan the global actor registry. Candidate cells and actors are sorted and a
canonical `ObservationActorIdentity` is emitted at most once. A canonical victim
is resolved through the actor identity/location indices and can be included
outside the ordinary radius.

Every included actor must have a fresh accepted mechanics snapshot with the
same canonical kind, actor instance ID, cell, migration generation, and current
cell-or-lease authority generation. Missing, stale, wrong-cell, or
generation-mismatched state is rejected. Enabled, alive, conscious, position,
facing, Sneak/Agility/Luck/fatigue, and relevant effects come from that accepted
snapshot and retain `ActorAuthorityDelegated` provenance.

Only canonical `ObservationActorKind::Npc` identities can be vanilla crime
witnesses. A remote human player remains kind `Player` even if a client runtime
represents that human through an NPC-shaped proxy, and is therefore excluded.
Creatures are also excluded, matching native `canReportCrime` behavior.

### Alarm provenance

The server content store supplies `ESM::NPC::mAiData.mAlarm` as an explicitly
classified static fallback. A fresh protocol-10 actor-authority mechanics
snapshot supersedes it with the current `CreatureStats` Alarm base under
`ValidatedActorAuthorityDelegated` provenance. The snapshot is accepted only
for the canonical actor, current authority sender, migration generation,
authority generation, monotonically newer sequence, and freshness window.
Values outside 0 through 100 are rejected.

### Relationship provenance

Protocol 10 extends the same accepted atomic mechanics snapshot with recursive
player-follower membership and canonical combat-target identity. Actor authority
computes follower membership through `getActorsSidingWith(player)` and retains
the native Follow-package condition. Combat targets are encoded as canonical
player GUID or actor instance identity. A known follower or an actor whose
combat target equals the canonical victim is excluded; a known safe result can
continue to observation. Missing, stale, or generation-mismatched relationship
state remains `Unknown` and fails closed. The server never treats the absence of
an ActorAI package as proof of safety.

`/crimewitness [victimActorNetId]` calls the production builder and reports the
identity, cell, distance, generations, snapshot age, Alarm and relationship
provenance, and terminal inclusion reason. It shares the disabled-by-default
observation diagnostics switch and performs no mutation.

## Witness eligibility and observation

Candidates must be enabled, alive, conscious, within alarm radius (unless they
are the victim), and have a known safe relationship classification. Known player
followers and actors in combat with the victim are excluded. Relationship input
has explicit `ObservationAuthority` provenance. `Unknown` fails closed rather
than accepting an arbitrary client boolean. The current core does not create a
new ActorSync relationship subsystem; live producer wiring must derive these
classifications from authoritative state or a separately validated delegated
snapshot.

Eligible ordinary witnesses use `ObservationService` with the
`VanillaCrimeWitness` policy, canonical collision generations, server LOS, and
the accepted awareness calculation/cache. The special cases remain explicit:

- a victim-aware victim uses `ObservationPath::VictimAware`, including when the
  victim is outside ordinary alarm radius;
- a nearby eligible non-victim Murder witness uses
  `ObservationPath::MurderHearing` without inventing a successful LOS result.

Assault derives victim awareness in the semantic core, matching native
`commitCrime`.

## Authoritative placed-world take and Theft

Connected `ActionTake` and inventory-window pickup are request-first. They send
`WorldItemTakeRequest` and return without locally adding inventory, deleting the
world object, or running `itemTaken`. Accepted state arrives through the normal
authoritative inventory snapshot and canonical object-deletion replication.
Single-player retains the original synchronous path.

Content references use `(cellId, refId, RefNum.index, RefNum.contentFile)`;
server-placed objects use `(cellId, refId, mpNum)`. Mixed or incomplete keys are
invalid. The server resolves content references from its ordered content world,
and derives enabled/present state, count, transform, owner, faction/rank,
ownership global, gold conversion, charge, soul, item value, and record type.
It validates the authenticated player's canonical cell, fresh mechanics
position/generations, exact requested count, and interaction range. Server-
placed objects resolve from authoritative `mWorld.placedObjects` and are
unowned drops.

`world_taken_references` stores durable global tombstones and
`world_item_take_requests` stores terminal accepted request identity/results.
One SQLite `BEGIN IMMEDIATE` transaction inserts the tombstone, removes a
server-placed row when applicable, rewrites the authoritative inventory and its
dynamic-record links, advances the inventory revision, and journals the result.
The server then replicates the inventory and deletion. Cell bootstrap replays
content tombstones, so reconnect and restart cannot resurrect a taken reference.
Duplicate request identity returns the stored result; a different hash conflicts,
and a different request for the same reference is already-taken.

Ownership is evaluated from server content and authoritative player faction
state. An accepted unowned take performs no crime mutation. An owned take emits
the deterministic event `world-item-take:<account>:<character>:<request>` and
feeds the production witness builder and `CrimeSemanticService`. Crime value is
gold count for gold and `count * item value` otherwise; reporting retains
`max(1, int(value * fCrimeStealing))` in the semantic policy. The client never
supplies ownership, Theft, value, witness, bounty, or crime IDs.

## Persistence and idempotency

The canonical little-endian `OMCI` cause encoding is hashed with SHA-256. Durable
terminal results use the versioned `OMCS` encoding and the existing
`semantic_requests` table under the `crime-event` service namespace. The result
includes witness-stage distinctions and final authoritative state, so replay
after reconnect or server restart does not rerun observation.

The service checks the durable event before witness evaluation. Reusing an event
ID with the same canonical cause returns the stored result without another
awareness roll, bounty application, or crime-ID increment. Reusing it with a
different cause is a conflict. `CrimeService` remains the final mutation boundary
and atomically commits the terminal event alongside any `PlayerCrimeState`
transition. The database commit also supports accepted no-state-change terminal
events so unseen causes are idempotent.

No scripting-language operation is persisted or replayed. Only the semantic
cause result and resulting gameplay state are durable.

## Deferred work

The following remain explicit later Phase 4B work:

- native/MWScript/OpenMW Lua crime producer wiring;
- authoritative pickpocket detection and container transfer authority;
- connecting authoritative combat/damage events to Assault and Murder causes;
- relationship derivation beyond the fail-closed live witness adapter;
- dialogue complaints, disposition, Fight changes, faction expulsion, pursuit,
  and other actor reactions;
- player-versus-player witnessing;
- arrest, guard dialogue/UI, jail, fines, fine payment, and multi-domain fine
  transactions;
- werewolf exposure and respawn bounty repair.

Server Lua package distribution remains a separate executable-code system and is
not part of RecordDynamic or this crime-state service.
