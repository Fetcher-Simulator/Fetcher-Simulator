# Server collision and LOS benchmark

Date: 2026-08-14

## Decision

Use **server-authoritative collision/LOS**. The measured query-only collision
world is cheap enough in memory, load latency, and event-driven ray cost that
delegating LOS to an actor-authority client is not justified.

Use a **hybrid ObservationService** initially for perception as a whole:

- `ServerCollision` is authoritative for LOS.
- Server-owned actor state is authoritative for identity, cell, position,
  radius, life state, and any other inputs already validated by ActorSync.
- `DelegatedActorAuthority` may temporarily supply only awareness inputs the
  server cannot yet derive. Such results remain explicitly delegated, not
  fully authoritative.

This decision does not authorize CrimeIntent, protocol 9, arrest, or broad
native crime implementation. LOS is only one input to awareness and crime
reporting.

## Build and isolation

- Branch: `experimental/public-test`
- Prototype commit: `809df0dcd45e5edf2f3517036c4ca5a598bc3b0d`
- Exterior-worldspace fix: `8389306a1749d1847eebe8acc678e0238dc28b54`
- Both commits have good GPG signatures from key
  `4857CC4439AA9DB9B4689B2B0BC6A51BF4914D93`.
- VPS: Linux 6.8.0-1049-oracle, AArch64, 4 x ARM Neoverse-N1, 23 GiB RAM,
  no swap.
- Benchmark binary SHA-256:
  `59e98348cda7ee11d45fca8bc0a3f489e9b6d95f58613f4c5ab982bee612ca7d`
- Content: 82 ordered files and 175 Lua scripts; resolved fingerprint
  `82d9b2f8b5c5324fa9a3bb80085c792e90a9cfa96e400736f52adc556fa0e4ff`.
- Isolated root: `/root/openmw-collision-benchmark`
- Isolated UDP port: 25569; public listing disabled.
- Isolated SQLite path: `runtime/playerdata.db`.
- Content assets were reused read-only by absolute path. No production
  database, Lua storage, runtime state, or distribution file was modified.
- Live UDP 25564 remained `openmw-server` PID 82701 throughout the benchmark.

The legacy TES3MP process on 25569 was verified before termination. Its exact
`autorestart.bash` parent was stopped to prevent it reclaiming the test port.

## Prototype scope

The prototype reuses:

- `ServerContentRegistry` for ordered content, VFS, `ResourceSystem`,
  `ESMStore`, and `WorldModel` initialization;
- `Resource::BulletShapeManager` and the existing NIF collision loaders;
- `BulletHelpers` for collision-object and heightfield transforms;
- OpenMW collision groups and the same LOS mask: world, heightmap, and doors.

It owns only a `btCollisionWorld`, dispatcher, broadphase, static collision
objects, and terrain heightfields. It does not create a dynamics world, actors,
rendering, animation, navigation, sound, character controllers, rigid bodies,
or a simulation loop.

For ESM3 benchmark cells it loads activators, containers, doors, statics,
non-carry lights, and terrain. NPCs and creatures are collected only as
realistic eye-position ray samples; they are not collision objects. Visual-only
and camera-only shapes are excluded by collision masks.

The prototype is an opt-in `openmw-server --collision-benchmark` executable
mode that exits before socket, database, and gameplay-service initialization.
It is instrumentation, not the final ObservationService.

The Phase 4A.4 benchmark additionally resolves the real
`Ex_De_SN_Gate`/`321262` door instance and searches bounded rays around its
placed transform. It requires one representative ray to transition
`blocked -> visible -> blocked` across close/open/close while collision
generation advances on both transforms. On the 82-file Windows content set the
accepted ray was:

```text
from -9712.44,-72253.7,229.923
to   -9457.42,-72231.4,229.923
generation 1 -> 2 -> 3
```

The earlier generic actor-pair ray remains blocked by unrelated geometry and is
retained as a separate regression; it is no longer mistaken for the doorway
visibility assertion.

## Baseline

The isolated normal server reached UDP-ready in 14.553 seconds. Three five-second
idle samples were stable at 484,424 KiB RSS and 0.20% of one CPU core. The active
production server was 544,708 KiB RSS, but it is not a controlled baseline
because it included live runtime/player state.

