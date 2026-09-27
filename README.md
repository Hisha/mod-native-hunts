# mod-native-hunts

`mod-native-hunts` is an independent AzerothCore WotLK Hunt implementation. It
uses Content Manager resources and its own `native_hunt_*` persistence; it does
not read, migrate, or preserve `mod-hunts` runtime or currency data.

## Implemented standard loop

Ten managed capital-city Huntmasters issue a random standard-prey assignment
and an authored, level-appropriate hunting zone. Non-grey ordinary creature
kills made by the hunter, their pet, or a nearby party member advance tracking.
Assignment geography is configurable as `Local`, `Continent`, or `World`.
`Local` is the default and uses the established explicit Huntmaster-to-zone
allowlists; `Continent` uses authored gameplay regions rather than raw map IDs;
`World` retains unrestricted level-appropriate selection. Candidate zones are
deduplicated before selection, so additional final sites never weight a zone.
The catalog contains all 195 established authored sites (84 Eastern Kingdoms,
63 Kalimdor, 21 Outland, and 27 Northrend). At 100%, one site is selected from
the assigned zone's complete pool, persisted, and reconstructed as an
owner-bound trail crystal when the hunter approaches it.

Standard tracking includes two required ambushes by default, at approximately
one-third and two-thirds progress. The assigned prey attacks as a temporary
owner summon and must be driven below its escape-health threshold. While it is
pending, ordinary kills cannot advance tracking. Pending state survives logout
and restart, while temporary creature GUIDs do not.

The crystal validates the player, assignment, state, managed object identity,
map, zone, and persisted site before summoning the assigned managed prey. A
successful summon changes the state to `PreyActive`; failure leaves the crystal
encounter retryable. Standard prey use player-relative health scaling and the
established two-ability profiles from the reference implementation.

At final reveal the player receives an explicit server message. A stock-client
`SMSG_GOSSIP_POI` marker is sent and periodically refreshed while the player is
on the final site's map. The 3.3.5a protocol cannot reliably place a remote-map
POI, so the message says when no pin is available until map arrival. No HuntsUI
or other addon is required. `.nativehunts status` and `.nativehunts reset` are
bounded GM diagnostics using the normal server-info RBAC permission.

The correct final kill changes the assignment to `ReadyToTurnIn` and may create
a temporary owner-bound Return Rift. The rift returns the player near the exact
issuing managed Huntmaster and never completes the Hunt. Turn-in preflights bag
space and creates the physical Huntmaster's Seal in the live inventory. The
public AzerothCore `SaveInventoryAndGoldToDB` API then appends that inventory
state, lifetime statistics, and assignment deletion to one character-database
transaction whose asynchronous result is waited on. A confirmed rollback
removes the provisional live Seal and leaves the Hunt ready to retry.

The residual failure window is a lost or indeterminate commit acknowledgement
while the character database is also unreadable. The module cannot safely know
whether the transaction committed, so it leaves the provisional live Seal in
place and blocks another reward. On the next turn-in attempt after database
recovery it treats the persisted assignment as authoritative: a missing
assignment confirms completion without another Seal; a retained assignment
causes the provisional Seal to be removed and leaves the Hunt ready to retry.
Logout performs the same check and removes the provisional live Seal whenever
the assignment is retained. If the database remains unreadable at logout, the
module also removes that provisional Seal to prevent a later character save
from making it durable beside the assignment. The residual tradeoff is possible
reward loss if the transaction actually committed but its result and the
confirming read were both unavailable at logout; this case requires operational
recovery rather than risking duplicate rewards. A process loss before any
independent character save is resolved by the transaction's already-durable
inventory and assignment state on login. Out-of-band edits to these rows during
turn-in remain unsupported. Abandonment clears the assignment and temporary
runtime objects.

## Managed content

`content/mod-native-hunts.epf` declares, without authored allocated IDs:

- ten Huntmaster templates and permanent managed spawns;
- sixteen standard-prey templates copied from established stock donors;
- permanent trail-crystal and Return Rift templates (runtime instances remain
  temporary and are never managed spawns);
- the physical Huntmaster's Seal item, CurrencyTypes row, and `Hunts` currency
  category.

Every allocated resource is resolved at runtime through `ContentResourceApiV1`.
Gameplay stays unavailable with a specific logged reason unless every required
resource belongs to the ACTIVE/APPLIED, parity-verified package and is loaded by
the worldserver.

## Persistence and restart behavior

Apply the character database files under `data/sql/db-characters/base` through
the normal AzerothCore module SQL process. `native_hunt_assignment` persists the
issuer, prey, tier, zone, domain state, progress, final site, timestamps, and
revision. `native_hunt_stats` owns lifetime completion statistics.

- `Tracking`, its completed/pending ambush state, `FinalRevealed`, and
  `ReadyToTurnIn` remain unchanged.
- `PreyActive` recovers to `FinalRevealed`; temporary creature/object GUIDs are
  never persisted.
- an idle character has no assignment row.

## Deliberate boundaries

This pass does not implement Elite/Epic assignments, dynamic final-site
generation, guard directions, a Seal equipment vendor, or a native client UI.
The existing snapshot/domain boundary remains independent of HuntsUI and ready
for a future UI adapter.

## Checks

Run:

```text
python3 tests/run_checks.py
```

The runner compiles strict C++17 domain/gameplay tests, verifies the EPF resource
contract, checks native-only source safety, and confirms the persistence files.
