# Semantic gameplay services

## Observation and witness foundation (protocol 10)

`ObservationService` is a read-only semantic service. Protocol-10 mechanics
snapshots provide one coherent transient input revision, but the server does not
network or replay the script, dialogue result, or Lua callback that produced any
of those mechanics values. The server validates canonical identity, actor kind,
cell, migration/authority generation, sender entitlement, sequence, and receipt
freshness before an accepted snapshot can participate in observation.

The actor-authority snapshot also carries the effective Alarm value, recursive
player-follower membership, and canonical combat-target identity used by live
crime-witness filtering. These values are validated and consumed atomically;
missing or stale relationship authority fails closed rather than being inferred
from incomplete server-side AI-package state. `/crimewitness` is a disabled-by-
default diagnostic view of those decisions and never creates a crime mutation.

Observation and witness results feed typed crime intents, never scripting-command
replay. Server Lua packages remain a separate executable-code distribution system
and are not runtime records or observation state.

Multiplayer gameplay authority synchronizes typed semantic state, not the
scripting-language operation that caused it. Implemented domains include
global player crime state, player faction state, and learned dialogue topics:

```text
trusted gameplay mutation
        -> CrimeService validation
        -> atomic SQLite commit
        -> revisioned PlayerCrimeState
        -> PacketPlayerBounty
        -> normal OpenMW player state
```

This is distinct from runtime content definitions (`RecordDynamic`/OMDR) and
executable client policy (server Lua packages). Neither transport stores or
replays authoritative gameplay state.

## Reusable foundation

Each mutable domain has a strongly typed state DTO plus typed mutation and
terminal-result contracts. Their canonical little-endian encodings are
independent of C++ object layout and are used for deterministic request hashing
and durable result replay.

`RevisionedStateGate<State>` supplies client ordering:

- a newer revision is accepted and staged for engine application;
- an older revision cannot overwrite current state;
- an identical replay at the current revision is idempotent;
- different state at the current revision is a conflict.

`AuthoritativeStateBootstrapGate<CharacterData>` is the final world-entry
barrier for semantic player state. It follows, rather than joins, the separate
runtime-content and server-Lua-package bootstrap gates.

The generic `semantic_requests` table is a service-namespaced terminal request
journal. It provides stable request identity without defining an arbitrary
field mutation or string-command protocol. Future services may reuse the
transaction/journal pattern while retaining typed domain requests and state.

## Authoritative crime state

The server owns:

```text
PlayerCrimeState
    schemaVersion : uint16
    bounty        : int32, nonnegative
    currentCrimeId: int32, -1 or greater
    paidCrimeId   : int32, -1 through currentCrimeId
    revision      : uint64, bounded to signed SQLite range
```

Vanilla-compatible defaults are bounty `0`, both crime IDs `-1`, and revision
`0`. Setting bounty to zero records the current crime generation as paid; it
does not reset either crime ID to zero. Witness/NPC crime IDs are actor runtime
state and are not required merely to restore player global crime state.

Every accepted set or modify operation increments the durable revision exactly
once. Invalid, stale, unauthorized, or conflicting operations do not increment
it. Negative and overflowing bounty results are rejected rather than clamped.

## SQLite and atomicity

`character_crime_state` is keyed by `characters.id` and cascades on character
deletion. It stores all three crime fields, revision, and update time.
Characters without a row load revision-zero defaults, safely migrating existing
databases.

`semantic_requests` is keyed by:

```text
(service, account_id, character_id, request_id)
```

It stores the canonical request hash, terminal status/error, encoded result,
source, and timestamps. Account and character come from the authenticated
selected-character session; a client cannot select another target.

An accepted mutation uses `BEGIN IMMEDIATE`, rechecks current revision, writes
every crime field, inserts the terminal result, and commits. A failure rolls
back both writes. A retry with the same request ID and hash replays the result
without another mutation. The same request ID with another hash conflicts.

## Wire and bootstrap

Multiplayer protocol version 5 assigns the reserved `PlayerBounty` message ID a
typed server-to-client snapshot:

```text
player guid
schema version
revision
bounty
current crime ID
paid crime ID
```

It is a pure authoritative snapshot and has no request ID. Mutation request IDs
exist only on the trusted service side. Truncation, trailing bytes, unsupported
schemas, invalid values, and identity mismatches are rejected.

At character selection the server loads durable crime state and sends
`PacketPlayerBounty` immediately before `CharacterData` on the same ordered
reliable bootstrap lane. The client retains ready `CharacterData` until the
crime snapshot is present. Reconnect resets both revision and bootstrap gates.

## OpenMW compatibility mirror

The client installs bounty in normal `MWMechanics::NpcStats`, and installs
`currentCrimeId` and `paidCrimeId` in normal `MWWorld::Player` state. There is
no separate multiplayer bounty consulted by gameplay. Existing
`GetPCCrimeLevel`, the OpenMW Lua crime getter, dialogue crime filters and
derived globals, and GUI bounty display therefore read the same engine state.
Single-player paths are unchanged.

## Trusted mutation seam

Dedicated-server Lua receives `player.crimeState` and exposes only:

```text
player:setBounty(value, requestId)
player:modifyBounty(delta, requestId)
```

Both enqueue to the server main thread and call `CrimeService`; they never
write SQLite or the player mirror directly. A request ID must remain stable for
one logical operation. Distributed client Lua packages cannot directly mutate
authoritative SQLite state.

## Phase 3 limits

This phase does not intercept every local source of crime. Theft, assault,
murder, trespass, pickpocket, witness simulation, and scripted/OpenMW-Lua
bounty proposals remain Phase 4 work. Guard pursuit policy, arrest dialogue,
Pay/Jail/Resist, confiscation, jail teleport, and jurisdictional bounty also
remain deferred.

