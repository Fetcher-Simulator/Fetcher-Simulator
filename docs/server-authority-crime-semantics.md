# Server-authoritative crime semantics

## Status and invariant

Phase 4 routes every classified multiplayer crime-state producer through a
server-owned semantic boundary or fails it closed until its larger consumer
system exists. Single-player retains upstream synchronous behavior.

```text
real gameplay cause
    -> mandatory authenticated/server-observable transaction
    -> objective validation
    -> canonical CrimeIntent
    -> live canonical witnesses
    -> ObservationService
    -> perception
    -> reporting
    -> CrimeService
    -> durable PlayerCrimeState
    -> authoritative replication
```

Gameplay cause, perception, reporting, and bounty mutation are separate facts.
Runtime content definitions, executable server Lua packages, and authoritative
player/world state are also separate systems. No client script command is
replicated or replayed to establish crime state.

## Producer and authority matrix

| Producer | Gameplay cause | Mandatory authority input | Semantic destination | Multiplayer status |
|---|---|---|---|---|
| placed item pickup | transfer a placed item | authenticated `WorldItemTakeRequest`, canonical reference, server content/world state, range, inventory revision | Theft when server ownership requires it | authoritative |
| container UI take | transfer from static container | authenticated `InventoryTakeRequest`, server-resolved container, authoritative source snapshot, item/count, range, inventory revision | Theft when server ownership requires it | authoritative |
| harvest | transfer harvest-container contents | one or more serialized container-take requests | Theft when server ownership requires it | authoritative; no local pre-mutation |
| corpse inventory | transfer from dead vanilla actor | generation-bound actor identity, authoritative corpse inventory | none | authoritative transfer |
| living actor inventory | take outside pickpocket | generation-bound actor identity, fresh relationship state, authoritative inventory | Theft unless follower relationship allows use | authoritative for vanilla actors |
| pickpocket item/finish | steal attempt or close detected | generation-bound victim, fresh mechanics snapshots, server GMSTs and PRNG, authoritative inventory | Pickpocket, victim-aware, only when detected | authoritative |
| lockpick/probe/Open spell | attempt to unlock an owned locked/trapped door or container | authenticated `CrimeInteractionRequest`, canonical content reference, server content ownership/lock/trap, player range | Trespass | authoritative |
| sleep in owned bed | attempt to use owned bed | requires a future async activation/result continuation | Sleeping-in-owned-bed/Trespass policy | fails closed in multiplayer; native in single-player |
| accepted hit | health damage accepted by current victim actor authority | pending authenticated attack proposal plus generation/lease/sequence-bound delegated result | Assault when native exclusions do not apply | authoritative |
| attributable death | death flag on accepted causal combat event | durable accepted combat event and victim lifetime | Murder, with hearing | authoritative |
| MWScript `SetPCCrimeLevel` | direct bounty set | authenticated ordered `CrimeMutationRequest` | `CrimeService` set | authoritative |
| MWScript `ModPCCrimeLevel` | direct bounty delta | authenticated ordered `CrimeMutationRequest` | `CrimeService` modify | authoritative |
| OpenMW Lua `player.setCrimeLevel` | direct bounty set | authenticated ordered `CrimeMutationRequest` | `CrimeService` set | authoritative |
| OpenMW Lua `player.commitCrime` | client script claims a cause | no trustworthy gameplay proof | none | fails closed in multiplayer; native in single-player |
| werewolf transformation exposure | observed transition into werewolf form | accepted player mechanics snapshot plus durable transformation edge | `WerewolfExposure` | authoritative |
| respawn/death reset | local actor reset | authoritative crime snapshot | none | local bounty reset suppressed; state retained |
| jail / `PayFine` / `PayFineThief` | arrest/fine transaction | future atomic fine/arrest/confiscation transaction | future paid-crime transition | fails closed in multiplayer |
| direct client `NpcStats`/legacy bounty fields | untrusted state write | none | none | cannot establish durable state; authoritative snapshot overwrites it |

Player-placed containers are unowned and remain on the legacy container-sync
path. Spawned-actor inventories fail closed because the legacy container key is
not actor-lifetime unique. These are not alternate crime-state paths. A future
container persistence migration should add lifetime-unique keys before enabling
those transfers.

## Cause, perception, reporting, and state

`CrimeIntent` carries a stable event identity, server source, typed crime,
canonical cell, offender observation snapshot, optional victim, victim-aware
policy, value, observation time/freshness, and collision generations. The
authenticated server context owns account, character, and player identity.

