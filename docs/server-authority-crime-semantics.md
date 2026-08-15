# Server-authoritative crime semantics

## Status and boundary

Phase 4B.1 provides a server-only, independently testable semantic crime engine.
It accepts an already-authenticated gameplay cause and evaluates Theft,
Pickpocket, Trespass, Assault, and Murder. It does not yet connect native,
MWScript, or OpenMW Lua producers to that engine and adds no packet or protocol
change.

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
`reported`, or witness lists. Later producers must build witness candidates from
the server actor/player registries.

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

## Witness eligibility and observation

Only canonical `ObservationActorKind::Npc` identities can be vanilla crime
witnesses. A remote human player remains kind `Player` even if a client runtime
represents that human through an NPC-shaped proxy, and is therefore excluded.
Creatures are also excluded, matching native `canReportCrime` behavior.

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
- live witness candidate construction and relationship derivation;
- dialogue complaints, disposition, Fight changes, faction expulsion, pursuit,
  and other actor reactions;
- player-versus-player witnessing;
- arrest, guard dialogue/UI, jail, fines, fine payment, and multi-domain fine
  transactions;
- werewolf exposure and respawn bounty repair.

Server Lua package distribution remains a separate executable-code system and is
not part of RecordDynamic or this crime-state service.
