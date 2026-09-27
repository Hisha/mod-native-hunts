# Reference behavior to Native Hunts responsibility

This is an implementation map, not a compatibility contract. `mod-hunts` was
read only and none of its schema or allocated identities are consumed.

| Reference behavior | Native Hunts responsibility in this pass |
|---|---|
| Ten named Huntmasters at capital/hub coordinates | Managed templates and managed permanent spawns; runtime entries and exact spawn GUIDs resolved symbolically |
| Sixteen standard beast prey copied from stock visual shells | Managed templates using the same stock donors, with Native Hunts symbolic identities |
| Elite/Epic roster and class archetypes | Audited but deferred; no Elite assignments or mechanics are registered |
| Random standard prey plus level-appropriate zone | Static native catalog; eligible authored zone selected by player level without weighting zones by their site count |
| Search scope 0/1/2 | Clean `Local`/`Continent`/`World` configuration; Local preserves the explicit 59-row giver-zone intent in catalog data, Continent uses authored gameplay region, World applies no geography filter |
| Ordinary non-grey kills advance 3–7% | Player/pet hooks plus per-hunter XP-color validation |
| Two required Standard ambushes at 33%/66% | Persisted pending/completed state, temporary owner summon, tracking pause, 50% escape completion, and logout/restart recreation |
| Nearby party credit, independently checked per hunter | Same group, map, assigned zone, configured radius, and non-grey checks per assignment |
| Authored weighted final location chosen at 100% | One of all 195 known-good authored sites is selected from the assigned zone's pool and persisted before `FinalRevealed` |
| Final-location POI | Stock-client gossip POI while on the site's map plus explicit reveal/fallback text; no addon dependency or claim of a remote-map pin |
| Player-owned final activation marker | Temporary managed-template instance; player, GUID, template, state, map, zone, and persisted-site checks |
| Spawn only after successful marker validation | Managed prey template resolution; state changes to `PreyActive` only after `SummonCreature` succeeds |
| Player-relative prey scaling | Hunter level plus 6x final-health multiplier with the established low-level scale curve |
| Standard prey abilities | Established two-spell profiles carried forward as native catalog data and runtime timers |
| Correct final prey death completes encounter | Owner, runtime GUID, managed template entry, and state must all match |
| Temporary Return Rift after final credit | Managed template, runtime-only summon/GUID, owner-only grant, expiry, and exact issuer destination |
| Rift returns but does not turn in | Teleport only; assignment remains `ReadyToTurnIn` |
| Exact issuing Huntmaster performs turn-in | Entry and retained managed spawn GUID both validated |
| Reward and statistics | Physical managed Seal inventory persistence, native lifetime statistics, and assignment deletion share one character-database transaction; no virtual balance |
| Full bags refuse reward | `CanStoreNewItem` preflight and failed creation leave assignment intact |
| Abandon/reset cleanup | Domain abandon, persistent row deletion, and temporary object/prey cleanup |
| Restart behavior | Tracking/revealed/ready remain; active prey recovers to revealed; runtime GUIDs are never persisted |
| Configuration | enable/debug/minimum level/progress range/group radius/Seal count/Rift enable, duration, and arrival distance |

The reference implementation's reward equipment rolls, guard-direction
integration, Elite unlock/daily rules, dynamic authoring tools,
shared final-kill credit, and client-addon vendor protocol are deliberately not
part of this standard native pass.
