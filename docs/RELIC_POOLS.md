# Shared relic pools

## Configuration

`NativeHunts.Relics.CountPerZone = 60` controls the number of relics in each
new active hunting-zone pool. Zero disables the subsystem without affecting
ordinary kill tracking or deleting historical `native_hunt_collection` rows.
Negative values are treated as zero. Values above 200 are clamped to 200 and
reported in the server log. There is deliberately no separate enable switch.

Changing the positive value does not rewrite an active pool. Its persisted
count and positions remain stable until its last associated Hunt ends. New
pools use the new value. Changing the value to zero immediately despawns and
retires managed pools and clears live assignment associations.

## Pool lifecycle and sharing

A pool key contains map ID, map instance ID, and zone ID. The first Tracking
assignment needing a key creates one pool. Later assignments in that exact
world scope reuse it. The assignment stores the pool UUID; the pool row and its
indexed positions live in the character database.

Relics are runtime summons of GameObject entry 990000. They are never consumed
on click. Collection credit remains keyed by hunt-instance ID, objective key,
and `pool UUID:index`, so two players can collect the same physical relic while
one player cannot receive duplicate credit during one Hunt. Credit uses the
existing tracking transaction and milestone reconciler; reaching 100% cannot
bypass a pending or outstanding ambush.

Completion and abandonment release the assignment. A pool stays alive while
any active or resumable assignment references it, including an offline player.
After the final reference disappears, every summoned object is deleted and the
pool and position rows are removed. Historical per-Hunt collection rows remain.

## Restart recovery

On startup Native Hunts loads all resumable assignments, then loads only pools
referenced by them. Persisted coordinates are reused and the GameObjects are
summoned again. Orphan pools are deleted. A referenced pool with missing or
incomplete position rows is logged, retired, and regenerated rather than being
silently restored with fewer objects. Collection rows are not changed, so a
previously credited indexed relic cannot grant duplicate progress after a
restart.

Non-instance outdoor maps are restored immediately through the base map.
Instance-scoped pools wait until that exact map instance exists. If a Tracking
Hunt enters a different live instance, its association moves to that instance's
pool and the old pool retires when no other Hunt references it.

## Random placement

For every candidate, the generator:

1. selects independent uniform 0–100 zone-map coordinates;
2. converts them through the client-build-12340 `WorldMapArea` zone rectangle;
3. loads the relevant map grid and obtains terrain height;
4. asks the instance's MMap navigation query for a nearby navigation polygon;
5. rejects candidates outside the assigned zone, outside valid map coordinates,
   or farther than three horizontal yards from the navigation surface.

No grid, predefined coordinate list, deliberate clustering, or minimum spacing
is used. Any nearby surface represented by the loaded MMap can be selected,
including roads, steep areas, ruins, structures, and shallows when those areas
have navigation polygons. Natural random clustering is retained.

Generation is bounded at 100 candidates per requested relic (6,000 attempts for
the default pool and at most 20,000 after configuration clamping). Acceptance
only queues a scope-level generation job. The 500 ms world update processes at
most 32 candidates globally and at most 8 for one scope before rotating to the
next scope; map and navigation access therefore stays on the world thread
without making Hunt acceptance perform the full search. Hunts sharing a scope
join the same job. A job is cancelled when no Tracking Hunt still needs its
scope or relic collection is disabled.

The pool is persisted only if the exact configured count is generated. Spawn
shortfalls are logged with actual and required counts and retried; they are
never reported as a successful smaller pool.

After a generation failure, the scope waits one minute before another bounded
attempt. This prevents the normal 500 ms reconciliation tick from repeatedly
loading terrain or flooding logs when MMap data is absent or incomplete.

Pool scope is unique in the character database. Before generation or a retry,
the manager checks for a complete persisted pool in that scope and recovers it.
If commit acknowledgement or the verification query is unavailable, the
completed in-memory job and its positions are retained; a later retry verifies
and reuses a committed pool before issuing another transaction. Because the
pool, all positions, and one assignment association commit atomically, restart
recovery can also load a transaction that committed immediately before a
worldserver interruption. Historical collection rows are never part of pool
cleanup.

### Placement limitation

AzerothCore 3.3.5a exposes a two-dimensional zone rectangle, surface/VMap
height, and MMap navigation polygons, but no API that uniformly samples every
walkable volume of a multi-level building, cave, or underwater interior.
Swimming volumes are generally not represented as walkable MMap polygons, so
fully underwater locations are not reliably generated by this runtime method.
The nearby-navigation test is stronger than accepting a height query alone,
but it cannot prove that every interior layer has equal selection probability
or that all client movement modes can reach every isolated polygon.

Complete interior coverage would require an offline build-12340 placement
catalog generated from MMap/VMap polygons, including polygon component and
liquid metadata, followed by validation tooling. That is the practical future
solution; this implementation does not invent fixed coordinates or claim the
runtime APIs provide that guarantee.

## Database deployment

New character databases require:

- `base/001_native_hunt_assignment.sql`
- `base/003_native_hunt_collection.sql`
- `base/004_native_hunt_relic_pool.sql`

Existing installations require
`updates/2026_10_18_02_native_hunt_relic_pools.sql`. The world database still
uses the existing shared-relic template SQL and repeatable display-5011 update.
No separate database or permanent `gameobject` spawn is introduced.

## Troubleshooting

- Confirm the GameObject template is entry 990000, type BUTTON, display 5011,
  and script `mod_native_hunts_shared_relic`.
- Search `module.native_hunts` logs for missing MMap data, bounded-generation
  failure, incomplete persisted positions, or spawn shortfalls.
- Confirm `native_hunt_assignment.relic_pool_id` references exactly one pool
  and that its position count equals `native_hunt_relic_pool.relic_count`.
- If count is zero, pool and position tables should be empty while historical
  `native_hunt_collection` rows remain.
- Missing MMap tiles prevent a pool from being created; the system does not
  downgrade to unverified height-only placement.

## PTR validation

1. Apply the character migration and restart worldserver with MMaps and VMaps.
2. Set `NativeHunts.Relics.CountPerZone = 60`.
3. Give Player A a Westfall Hunt and confirm one pool row with 60 position rows
   and 60 visible/interactable objects.
4. Give Player B a Westfall Hunt; confirm both assignments reference the same
   pool UUID and the position count remains 60.
5. Collect the same indexed relic with both players; both should gain tracking.
   Re-click with either player and confirm no second award.
6. Use relics and kills across both ambush thresholds and confirm each actual
   ambush must resolve before final reveal.
7. Relog each player and restart worldserver. Confirm pool UUID, coordinates,
   and collection credit remain unchanged.
8. Complete Player A's Hunt; confirm all relics remain for Player B.
9. Complete Player B's Hunt; confirm the objects and pool/position rows retire.
10. Give Player C a Westfall Hunt and compare positions; confirm a new pool UUID
    and newly randomized coordinates.
11. Repeat with two map instances where an authored hunting zone permits it;
    confirm distinct pool UUIDs and no cross-instance interaction.
12. Set count to zero and reload configuration. Confirm managed relics retire,
    clicks grant nothing, historical collection rows remain, and kill-only Hunt
    progression still completes normally.
