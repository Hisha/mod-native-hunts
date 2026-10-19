# Native Hunts

**A hunt-driven adventure system for AzerothCore (Wrath of the Lich King, 3.3.5a).**

Native Hunts adds repeatable hunts across Azeroth and beyond. Visit a Huntmaster, track a dangerous quarry through a level-appropriate zone, withstand ambushes, and follow the trail to a final encounter. Along the way, discover cursed relics that offer another way to advance your search. Complete hunts to earn rewards and work toward more challenging Elite Hunts.

The experience is integrated into the original WoW client, with a dedicated Hunts tab in the Dungeon Finder interface, world-map guidance, Huntmasters in major cities, and a native equipment merchant. **No standalone client addon is required.** Client content is delivered through the project's managed Content Manager/MPQ pipeline.

## The hunt

1. **Accept an assignment.** Speak with a Huntmaster to receive a quarry and a level-appropriate hunting zone.
2. **Follow the trail.** Defeat eligible creatures in the hunting grounds, or examine scattered cursed relics for optional tracking progress. Your pet and nearby party members can also contribute eligible kill credit.
3. **Survive the ambushes.** Your quarry strikes back during the search. Drive it below its escape-health threshold to resume tracking.
4. **Find the final site.** When tracking is complete, follow the in-game guidance to a trail crystal and summon the final confrontation.
5. **Claim your reward.** Defeat the quarry and return to the issuing Huntmaster. A temporary Return Rift can help with the journey back.

Hunts are designed to be resumed: assignment progress, ambush milestones, final-site selection, and relic collection history are persisted. Temporary summoned creatures and objects are reconstructed or recovered as appropriate rather than treated as permanent world spawns.

## Features

### Standard and Elite Hunts

Standard Hunts are available from managed Huntmasters in ten capital-city locations, with assignments drawn from authored hunting grounds appropriate to the character's level. The module includes **195 authored final encounter sites** across Eastern Kingdoms, Kalimdor, Outland, and Northrend.

Standard tracking includes **two required ambushes by default**. Elite Hunts unlock after the configured number of Standard completions and offer their own difficulty, daily limit, and reward settings. The Huntmaster explains the assignment and its objectives in-game.

### Cursed relics

Cursed relics are optional discoveries scattered across the assigned hunting zone. Interacting with one begins the custom **Examine Cursed Relic** cast; a successful examination contributes to tracking. Players can complete hunts through creature kills without collecting relics.

Relics use **randomized, shared pools** rather than fixed permanent spawn points. Players hunting the same zone share the physical relic locations but collect each relic independently. Pools persist while active or resumable hunts depend on them; after the last associated hunt ends, the pool retires and a future hunt generates new locations. Collection credit survives relogs and restarts, and already-collected relics are hidden from the collector without removing them for others.

The default is **60 relics per active zone pool**, with a **three-second interruptible examination cast**. Administrators can adjust both settings, including disabling relics entirely with a count of `0`.

See [Relic Pools](docs/RELIC_POOLS.md) for generation, persistence, placement limitations, and troubleshooting.

### Native client interface

Native Hunts adds a **Hunts** tab alongside the stock Dungeon Finder interface, presenting hunt details and progression without requiring an independently installed addon. The client UI is packaged as managed FrameXML, Lua, and artwork through Content Manager. Final-site guidance also uses stock-client map-point-of-interest messages when the player is on the appropriate map.

See [Client UI](docs/CLIENT_UI.md) for implementation and content details.

### Rewards and the Huntmaster's Seal

Completing hunts awards the configured rewards and updates lifetime hunt statistics. Elite Hunts can award **Huntmaster's Seals**, which are represented as a physical item and managed currency.

Huntmasters offer a native equipment merchant using Blizzard's MerchantFrame. The catalog is generated from eligible equipment and supports a specialization dropdown for the character's three talent trees. Purchases are validated server-side against the selected item, price, currency, and available inventory space. The legacy gossip store remains as a compatibility fallback.

### Designed for persistence and safe recovery

Hunt assignments, milestones, statistics, relic pools, and per-hunt collection credit are stored in the character database. The module validates completion and reward delivery against persistent state to reduce duplicate rewards and preserve recoverable hunts across worldserver restarts. See the implementation documentation and source for transaction and failure-recovery behavior.

## Requirements

- **AzerothCore** with the compatible **mod-playerbots**-based server environment used by this module.
- **World of Warcraft 3.3.5a (build 12340)** client.
- **mod-content-manager**, including managed spell cloning and the resource API required by this package.
- The module's character-database SQL schema and updates.
- A compatible client MPQ produced and distributed through the existing Content Manager pipeline.

Native Hunts is a standalone implementation with its own `native_hunt_*` data. It does **not** import or migrate the runtime state or currency data of the older `mod-hunts` module.