The query-only content baseline was 480,400-480,532 KiB RSS. Each process peaked
near 704 MiB while loading the 82-file content set; that transient peak was the
same with every collision scenario and is therefore content initialization,
not collision scaling.

## Collision memory and load cost

Each memory row below came from a fresh process. `Collision delta` is loaded RSS
minus that process's query-only content baseline. `Normal-server delta` compares
loaded RSS with the isolated normal-server steady baseline of 484,424 KiB.

| Scenario | Cells | Objects | Triangles | Loaded RSS KiB | Collision delta KiB | Normal-server delta KiB | Load ms | Unload ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| One interior | 1 | 184 | 14,286 | 483,308 | 2,812 | -1,116 | 7.80 | 0.60 |
| Open exterior | 1 | 294 | 23,341 | 482,352 | 1,856 | -2,072 | 12.35 | 0.84 |
| Balmora dense cell | 1 | 414 | 30,124 | 483,300 | 2,812 | -1,124 | 14.00 | 1.08 |
| Balmora 3x3 | 9 | 1,481 | 183,873 | 485,932 | 5,532 | 1,508 | 40.41 | 3.34 |
| Four disjoint 3x3 areas | 36 | 5,454 | 796,727 | 486,020 | 5,568 | 1,596 | 116.71 | 12.03 |

The first cold interior load observed during the initial failed run was 74.12
ms. In the combined warm run, the 36-cell load was 183.62 ms. Treat the table as
warm-cache measurements and these larger observations as practical cold/order
variance.

Five repeated Balmora dense-cell transitions loaded in 7.82-8.12 ms and
unloaded in 0.54-0.66 ms.

RSS does not immediately fall after unload because allocator and resource
caches retain pages. Cell ownership therefore needs explicit refcounts and
budgets even though the measured steady footprint is small.

## Ray throughput

Measurements are single-threaded. CPU time tracked wall time closely, so a
synthetic saturated batch consumes approximately one core while it runs.

| Scenario | 1,000 rays | 10,000 rays | 100,000 rays | 100k throughput |
| --- | ---: | ---: | ---: | ---: |
| Interior | 3.08 ms | 29.04 ms | 281.70 ms | 354,984 rays/s |
| Open exterior | 3.40 ms | 31.45 ms | 310.84 ms | 321,712 rays/s |
| Balmora dense | 1.70 ms | 16.66 ms | 166.21 ms | 601,654 rays/s |
| Balmora 3x3 | 3.05 ms | 29.11 ms | 287.37 ms | 347,987 rays/s |
| Four disjoint areas | 4.82 ms | 43.62 ms | 430.77 ms | 232,142 rays/s |

Repeated uncached single-pair throughput ranged from 301,592 to 878,881
rays/s. A simple actor-pair result cache handled 155-320 million lookups/s;
that figure measures cache lookup overhead, not Bullet raycasts. A production
cache must include actor/cell collision generations and freshness, so it cannot
be an unqualified permanent pair cache.

## Event-driven witness scaling

The Balmora 3x3 test ran 1,000 synthetic crime events after candidate selection:

| Candidate witnesses/event | Total rays | Total time | LOS cost/event |
| ---: | ---: | ---: | ---: |
| 5 | 5,000 | 15.62 ms | 0.0156 ms |
| 20 | 20,000 | 56.74 ms | 0.0567 ms |
| 50 | 50,000 | 139.88 ms | 0.1399 ms |
| 100 | 100,000 | 280.86 ms | 0.2809 ms |

Even 10 events/s with 100 LOS candidates would consume about 2.81 ms of one
core per second (approximately 0.28% of one core). Normal processing should be
lower because the server first limits actors by cell, `fAlarmRadius`, identity,
life state, and other cheap eligibility rules. Spatial enumeration and
awareness computation were not measured here.

## ObservationService boundary

The next implementation should introduce an interface independent of
CrimeService and of its backend:

```text
ObservationQuery
  observer canonical actor identity and kind
  target or event identity
  worldspace/cell and eye positions
  event timestamp/request ID
  actor-authority lease + generation
  actor snapshot generation
  collision-cell generation
  requested components (LOS, awareness, special hearing/victim rules)

ObservationResult
  LOS outcome
  awareness outcome, if evaluated
  observable outcome and reason
  backend/authority classification
  actor and collision generations used
  freshness/expiry metadata
```