The wire does not accept client-authored `crimeSeen`, reporters, witness lists,
ownership, item value, bounty, or crime IDs. `CrimeWitnessBuilder` materializes
candidates from the live actor registry. `CrimeSemanticService` evaluates
eligibility and perception, then preserves the two native passes:

1. any eligible witness perception starts reporting semantics;
2. the report pass considers every eligible candidate, and Alarm 100 applies
   bounty.

The perceiver and bounty reporter can therefore be different NPCs. Reporting
can advance `currentCrimeId` without changing bounty. Accepted unseen causes are
also journaled, preventing a replay from rerolling awareness.

## Live witnesses, Alarm, and relationships

`MPServer::buildLiveCrimeWitnesses` enumerates `mWorld.actorCells` only in the
event cell and relevant neighboring exterior cells intersecting `fAlarmRadius`.
Candidates are sorted and deduplicated. A canonical victim can be included
outside ordinary radius.

Every candidate requires a fresh atomic mechanics snapshot matching canonical
actor kind/identity, cell, migration generation, current authority generation,
monotonic sequence, and current actor-authority sender. Remote human players and
creatures are not vanilla NPC witnesses.

Alarm comes from the fresh actor-authority snapshot with validated delegated
provenance. Static NPC-record Alarm is explicitly classified fallback data.
Values outside 0..100 fail validation.

The same snapshot carries recursively computed player-follower membership and a
canonical combat target. A follower, or an actor fighting the victim, is not an
eligible witness. Missing or stale relationship provenance fails closed.

Ordinary witnesses use server LOS and awareness. Victim-aware Assault and
detected Pickpocket use the explicit victim path. Murder hearing is a distinct
LOS-independent observation path; it is never represented as fake successful
LOS.

`/crimewitness [victimActorNetId]` calls this production builder and reports
identity, cell, distance, snapshot age, generations, Alarm provenance,
relationship provenance, and inclusion/rejection reason without mutation.
`/observe` remains available. Both diagnostics are disabled by default.

## Authoritative item transactions

### Placed world items

Connected pickup is request-first. Content references use
`(cellId, refId, RefNum.index, RefNum.contentFile)` and server-placed objects use
`(cellId, refId, mpNum)`. The server resolves existence, enabled state, count,
position, owner/faction/rank/global, item type/value, charge, soul, gold
conversion, player cell/range, and expected inventory revision.

One SQLite transaction inserts the content tombstone or removes the placed
object, adds authoritative inventory, maintains dynamic-record links, advances
the inventory revision, journals the terminal take request, and, for Theft,
commits the already-evaluated terminal crime result plus any `PlayerCrimeState`
transition. The gameplay cause cannot survive a rollback without its semantic
outcome. Bootstrap replays tombstones. The accepted transaction ID directly
names its Theft semantic event.

### Containers and actor inventories

`InventoryTakeRequest` distinguishes container, corpse, living actor,
pickpocket, and pickpocket-finish operations. The server bootstraps a missing
full source snapshot from current cell/actor authority, then validates source
identity/lifetime, source contents, requested stack/count, content-derived item
properties, player and victim snapshots, range, ownership/relationship, and
destination inventory revision.

Source removal, destination addition, inventory revision, dynamic-record links,
pickpocket result metadata, terminal take request, and any Theft/Pickpocket
semantic result are committed atomically. The server performs witness
observation and the pickpocket roll before opening that commit boundary, then
persists the resulting terminal facts without rerunning them. Identical
duplicates return the durable result; conflicting payloads reject.
Client revision conflicts update and retry the pending request. This serializes
multi-stack harvest without local pre-mutation.

Legacy client-authored container `Remove` cannot bypass the transaction for
static or actor sources. Non-authority `Set` receives the authoritative source
snapshot. Runtime/player-placed unowned containers remain on the legacy path as
described in the matrix.

### Pickpocket

The server reproduces the native formula using fresh thief/victim Sneak,
Agility, Luck, fatigue, server GMSTs, content-derived item value, and one server
PRNG roll. A terminal request stores the roll and detection outcome, so duplicate
or restart replay never rerolls. Detection prevents transfer, matching native
behavior. Undetected item takes transfer without crime; detected item or finish
attempts emit victim-aware Pickpocket semantics.

## Authoritative combat events

An attack request creates a pending server-issued `CombatEventId` bound to the
authenticated attacker, canonical victim actor instance, cell, migration and
authority generations, current actor-authority lease, proposed damage class,
proposal hash, and expiry.

