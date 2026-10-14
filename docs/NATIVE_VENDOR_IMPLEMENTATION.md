# Native Huntmaster Merchant Implementation

## Implemented architecture

The Huntmaster equipment action now opens Blizzard's standard 3.3.5a
MerchantFrame. The module builds a per-player catalog from
`NativeHuntsManager::BuildSealStoreItems`, sends `SMSG_LIST_INVENTORY`, and
retains the exact row vector in a five-minute server session.

`NativeHuntsPlayerScript::OnPlayerBeforeBuyItemFromVendor` intercepts only a
matching Huntmaster session. It validates the live vendor, interaction range,
session expiration, quantity, zero-based vendor slot, item identity, configured
price, carried Seal balance, and storage before delegating to
`PurchaseSealStoreItem`. The manager remains the only code that creates the
item and deducts Seals. A successful custom purchase sends the native merchant
acknowledgement, refreshes the inventory and balance, and installs a short
same-slot/item replay guard. Other vendors fall through to AzerothCore
unchanged.

The merchant packet limit is 150 rows. Catalog assembly uses rank round-robin
across every nonempty tier/category group: every available group receives its
top-ranked item before any group receives its next-ranked item. If more than
150 eligible rows exist, the server logs the eligible/exposed counts instead of
silently dropping a whole equipment category or price tier.

The prior specialization/tier/category/item gossip flow remains compiled as a
compatibility fallback and is entered only if the native merchant cannot open.

## Client patch

`NativeHuntsFrame.lua` creates one `UIDropDownMenuTemplate` child on the stock
MerchantFrame. The dropdown is visible only after a Native Hunts vendor-state
message and is hidden/reset when MerchantFrame closes, so ordinary vendors are
unchanged. Its three labels come from `GetTalentTabInfo`.

Selecting a row sends `NHUNTS` request `1<TAB>V<TAB>index` to the server. The
server maps the 1–3 index to the character class's existing talent-tree IDs and
rebuilds the catalog. No talent APIs that mutate the active spec are called.
The Hunt Record, FrameForge export, Dungeon Finder/Hunts geometry, and package
identity are unchanged.

## Managed content

Package `mod-native-hunts` version `22-ptr1` adds four semantic extended-cost
symbols:

- `seal-cost-tier-1`: 5 Huntmaster's Seals
- `seal-cost-tier-2`: 10 Huntmaster's Seals
- `seal-cost-tier-3`: 20 Huntmaster's Seals
- `seal-cost-tier-4`: 30 Huntmaster's Seals

Content Manager allocates every `item-extended-cost.id`; the module resolves
each symbol dynamically. No allocated ID is authored in source. At startup and
again while constructing packets, the module verifies that every resolved DBC
row references the managed Seal and matches the configured tier price. A
mismatch disables only the native merchant path and leaves Hunts and the gossip
fallback available.

Because applied ItemExtendedCost definitions are immutable, changing a tier
price requires authoring a new matching semantic cost symbol, updating the
module's symbolic resolver to that new immutable resource, rebuilding/activating
content, and restarting worldserver. Changing only the server `.conf` price
deliberately does not show an incorrect client price; native merchant opening
fails closed instead.

All ten existing Huntmasters now use NPC flags 129 (gossip plus vendor). No NPC,
spawn, `npc_vendor` row, currency, or standalone addon was added.

## PTR deployment

1. Back up the PTR world/character databases and current cumulative client
   artifact.
2. Install the updated module source and rebuild/install worldserver normally.
3. Place `content/mod-native-hunts.epf` in Content Manager's configured package
   discovery directory.
4. In worldserver, run `.content scan` and confirm package
   `mod-native-hunts` version `22-ptr2` is discovered.
5. Run `.content install mod-native-hunts` if that version is not already the
   selected package.
6. Run `.content build`, then `.content build list`; retain the new STAGED build
   number.
7. Inspect `.content server status <build-number>`, then run
   `.content activate <build-number>`. Activation must apply/verify the managed
   item, extended-cost, currency, creature-template and spawn prerequisites
   before publishing the client artifact.
8. Distribute the exact activated cumulative MPQ to the PTR client patch path.
9. Restart worldserver so `itemextendedcost_dbc` and Huntmaster NPC flags reload;
   restart the client so the DBC and FrameXML patch reload.
10. Confirm Native Hunts initializes with resources ready and without a
    `Native Huntmaster merchant disabled` log entry.

## PTR acceptance path

1. Open an ordinary vendor first and confirm its inventory and purchases are
   unchanged and no Native Hunts dropdown appears.
2. On two different level-cap characters, open the same Huntmaster and choose
   **Browse Huntmaster's Seal rewards**. Confirm MerchantFrame opens and each
   player sees an independent class/spec catalog.
3. Confirm the dropdown has exactly the class's three talent trees. Record the
   active talent group/tree, select all three shopping choices, and confirm the
   gameplay talents never change.
4. For every selection, browse the native merchant pages and confirm available
   equipment categories and enabled tiers are represented, with native icons,
   names, tooltips, and the correct Seal prices.
5. Buy one item with sufficient Seals. Confirm exactly one item is delivered,
   the configured Seal amount is removed once, the merchant refreshes, and the
   Hunt Record balance updates.
6. Double-click/replay the same row and confirm the short duplicate request is
   rejected without a second delivery or deduction.
7. Test insufficient Seals and a full inventory; both must reject without any
   item or currency mutation.
8. Change specialization and immediately attempt an old slot/item purchase;
   confirm it is rejected as stale.
9. Walk out of interaction range, wait beyond five minutes, logout/login, and
   open another vendor; confirm stale Huntmaster sessions cannot purchase.
10. Run Standard and Elite Hunt smoke paths, turn-in, Return Rift, Elite reward,
    Two-Seal fallback, tracking, and Hunt Record visibility checks.

## Remaining limitations

- The 3.3.5a inventory packet has no catalog revision. Safety comes from exact
  vendor/slot/item matching, current eligibility checks, expiration, atomic
  session replacement, and short replay suppression.
- One packet can expose at most 150 items. The fair limiter preserves every
  nonempty tier/category group but may omit lower-ranked alternatives within a
  group; the truncation is logged.
- Local automated checks validate protocol parsing, packaging, Lua 5.1 behavior,
  resource declarations and source contracts. A running worldserver/client is
  still required to prove real packet/UI behavior.
