# Semantic gameplay services

Multiplayer gameplay authority synchronizes typed semantic state, not the
scripting-language operation that caused it. The first implemented service is
global player crime state:

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
replays authoritative crime state.

## Reusable foundation

`PlayerCrimeState` is a strongly typed domain DTO. `CrimeMutationRequest` and
`CrimeMutationResult` provide typed mutation and terminal-result contracts.
Their canonical little-endian encodings are independent of C++ object layout
and are used for deterministic request hashing and durable result replay.

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