`CrimeService` should consume a validated semantic observation result. It must
not consume a raw client-authored `crimeSeen` boolean.

The server actor registry should provide cell-bucketed candidates, including
adjacent exterior cells only where the radius crosses a boundary, before the
distance and LOS filters. Canonical identity must distinguish ordinary remote
NPC/creature actors from remote human players. Remote NPCs remain eligible;
remote human NPC proxies are excluded from vanilla NPC witness rules unless a
separate player-witness mechanic is designed later.

Collision cells should be reference-counted by relevant players/events and
carry a monotonically increasing generation. Dynamic doors and future runtime
world blockers must invalidate that generation. Runtime collision definitions
remain separate from persistent player/world gameplay state.

## Rendered-client acceptance

Phase 4A.4 subsequently exercised the collision backend through the live
protocol-9 observation path rather than the benchmark harness alone. The final
rendered test used the real hinged door and vanilla NPCs in `Balmora, Guild of
Fighters`.

The first authority-client pass selected `flaenia amiulusus` (actorNetId 67613)
and held the player on the opposite side of the same real door. Close/open/close
advanced the authoritative collision generation `2 -> 3 -> 4` and produced:

```text
closed  -> los=false, reason=blocked_los
open    -> los=true,  awareness=true, observable=1, reason=observed
closed  -> los=false, reason=blocked_los
```

A second simultaneous rendered client then joined the same cell while the first
client retained actor authority. The second client was not actor authority and
only issued observation queries. Its close/open/close sequence advanced the same
server-owned collision timeline `4 -> 5 -> 6` and again produced
`los=false -> true -> false`. The NPC mechanics snapshot sequence continued to
advance (`5093 -> 5176 -> 5256`) with non-zero generation metadata, proving that
the non-authority result consumed server collision state plus the authoritative
NPC snapshot stream rather than a local-client LOS decision.

The live test also exposed and fixed a protocol-9 generation bootstrap defect:
ordinary placed actors could retain migration generation zero, and the authority
client could overwrite its current cell authority generation with zero while
constructing outbound authority state. Signed commit
`5b6e6fab15fc42f10d6eb5d4ec5407dec8042c28` seeds the initial canonical actor
lifetime at generation 1 and preserves the server-issued authority generation.
Both one-client and authority/non-authority rendered acceptance passed after the
fix.

## Remaining risks and non-results

- The benchmark uses placed ESM3 geometry and base door transforms. A final
  backend must track authoritative door open/closed transforms and other
  relevant dynamic blockers.
- ESM4 worldspace collision was not benchmarked.
- One referenced test NIF was absent and skipped with a warning; counts and
  results include all successfully loaded blockers.
- Static placed actors supplied realistic ray endpoints, but live ActorSync
  positions, facing, and movement churn were not benchmarked.
- LOS does not establish awareness. Facing, sneak, attributes, fatigue,
  equipment weight, invisibility/chameleon/blindness, cached rolls, PRNG,
  combat/follower relationships, victim-awareness rules, and murder hearing
  still need an authority classification.
- `crimeSeen`, reporting, Alarm >= 100, bounty mutation, crime IDs, disposition,
  pursuit, and arrest remain separate semantic stages.

## Verification

- Local `openmw` and `openmw-server` RelWithDebInfo builds: pass (`--parallel 24`).
- Post-fix Windows `openmw-tests`: 683/683 pass.
- Post-fix Windows `components-tests`: 1,604/1,605 pass; only the established
  `LuaL10nTest.L10n` failure occurred.
- ARM64 dedicated-server build after generation-bootstrap fix: pass.
- ARM64 isolated start/bind/stop smoke: pass.
- Isolated normal-server baseline and all five collision benchmark scenarios: pass.
- Synthetic real-door benchmark: `blocked -> visible -> blocked`, generation
  `1 -> 2 -> 3`: pass.
- Rendered authority-client Fighters Guild doorway test: `blocked -> observed -> blocked`,
  collision generation `2 -> 3 -> 4`: pass.
- Rendered non-authority second-client doorway test while the first client retained
  actor authority: `blocked -> observed -> blocked`, collision generation
  `4 -> 5 -> 6`: pass.
- After acceptance, isolated UDP 25569 was stopped and verified free; production
  UDP 25564 remained running.
