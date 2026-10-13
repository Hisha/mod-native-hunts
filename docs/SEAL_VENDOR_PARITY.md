# Huntmaster's Seal vendor parity

## Reference behavior audit

The authoritative `mod-hunts` flow is implemented by `HuntManager` and the
Huntmaster script. It is a dynamic, player-specific equipment store rather than
a static vendor list.

1. **Catalog generation.** The server scans the loaded `item_template` store on
   demand. For a selected specialization, tier, and slot category it filters
   every template, calculates a role/stat score, sorts by score, item level, and
   item ID, and returns at most 20 entries. No curated item list is involved.
2. **Tiers.** Four independently configurable bands default to 213-219, 226-232,
   245-251, and exactly 264. Reversed minimum/maximum settings are normalized.
   A zero cost disables that tier.
3. **Costs.** Tier costs default to 5, 10, 20, and 30 Seals. The catalog and
   confirmation text read the same server configuration used by purchase
   validation.
4. **Class and specialization.** Each class exposes its three Wrath talent
   trees, so selecting another tree is the off-spec flow. Role weights distinguish
   Strength melee, Agility melee, Hunter ranged, caster, healer, and tank gear.
   Weapon, shield, off-hand, ranged, and relic rules are specialization-specific.
   `Player::CanUseItem` supplies final class, race, proficiency, and level checks.
5. **Categories.** The store has Weapon, Head, Neck, Shoulder, Back, Chest,
   Wrist, Hands, Waist, Legs, Feet, Ring, Trinket, OffHand, and Relic categories.
   Chest includes robes; OffHand includes shields, holdables, and off-hand
   weapons; Relic also owns bows/guns/crossbows, wands, and thrown weapons.
6. **Main/off spec.** There is no persisted main-spec restriction. The player
   explicitly chooses any of the class's three talent trees each time the store
   opens. This is the reference implementation's main/off-spec behavior.
7. **Exclusions.** Only Epic weapons and armor in the tier band are considered.
   Heroic-tooltip items and resilience items are excluded. Wrong armor material,
   unusable items, spec-incompatible items, and candidates with a non-positive
   role score are excluded.
8. **Inventory.** `CanStoreNewItem` must succeed before a purchase changes
   currency or creates the item. Full bags and unique/limit restrictions fail
   without charging the player.
9. **Purchase and binding.** The server re-runs eligibility independent of the
   displayed selection, checks the configured tier and balance, creates one item,
   marks the item instance bound, and sends the normal new-item notification.
10. **Interfaces.** The complete stock-client interface is Huntmaster gossip:
    spec -> tier -> category -> up to 20 items -> confirmation. The later
    `mod-hunts` addon path also sends a synthetic merchant catalog and handles a
    custom addon purchase message, but retains server-side validation and the
    gossip fallback.
11. **Configuration.** Cost and minimum/maximum item level are configurable per
    tier. Availability also requires the module to be enabled and the player to
    meet the greater of the Elite Seal minimum level and endgame reward level.
12. **Balance.** The reference repository stores a virtual balance in
    `hunt_stats.huntmaster_seals`, conditionally decrements it, and compensates on
    item-creation failure. Native Hunts already replaced that balance with the
    carried managed Seal item and exposes it through the Hunts currency category.

## Pre-implementation comparison

| Capability | Previous Native Hunts state | Result |
| --- | --- | --- |
| Managed Seal item and Hunts currency category | Implemented | Preserved |
| Physical Seal rewards and two-Seal fallback | Implemented | Preserved |
| Seal balance in Native Hunts UI | Implemented | Preserved |
| Dynamic equipment catalog and tier configuration | Missing | Implemented |
| Class/spec and slot/category filtering | Elite reward helpers existed; vendor absent | Implemented using the same helpers |
| Main/off-spec selection | Missing | Implemented through three class specs |
| Heroic, resilience, armor, and usability exclusions | Elite path was partial; vendor absent | Implemented for vendor |
| Inventory validation, purchase, and soulbinding | Missing | Implemented |
| Existing Huntmaster entry point | Hunt gossip implemented | Extended without adding an NPC |
| Standard merchant presentation | Incompatible with a fully dynamic store | Gossip used, matching the reference stock-client fallback |
| Virtual Seal balance | Correctly absent | No second balance introduced |

