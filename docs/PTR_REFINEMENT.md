# PTR refinement deployment and validation

## Database ownership

Native Hunts owns `native_hunt_assignment` and `native_hunt_stats` only in the
character database. The repository has no world-database source for either
table, and repository history shows `native_hunt_assignment` was introduced
only under `db-characters`. The empty PTR world table is therefore stale or was
created by applying the character base file to the wrong database.

After backing up PTR and verifying that the world-side table is empty, remove
only that erroneous PTR table manually. This statement is deliberately not a
normal module migration:

```sql
DROP TABLE IF EXISTS `acore_world_ptr`.`native_hunt_assignment`;
```

Apply the character update
`data/sql/db-characters/updates/2026_09_26_00_native_hunt_ambush.sql` to the PTR
character database through the normal module updater before starting the new
binary.

## Content lifecycle

The package version is 3. The trail crystal now copies the clean, zero-data
gameobject donor 19529 and explicitly retains display 7942. Both it and the
Return Rift are type-10 scripted goobers with no lock, quest, autoclose, or
spell behavior. Content Manager must scan, uninstall/install the package
selection, build, and activate the new cumulative build. Confirm server state
`APPLIED`, distribute the matching client artifact, and restart worldserver so
the object manager loads the new templates.

## Map-marker behavior

The pre-refinement Native Hunts path did not send any POI packet; it only
persisted the site and reconstructed the crystal. The reference module used
`SMSG_GOSSIP_POI`, the stock 3.3.5a guard-direction marker. Native Hunts now
uses that packet while the player is on the final site's map and refreshes it
periodically. It does not depend on HuntsUI. Cross-map pins are not represented
reliably by this protocol, so explicit server text is the fallback until the
player reaches the destination map.

## Focused PTR path

1. Confirm `NativeHunts.AssignmentScope = Local` and use a Huntmaster with a
   level-appropriate local catalog (Dalaran/Varyn for a level-80 test).
2. Accept a Standard Hunt and inspect `.nativehunts status`.
3. Track ordinary non-grey kills in the assigned zone.
4. At the first threshold, confirm the ambush appears, ordinary kills stop
   advancing progress, and the ambush escapes near 50% health.
5. Repeat for the second ambush; verify logout/relogin during a pending ambush
   recreates it without a persistent spawn.
6. Reach 100% and confirm the explicit reveal message, stable final-site key,
   and stock-client POI on the correct map. If crossing maps, confirm the
   graceful text fallback until arrival.
7. Travel to the site, click the Prey Trail Crystal, and confirm the exact
   assigned prey spawns. Verify another player cannot activate it.
8. Defeat the prey and confirm `ReadyToTurnIn` plus an owner-only Return Rift.
9. Use the Rift and confirm arrival near the exact issuing Huntmaster.
10. Turn in at that Huntmaster; confirm one physical Seal, assignment removal,
    and incremented lifetime statistics.