The current victim actor authority may submit `ActorCombatResult`, but it is a
validated delegated result rather than an independent assertion. Acceptance
requires:

- a known unexpired pending proposal;
- matching attacker, victim, cell, and proposal identity;
- the current authority sender and live lease;
- matching migration and authority generations;
- a strictly valid monotonic result sequence;
- bounded finite applied damage and consistent applied/death flags;
- an independent fresh victim mechanics snapshot; and
- no attacker/actor-authority conflict.

When the attacker is also the victim's actor authority, the event fails closed
for crime attribution because one client cannot corroborate its own proposal.
Wrong sender, stale generation, unsolicited, expired, or conflicting duplicate
results reject. Identical accepted replay is idempotent.

Accepted records and attribution are durable in SQLite. Native criminality
exclusions (victim already aggressive, engaged with attacker, pursuing,
werewolf, or vampire) are captured at the accepted result boundary. A qualifying
first accepted health-damage result prepares victim-aware Assault. A validated
lethal result can prepare Murder only when it references that same event and
victim lifetime. Combat acceptance, `assault_reported`, and all prepared Assault
and Murder semantic results commit in one SQLite transaction. The later actor
death update verifies the already-durable Murder event instead of rebuilding
witnesses from post-event state. The same `CombatEventId` supplies Assault,
death cause, responsible attacker, Murder, and semantic idempotency. No
nearest-player or last-packet guessing is used.

## Trespass interactions

Native source audit found two producer families:

- `unlockAttempted`, invoked by lockpick, probe, and Open effects;
- `sleepInBed` for an owned bed.

Connected unlock attempts send a typed request and do not locally run
`commitCrime`. The canonical static identity includes cell, record ID, reference
index, and content file. The server resolves door/container type, enabled state,
lock/trap state, position, owner/faction/rank/global, player faction state,
fresh player position, and range. Only a still relevant owned attempt creates
Trespass. The semantic source contains the canonical request hash, allowing the
durable journal to distinguish identical replay from a conflicting duplicate.

Owned-bed resting needs an asynchronous authoritative result to resume an
upstream synchronous rest UI safely. Multiplayer therefore displays the native
refusal and fails closed; single-player continues through native synchronous
crime/rest behavior. No client-authored conclusion is accepted as a shortcut.

## Direct script mutations and resets

MWScript `SetPCCrimeLevel` and `ModPCCrimeLevel`, and OpenMW Lua
`player.setCrimeLevel`, enqueue authenticated, ordered, uniquely identified
`CrimeMutationRequest` values. `CrimeService` applies them durably and normal
`PlayerBounty` replication updates the client. Single-player executes upstream
local behavior.

OpenMW Lua `player.commitCrime` cannot prove its gameplay cause or witnesses and
therefore returns false in multiplayer. Authoritative native transactions emit
their own causes. As a final compatibility boundary, the native
`MechanicsManager::commitCrime` implementation itself fails closed while a
multiplayer session is initialized; supported producers must have entered a
typed authoritative path before reaching it. Single-player retains the upstream
implementation unchanged.

`PayFine`, `PayFineThief`, and jail entry are disabled in multiplayer until an
atomic arrest/fine/confiscation system exists. They cannot clear bounty or move
`paidCrimeId`. Local respawn does not clear bounty. Reconnect, respawn, and
server restart restore the exact durable bounty, `currentCrimeId`,
`paidCrimeId`, and revision.

## Werewolf exposure

Protocol-10 mechanics snapshots include werewolf state. The server compares the
accepted state with the durable per-character transformation edge. A newly
observed transition to werewolf form prepares one stable `WerewolfExposure`
event through the production witness pipeline. The transformation edge and its
terminal semantic result commit atomically, so a crash cannot persist the edge
while losing exposure. Its policy applies the server GMST bounty when reported
but does not advance `currentCrimeId`, matching native behavior. Duplicate
snapshots, reconnect, and restart do not repeat exposure.

## Persistence and idempotency

Canonical little-endian encodings are SHA-256 hashed. The shared
`semantic_requests` journal stores terminal semantic results. Gameplay
transactions additionally store their accepted request/result and expected
state revisions. Observation happens once per canonical event; replay reads the
stored result rather than re-evaluating LOS, awareness, PRNG, ownership, or
combat attribution.

`CrimeSemanticService` can prepare a terminal semantic mutation without writing
it. World-item takes, inventory/pickpocket takes, accepted combat results, and
werewolf transformation edges pass that prepared mutation into the gameplay
transaction, where one shared in-transaction crime commit updates
`PlayerCrimeState` and `semantic_requests`. SQLite therefore links cause,
perception result, authoritative state, and idempotency record at the same
commit boundary. An accepted source mutation cannot commit without its
destination/result, and a criminal cause cannot commit while its terminal crime
result is missing.

