# Server-authoritative crime semantics

## Status and boundary

Phase 4B.1 provides a server-only, independently testable semantic crime engine.
Phase 4B.2 adds a production live-witness source, but intentionally does not
connect a native crime producer because the current pickup path fails the
authoritative-cause gate described below. MWScript and OpenMW Lua producers also
remain disconnected. There is no packet or protocol change.

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

The server content store supplies `ESM::NPC::mAiData.mAlarm`, classified as
`CrimeAlarmProvenance::StaticContentBase`. Values outside the ESM Alarm range
0 through 100 are rejected. Missing or unavailable provenance cannot become a
reporter.

This is explicitly the static content base, not a claim about the NPC's current
effective live Alarm. Native `SetAlarm` can modify runtime AI settings, and the
current actor/mechanics snapshots do not carry a validated effective Alarm
value. A later effective-AI-state authority layer may supersede the base value;
it must preserve explicit provenance and freshness.

### Relationship provenance

The actor registry contains delegated ActorAI packages, but it does not yet
record an independently fresh, generation-bound assertion suitable for proving
crime follower/combat relationships. The production adapter therefore sets
`CrimeRelationshipProvenance::Unavailable` and `Unknown`; the builder fails
closed. It does not infer a safe relationship merely because the latest package
is not `Follow` or `Combat`, and it does not accept a new client boolean.

The typed builder can consume `ServerAuthoritative` or
`ValidatedActorAuthorityDelegated` relationship classifications once such a
source exists. Known followers and actors in combat with the victim are always
excluded. Consequently the Phase 4B.2 production adapter is ready to construct
and diagnose canonical live candidates, but it cannot yet produce an eligible
reporter from current relationship state.

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

## Native placed-item Theft authority audit

The current native `ActionTake` order is local `itemTaken`, local inventory
insertion, `WorldObjectSync::onLocalObjectTaken`, and local world deletion.
`onLocalObjectTaken` returns immediately when `getMpNumForObject` is zero.
Ordinary content-placed world objects do not have a server multiplayer number,
so their successful pickup emits no mandatory object-take event at all.

For a multiplayer-placed object, deletion emits `PacketObjectDelete` only when
the object has a nonzero multiplayer number. `MPServer::handleObjectDelete` can
then resolve and remove an entry from `mWorld.placedObjects` and retain its
`mpNum`, `refId`, count, position, and cell for inventory identity transfer.
That record does not contain ESM ownership, faction ownership, or a canonical
content refnum. The handler checks that the placed object exists but does not
currently validate activation distance before removal.

The later `PlayerInventory` Set snapshot is revision checked and validates
generated record references and instance identities, but it does not prove that
an added item came from the deleted world object. A modified client can omit the
object deletion and still propose the inventory addition. It can also take an
ordinary content-placed object without any object event because that object has
no multiplayer number. Therefore no current server event is both unavoidable
and sufficient to establish placed-world-item Theft, and no native Theft
producer is connected in Phase 4B.2.

The audited native value rule remains local-only today: `itemTaken` uses the
requested count directly for gold, otherwise `count * item value`; after a crime
is seen, `reportCrime` computes `max(1, int(value * fCrimeStealing))`. A future
producer must derive the item, count, ownership/faction, and value from the same
server-accepted transfer rather than trusting those conclusions from the
client.

Before authoritative Theft can be wired, the mandatory pickup operation needs
a canonical server-known identity for ordinary and multiplayer-placed objects,
validated object existence/cell/position/ownership/value/count, authenticated
sender and fresh player cell/position, interaction-range validation, and an
atomic or idempotently linked world-removal/inventory-add acceptance. The crime
event ID must be derived from that accepted operation. Adding an optional
`CrimeIntent` packet would not satisfy this boundary.

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
- authoritative placed-world-item transfer acceptance required by Theft;
- fresh generation-bound follower/combat relationship state;
- effective live Alarm authority for runtime-modified NPC AI settings;
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