## Installation and deployment

Native Hunts has **two coordinated parts**: the AzerothCore server module and the managed client/server content package. They must be deployed as a compatible set.

1. Install `mod-native-hunts` and a compatible `mod-content-manager` in the AzerothCore modules tree.
2. Apply the appropriate character-database base SQL for a fresh installation, or the supplied update migrations for an existing installation, through your normal AzerothCore module SQL workflow.
3. Configure Native Hunts using `conf/mod_native_hunts.conf.dist` as the reference, preserving your deployment's existing configuration.
4. Build and install the AzerothCore worldserver with both modules enabled.
5. Import `content/mod-native-hunts.epf` into Content Manager, build cumulative content, and inspect the resource allocation and parity results.
6. Activate the matching managed content so the required resources are **ACTIVE/APPLIED**, including the custom relic examination spell and its server-side `spell_dbc` record.
7. Publish the generated MPQ through the managed client distribution pipeline, then restart the worldserver and patched client.
8. Verify hunt acceptance, UI display, tracking, relic examination, completion, and Huntmaster rewards in-game.

**Do not copy a custom spell ID or an MPQ from another realm without verifying that realm's allocation registry and content state.** Content Manager assigns and retains symbolic resource IDs independently for each environment. Native Hunts resolves required resources at runtime and refuses to expose gameplay when required active content is missing or mismatched.

See [Server Parity](docs/SERVER_PARITY.md) for the client/server content contract.

## Configuration

The full set of settings and explanations is in [`conf/mod_native_hunts.conf.dist`](conf/mod_native_hunts.conf.dist). Common settings include:

| Setting | Default | Purpose |
| --- | --- | --- |
| `NativeHunts.Enable` | `1` | Enable Native Hunts. |
| `NativeHunts.MinimumLevel` | `10` | Minimum level for assignments. |
| `NativeHunts.AssignmentScope` | `Local` | Huntmaster-local, continent, or world-wide eligible zone selection. |
| `NativeHunts.Tracking.ProgressMin` / `ProgressMax` | `3` / `7` | Tracking progress range. |
| `NativeHunts.GroupCreditRadius` | `100.0` | Radius for eligible nearby party kill credit. |
| `NativeHunts.Ambush.Count` | `2` | Required Standard Hunt ambushes; `0` disables them. |
| `NativeHunts.Relics.CountPerZone` | `60` | Shared relic count per active zone pool; `0` disables relic collection. |
| `NativeHunts.Relics.CollectionCastTimeMs` | `3000` | Examination cast time; `0` makes collection instant. |
| `NativeHunts.Elite.RequiredStandardCompletions` | `10` | Standard completions needed to unlock Elite Hunts. |
| `NativeHunts.Elite.DailyLimit` | `1` | Daily Elite assignment allowance. |
| `NativeHunts.ReturnRift.Enable` | `1` | Enable the temporary return portal after the final encounter. |

Other options control XP, ambush and Elite difficulty, Elite rewards, equipment merchant tiers, and Return Rift duration and placement. Assignment scope defaults to authored Huntmaster-local zone lists; `Continent` and `World` broaden selection to other eligible authored zones.

## Managed content

The EPF package at [`content/mod-native-hunts.epf`](content/mod-native-hunts.epf) describes the module's client and server content without assigning fixed numeric resource IDs. It includes Huntmasters, prey templates, trail and Return Rift objects, the Hunts interface, Huntmaster's Seal and merchant costs, and the **Examine Cursed Relic** spell cloned from stock spell 61413.

Content Manager generates the corresponding managed resources, client patch, and server-side data. Native Hunts uses `ContentResourceApiV1` to resolve and verify required resources at runtime. The original donor spell remains unchanged.

## Administration and testing

The module provides bounded GM diagnostics through:

```text
.nativehunts status
.nativehunts reset
```

These use the normal server-info RBAC permission. For automated checks, run:

```bash
python3 tests/run_checks.py
```

The test runner covers C++17 domain/gameplay behavior, managed package contracts, persistence expectations, and source-safety checks. A successful build and automated checks do not replace in-game verification of the patched 3.3.5a client.

## Documentation

- [Relic Pools](docs/RELIC_POOLS.md) — shared spawning, collection, recovery, and placement constraints.
- [Client UI](docs/CLIENT_UI.md) — Hunts interface and managed client assets.
- [Server Parity](docs/SERVER_PARITY.md) — managed resources and client/server consistency.
- [Managed Spells](https://github.com/Hisha/mod-content-manager) — Content Manager repository and spell-management implementation.

---

**Native Hunts** brings tracking, ambushes, exploration, and rewards into a repeatable hunt experience built for the AzerothCore WotLK world.