Failure-injection coverage aborts each linked transaction after the crime-state
write and verifies rollback of the cause as well: world tombstone/inventory,
container source/destination, combat acceptance and earlier semantic writes, and
the werewolf transformation edge all remain unchanged. Deferred semantic tests
also verify that observation is evaluated once before the outer commit and is
not persisted or rerolled independently.

## Protocol 10

Protocol 10 remains unreleased and is not bumped during this closeout. It now
includes:

- mechanics snapshot wire v3 with Alarm, relationship, combat target, and
  werewolf fields;
- `WorldItemTakeRequest`/`Result` (147/148);
- `ActorCombatResult` (149);
- `InventoryTakeRequest`/`Result` (155/156); and
- `CrimeInteractionRequest` (157).

All new decoders enforce versions, bounds, canonical identity rules, exact
payload consumption, and no trailing bytes.

## Diagnostics

Bounded server logs identify accepted causes and semantic results, including
event ID, offender, victim/object, crime type/value, seen/reporting facts,
bounty delta, crime-ID transition, and replay status. Combat logging identifies
proposal/result event, authority and generations, and death attribution.
Diagnostics do not make a client claim authoritative.

## Automated closeout verification

The Phase 4 closeout build succeeds for `components-tests`, `openmw-tests`,
`openmw-server`, and `openmw`. Focused authority/protocol coverage passes 30/30
component tests and 29/29 OpenMW tests. Full Windows regression passes 722/722
`openmw-tests` and 1620/1621 `components-tests`; the only component failure is
the established unrelated `LuaL10nTest.L10n` failure.

The persistence suite includes explicit rollback injection at the semantic
boundary, and the producer audit leaves no supported connected multiplayer path
that may use local `commitCrime` to establish durable bounty.

## Rendered two-client acceptance

Use the isolated Phase 4 environment. Do not use production. Run Client A as
the NPC actor authority and Client B as the non-authority offender.

1. In a populated interior, run `/crimewitness`. Confirm real NPCs, fresh
   mechanics, current generations, Alarm and relationship provenance, eligible
   witnesses, and exclusion of Client A's remote-human proxy.
2. Client B takes an owned placed item in view. Confirm accepted world take,
   server ownership/value, Theft, witness observation/reporting, and offender-
   only bounty/current-crime replication.
3. Repeat with geometry blocking ordinary LOS. Confirm no ordinary perception.
4. Take an owned item from a static container, then harvest an owned harvestable
   container. Confirm authoritative source removal/destination addition and the
   same Theft pipeline. Reconnect and confirm exact contents.
5. Client B lands one qualifying hit on an innocent NPC. Confirm proposal,
   delegated accepted result, stable event ID, victim-aware Assault, and no
   Assault from rejected/proposal-only traffic.
6. Kill that NPC. Confirm the death references its accepted combat event,
   responsible attacker is Client B, Murder runs once, and nearby eligible NPCs
   use murder hearing.
7. Make pickpocket attempts until both outcomes are seen. Confirm the server log
   records its roll: undetected transfers without crime; detected does not
   transfer and emits victim-aware Pickpocket.
8. Attempt a lockpick/probe/Open effect on an owned locked or trapped static
   door/container. Confirm typed server-resolved Trespass. Try an owned bed and
   confirm multiplayer refuses rest without local bounty mutation.
9. Exercise `SetPCCrimeLevel`, `ModPCCrimeLevel`, and OpenMW Lua
   `player.setCrimeLevel` in an isolated fixture. Confirm one authoritative
   state transition each. Confirm Lua `commitCrime` cannot invent a cause.
10. After acquiring bounty, respawn, reconnect, and restart the isolated server.
    Confirm exact bounty, current/paid crime IDs, revision, item tombstones,
    inventories, pickpocket result, combat attribution, and werewolf edge.

## Deferred systems

Arrest, surrender UI, guard dialogue and pursuit, fine payment, jail,
confiscation/evidence chests, sentence/skill loss, NPC Fight/disposition/faction
reactions, PvP witnessing, party mechanics, and vehicles are separate systems.
Their current multiplayer entry points fail closed where they could mutate crime
state.

Server Lua package distribution remains a separate executable-code system. It
must not be placed in RecordDynamic or treated as gameplay-state replication.