Phase 4 should introduce typed semantic crime proposals/reporting that feed
this same service, then build arrest as a separate authoritative transaction.
It must not use client final-state snapshots or remote MWScript/Lua execution.

## Authoritative known dialogue topics

`PlayerTopicState` is a separate revisioned, add-only gameplay domain:

```text
local MWScript/OpenMW Lua/dialogue result
        -> normal DialogueManager topic set
        -> AddKnownTopic proposal
        -> server effective-content validation
        -> atomic SQLite commit
        -> authoritative full topic set
        -> normal DialogueManager topic set
```

The server stores the revision in `character_topic_state` and the canonical,
case-insensitive IDs in `character_known_topics`. Both tables are keyed by the
authenticated selected character and cascade on character deletion. Duplicate
adds are idempotent and do not advance the revision. Stale proposals receive
the current authoritative state without mutation.

Protocol version 6 assigns the reserved `PlayerTopic` message ID a typed
contract. Clients may send only an `Add` proposal with their expected revision;
they cannot replace or remove the complete set. The server replies only with a
`Set` snapshot containing its full canonical state. Packet validation bounds
the set and ID sizes, requires valid UTF-8, and rejects noncanonical ordering,
duplicates, unsupported schemas, trailing bytes, and identity mismatches.

The current client proposal is deliberately transitional: the server proves
that every proposed ID is a Topic DIAL in its effective static or runtime
content, but it does not yet prove which dialogue response or script entitled
that player to learn it. Stronger source-specific entitlement can later feed
the same add-only service without changing the persisted result model.

At character selection the topic snapshot is an explicit world-entry
prerequisite alongside crime state. Runtime definitions remain a separate
bootstrap domain and are installed first. If a restored topic references a
dynamically supplied DIAL that is not yet visible, the snapshot stays queued
and is retried after record insertion; it is never silently discarded.

The client mirrors an accepted snapshot through a narrow replacement seam for
`DialogueManager::mKnownTopics`. It does not call `DialogueManager::clear()`,
does not maintain a competing multiplayer topic cache, and does not replay the
MWScript, Lua, INFO result script, or dialogue interaction that caused the
topic to be learned. Normal dialogue UI, MWScript, and OpenMW Lua therefore
observe the restored semantic result naturally. Single-player save/load paths
remain unchanged.

## Authoritative player faction state

`PlayerFactionState` is an independently revisioned character domain. Each
canonical faction entry stores the complete vanilla-compatible tuple:

```text
factionId  : canonical case-insensitive record ID
rank       : -1 when not a member, otherwise a validated FACT rank
reputation : signed int32
expelled   : boolean
```

Reputation and expulsion are retained even when rank is `-1`, matching
`ESM::NpcStats`. A tuple with rank `-1`, reputation `0`, and no expulsion is
omitted from the canonical state. Entries are sorted and unique, IDs and packet
counts are bounded, and invalid UTF-8, unsupported schemas, invalid ranks,
trailing bytes, and same-revision conflicts fail validation.

Clients and trusted server Lua can submit only typed transitions:

```text
JoinFaction              LeaveFaction
SetFactionRank           ModifyFactionRank
SetFactionReputation     ModifyFactionReputation
ExpelFromFaction         ClearFactionExpulsion
```

There is no client operation that replaces the authoritative tuple directly.
The ordinary MWScript and OpenMW Lua APIs remain synchronous: they first mutate
normal local `NpcStats` as they do in single player. The multiplayer sync layer
observes the resulting tuple, derives the minimum ordered typed transitions,
and proposes those transitions with the expected faction revision. The server
validates every faction and rank against its effective `FACT` store, applies
the operations to the previous authoritative state, and returns a full result
snapshot. A rejection or stale response corrects optimistic local state. Local
changes made while a proposal is in flight are retained and proposed after the
authoritative result arrives.

The server persists the revision in `character_faction_state` and entries in
`character_factions`. Both are keyed by the authenticated selected character
and cascade on deletion. An accepted operation uses `BEGIN IMMEDIATE`, rechecks
the revision, replaces the tuple rows, writes the new revision, and inserts the
terminal result into the service-namespaced `semantic_requests` journal in one
transaction. Duplicate request IDs with the same canonical hash replay their
stored result; the same ID with different content conflicts. Restart and
reconnect load the exact tuple and revision.

Multiplayer protocol version 7 assigns the reserved `PlayerFaction` message ID
a typed proposal/result contract. Initial character selection sends an empty-
request authoritative snapshot after runtime definitions and before the topic
snapshot and `CharacterData`. World entry requires crime, faction, and topic
snapshots as well as the separate runtime-content and server-Lua-package gates.
Faction application checks that referenced definitions are visible in the
effective ESM store and is retried after dynamic-record insertion.

The client replaces only the faction rank, reputation, and expulsion maps in
normal `MWMechanics::NpcStats`; bounty, skills, disposition, and other NPC state
are untouched. Dialogue faction/rank/reputation/expulsion filters, MWScript,
OpenMW Lua, and UI code already read those same maps, so they observe restored
state without multiplayer-specific branches. The script or dialogue result
that caused a transition is never replayed on another client. Single-player
mutation and save/load behavior remains unchanged.

Trusted dedicated-server Lua receives `player.factionState` and may enqueue the
same eight typed operations through `joinFaction`, `leaveFaction`, rank and
reputation set/modify methods, and expel/clear methods. Every call requires a
stable request ID and enters `FactionService` on the server main thread. This is
not executable-code distribution and does not give distributed client packages
direct SQLite access.