## Native architecture decision

Content Manager supports symbolic, allocator-owned `ItemExtendedCost` rows. Four
static tier costs could therefore be declared without hard-coded IDs. That alone
does not make the dynamic store a valid standard merchant: AzerothCore validates
`CMSG_BUY_ITEM` against the creature's loaded `npc_vendor` list, while this
catalog varies by player, class, selected spec, tier, and category. A handcrafted
`SMSG_LIST_INVENTORY` packet is only presentation and cannot provide that server
authorization. Content Manager also intentionally does not synthesize per-player
vendor rows.

The smallest compatible adaptation is the reference implementation's full gossip
fallback. Purchases use the already resolved `mod-native-hunts/huntmaster-seal`
item resource, not an extended-cost ID. This keeps the standard 3.3.5a client,
requires no addon or new NPC, and preserves every selection stage. No EPF change
is required.

## Native purchase transaction

The confirmation context is server-owned and erased before purchase execution,
so a replayed confirmation has no pending item. The purchase method independently
validates store availability, class/spec membership, tier cost, complete item
eligibility across all categories, carried (not banked) Seal balance, and inventory
storage. In the serialized gossip handler it creates the item only after all
checks pass, consumes the physical managed Seals at the single deduction site,
binds the item instance, and queues a Native Hunts UI balance refresh. If item
creation unexpectedly fails, no Seal has yet been consumed.

## PTR deployment and verification

1. Back up the PTR character/world/auth databases and record the currently active
   Content Manager build and published MPQ SHA-256.
2. Deploy this module source to the AzerothCore module tree, merge the new
   `NativeHunts.SealStore.*` settings into the PTR configuration, configure, and
   build worldserver. This change adds no database migration.
3. Rebuild `content/mod-native-hunts.epf` only if its existing source package is
   not already the intended package version. Keep the package key exactly
   `mod-native-hunts`; the vendor adds no EPF resources or extended costs.
4. In the PTR console run `.content status`, `.content scan`, and `.content
   allocations`. Confirm `huntmaster-seal` resolves as the existing managed item
   (PTR currently expects item 56807) and `huntmaster-seal-currency` remains in
   category `hunts`.
5. If the EPF bytes did not change, keep the current active content build. If an
   authorized cumulative client rebuild is required, run `.content install
   mod-native-hunts`, `.content build`, record the new build number/SHA-256, then
   `.content activate <build-number>`. Distribute that exact published artifact.
6. Restart worldserver so the new module binary/config loads. If a new client
   artifact was activated, fully exit WoW, install that artifact, and restart the
   client; `/reload` is insufficient for patch replacement.
7. With a level-80 character of each class, open an existing Huntmaster. Verify
   all three specs, four tiers, and all 15 categories appear, with the configured
   costs and ranges. Spot-check tank/healer/DPS, dual-wield/two-hand, shield,
   wand/ranged, and class relic results.
8. Inspect candidate templates/tooltip data to prove Heroic-tooltip and resilience
   items are absent. Try an item at each tier boundary.
9. Attempt purchases with insufficient Seals, a full bag, and a unique/limit
   conflict. Confirm the item and Seal counts do not change.
10. Purchase one item successfully. Confirm exactly the configured carried Seals
    disappear, exactly one bound item appears, the Native Hunts balance refreshes,
    and banked Seals were not counted.
11. Replay the confirmation action and forge spec/tier/item actions with a packet
    test client. Confirm no second item or deduction and that invalid class/spec,
    tier, and item combinations fail.
12. Complete an Elite Hunt with and without an eligible upgrade. Confirm the
    existing equipment reward, normal Seal, two-Seal no-upgrade fallback, mail
    fallback, Return Rift, assignment, tracking, and UI layout are unchanged.

## Remaining limitations

Local tests cannot exercise AzerothCore `Player` inventory mutation, live DBC/item
templates, packet replay, Content Manager activation, or the 3.3.5a client. Those
checks remain mandatory PTR acceptance items. The stock gossip catalog is limited
to the reference maximum of 20 ranked items per selected category.
