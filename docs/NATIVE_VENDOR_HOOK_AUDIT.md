# Native Huntmaster Vendor Hook Audit

## Scope and source baseline

This is a read-only architecture audit. It does not change AzerothCore, Native Hunts production code, FrameForge, or EPF assets.

Sources inspected:

- AzerothCore: `/home/smithkt/aicode_folder/azerothcore-wotlk`
- Branch/commit: `Playerbot` at `2a2211cd8f3d157da432ec0175ddd4b8a191931f`
- Upstream comparison: local `origin/master` at `e72b2219697d098f861e91d6c52064bfe20644c9`
- Native Hunts: `/home/smithkt/git/mod-native-hunts`

The existing dynamic catalog and purchase authority are `NativeHuntsManager::BuildSealStoreItems` and `NativeHuntsManager::PurchaseSealStoreItem`. This audit treats them as functionality to reuse, not replace.

## Executive conclusion

AzerothCore can support the genuine WoW 3.3.5a MerchantFrame for a dynamic, player-specific Huntmaster catalog **without a core change**, provided the module owns two pieces of the transaction:

1. Build and send `SMSG_LIST_INVENTORY` from a module-owned, per-player vendor session.
2. Intercept `CMSG_BUY_ITEM` through `PlayerScript::OnPlayerBeforeBuyItemFromVendor`, process it with the existing Native Hunts purchase backend, and set the mutable `item` argument to zero to stop the stock purchase path.

This uses the native merchant window, item rows, tooltips, paging, and normal client buy packets. It does **not** use AzerothCore's stock `VendorItemData` validation, extended-cost deduction, or delivery for Huntmaster purchases; the current Native Hunts backend remains authoritative.

Existing hooks cannot inject arbitrary per-player `VendorItemData` into the stock list and buy paths. The inventory hook can only redirect to another globally cached vendor entry. Therefore:

- **Recommended:** module-owned list packet plus module-owned purchase interception. No core modification.
- **If `Player::BuyItemFromVendorSlot` itself must perform the full transaction:** add a small core extension that resolves the same player-specific inventory in both listing and purchase validation.

Temporarily mutating `ObjectMgr` vendor caches is not valid: the caches are global and would create cross-player leakage and stale/racy purchases.

## 1. Stock AzerothCore merchant flow

### 1.1 Opening a merchant

There are two stock entry paths.

For a gossip vendor option:

1. `WorldSession::HandleGossipSelectOptionOpcode` in `src/server/game/Handlers/MiscHandler.cpp` receives the selection.
2. It invokes the creature AI callback and `sScriptMgr->OnGossipSelect(...)`.
3. If the script hook does not consume it, `Player::OnGossipSelect` in `src/server/game/Entities/Player/PlayerGossip.cpp` handles the database option.
4. `GOSSIP_OPTION_VENDOR` and `GOSSIP_OPTION_ARMORER` call:

   ```cpp
   GetSession()->SendListInventory(guid, menuItemData->GossipActionMenuId);
   ```

The optional gossip action menu ID is used as an alternate vendor entry.

For a direct client request, `WorldSession::HandleListInventoryOpcode` in `src/server/game/Handlers/ItemHandler.cpp` handles `CMSG_LIST_INVENTORY`, rejects dead players, and calls `SendListInventory(packet.VendorGuid)`.

### 1.2 Building and sending the inventory

`WorldSession::SendListInventory(ObjectGuid vendorGuid, uint32 vendorEntry = 0)` is defined in `src/server/game/Handlers/ItemHandler.cpp` and declared in `src/server/game/Server/WorldSession.h`.

Its relevant sequence is:

1. Invoke `OnPlayerSendListInventory`, allowing scripts to rewrite `vendorEntry`.
2. Resolve an interactable creature with `UNIT_NPC_FLAG_VENDOR` through `GetNPCIfCanInteractWith`.
3. Pause the vendor and save `vendorEntry` in the session with `SetCurrentVendor`.
4. Select one global inventory:

   ```cpp
   vendorEntry
       ? sObjectMgr->GetNpcVendorItemList(vendorEntry)
       : vendor->GetVendorItems();
   ```

5. Filter rows for stock, conditions, faction, and the core's limited class/bind rules.
6. Calculate money prices and reputation discount.
7. Serialize and send `WorldPackets::Item::ListInventory` (`SMSG_LIST_INVENTORY`).

Each row contains the one-based client slot (`slot + 1`), item entry, `DisplayInfoID`, stock, gold price, maximum durability, template `BuyCount`, and `ItemExtendedCost` ID. `MAX_VENDOR_ITEMS` is 150 and the packet count is a `uint8`; a custom catalog must enforce the same limit.

`VendorItem` and `VendorItemData` are defined in `src/server/game/Entities/Creature/CreatureData.h`. `Creature::GetVendorItems` in `Creature.cpp` delegates to the global `ObjectMgr` cache. `ObjectMgr::LoadVendors` in `src/server/game/Globals/ObjectMgr.cpp` loads `npc_vendor` into `_cacheVendorItemStore`; `AddVendorItem` and `RemoveVendorItem` also mutate this shared cache. `IsVendorItemValid` validates static rows, including item-template and extended-cost existence. None provide player-local inventory state.

`WorldSession::SetCurrentVendor` stores only an alternate global vendor entry ID, not a list or catalog revision.

### 1.3 Receiving a purchase

Packet definitions are in `src/server/game/Server/Packets/ItemPackets.h` and `.cpp`:

- `CMSG_BUY_ITEM` supplies vendor GUID, item entry, vendor slot, count, and an unknown byte.
- `CMSG_BUY_ITEM_IN_SLOT` additionally supplies destination bag GUID and bag slot.

`WorldSession::HandleBuyItemOpcode` and `HandleBuyItemInSlotOpcode` reject wire slot zero, convert the one-based wire slot to a zero-based server slot, and call:

```cpp
Player::BuyItemFromVendorSlot(
    ObjectGuid vendorguid,
    uint32 vendorslot,
    uint32 item,
    uint8 count,
    uint8 bag,
    uint8 slot);
```

The packet count is wider than the `uint8` function argument. The custom equipment handler should explicitly require count one.

### 1.4 Stock validation, costs, and delivery

`Player::BuyItemFromVendorSlot` is in `src/server/game/Entities/Player/Player.cpp`:

1. Invoke `OnPlayerBeforeBuyItemFromVendor` before normal validation.
2. If the hook changed `item` to zero, return success immediately. The source explicitly documents this as the custom-vendor escape path.
3. Validate destination, player state, item template, class/faction eligibility, vendor interaction, and conditions.
4. Resolve the same global vendor inventory used by listing, using `GetCurrentVendor()` when nonzero.
5. Require the zero-based slot to exist and its item to match the client item.
6. Check stock and reputation.
7. Resolve `ItemExtendedCostEntry` from `sItemExtendedCostStore`; check honor, arena points, up to five required item currencies, and arena rating.
8. Calculate gold and discount.
9. Call `_StoreOrEquipNewItem`.

`_StoreOrEquipNewItem` preflights storage/equip, deducts money and extended-cost currencies/items, invokes the before-store hook, stores/equips the item, updates stock, sends `SMSG_BUY_ITEM` and new-item feedback, records refund metadata when applicable, and invokes the after-store hook.

Normal failures are preflighted, but this helper has no rollback if creation unexpectedly returns null after deduction. The existing Native Hunts backend's create-before-deduct ordering should be preserved rather than replaced merely to obtain MerchantFrame.

## 2. Relevant ScriptMgr hooks

### 2.1 Inventory redirect

```cpp
virtual void OnPlayerSendListInventory(
    Player* player,
    ObjectGuid vendorGuid,
    uint32& vendorEntry);
```

- Declaration: `src/server/game/Scripting/ScriptDefines/PlayerScript.h`
- Dispatcher: `ScriptMgr::OnPlayerSendListInventory` in `ScriptDefines/PlayerScript.cpp`
- Invocation: first operation in `WorldSession::SendListInventory`
- Can: replace `vendorEntry`, selecting a different globally cached `npc_vendor` list
- Cannot: supply rows, return `VendorItemData`, veto the stock packet, or attach player-specific state
- Classification: mutable redirect, not inventory generation

Sending a custom packet inside this hook is insufficient because core continues and sends its own list afterward. The recommended flow calls a module list-sender directly from Huntmaster interaction and specialization refresh.

### 2.2 Pre-purchase replacement

```cpp
virtual void OnPlayerBeforeBuyItemFromVendor(
    Player* player,
    ObjectGuid vendorguid,
    uint32 vendorslot,
    uint32& item,
    uint8 count,
    uint8 bag,
    uint8 slot);
```

- Declaration: `PlayerScript.h`
- Dispatcher: `ScriptMgr::OnPlayerBeforeBuyItemFromVendor` in `PlayerScript.cpp`
- Invocation: first line of `Player::BuyItemFromVendorSlot`
- Can: recognize a module session, process a custom purchase, and set `item = 0` to stop core
- Cannot: return an explicit result; only `item` is mutable, and core has not yet validated interaction
- Classification: deliberate full-replacement escape path when using the zero-item sentinel

The module must independently validate exact vendor GUID, Huntmaster identity, vendor flag, alive/interactable state, distance, session catalog, slot, item, quantity, destination, eligibility, and currency before consuming the request. It must set `item = 0` only for its own recognized vendor; ordinary vendors must fall through.

### 2.3 Before-store notification

```cpp
virtual void OnPlayerBeforeStoreOrEquipNewItem(
    Player* player, uint32 vendorslot, uint32& item,
    uint8 count, uint8 bag, uint8 slot,
    ItemTemplate const* pProto, Creature* pVendor,
    VendorItem const* crItem, bool bStore);
```

This runs inside `_StoreOrEquipNewItem`, after destination checks and currency deduction. It cannot veto. Although `item` is mutable, `pProto`, destination calculations, and `crItem` already describe the original row, so changing it is unsafe. It cannot authorize a dynamic row.

### 2.4 After-store notification

```cpp
virtual void OnPlayerAfterStoreOrEquipNewItem(
    Player* player, uint32 vendorslot, Item* item,
    uint8 count, uint8 bag, uint8 slot,
    ItemTemplate const* pProto, Creature* pVendor,
    VendorItem const* crItem, bool bStore);
```

This runs after the store/equip attempt and is notification-only. `item` can be null. The general `OnPlayerStoreNewItem(Player*, Item*, uint32)` hook is also post-storage, not vendor-specific, and cannot authorize a purchase.

### 2.5 Gossip hooks

```cpp
virtual bool CreatureScript::OnGossipHello(Player*, Creature*);
virtual bool CreatureScript::OnGossipSelect(
    Player*, Creature*, uint32 sender, uint32 action);
```

`OnGossipHello` is invoked by `WorldSession::HandleGossipHelloOpcode` in `NPCHandler.cpp`; true suppresses default gossip preparation/sending. `OnGossipSelect` is invoked by `HandleGossipSelectOptionOpcode` in `MiscHandler.cpp`; true suppresses `Player::OnGossipSelect`. Either can call a module function that sends a native inventory packet.

`AllCreatureScript::CanCreatureGossipHello` and `CanCreatureGossipSelect` are global short-circuit hooks. They are unnecessary because Native Hunts already binds a `CreatureScript` to Huntmasters. Player-GUID gossip hooks are irrelevant to creature vendors.

### 2.6 Gameplay spec-slot notification

```cpp
virtual void OnPlayerAfterSpecSlotChanged(Player* player, uint8 newSlot);
```

This is dispatched at the end of `Player::ActivateSpec`. It is notification-only and represents a real dual-spec loadout switch. A shopping-specialization dropdown must not call `ActivateSpec` or use this hook as its selection mechanism.

## 3. Playerbot fork comparison

Comparison with local `origin/master` found:

- No Playerbot change to `src/server/game/Handlers/ItemHandler.cpp`.
- No Playerbot-specific `SMSG_LIST_INVENTORY`, `BuyItemFromVendorSlot`, or relevant vendor-hook implementation in the module/source search.
- The traced hook declarations, dispatchers, and call sites exist in the upstream baseline.

Some large player/script files differ elsewhere, but the relevant vendor blocks add no player-specific inventory source or alternate merchant transaction. The design should not depend on Playerbot-specific behavior.

## 4. Dynamic inventory feasibility

Stock core can open MerchantFrame, redirect to a different **static global** vendor entry, serialize normal rows/extended costs, and complete purchases for rows in that same global list.

Stock hooks cannot return arbitrary `VendorItemData`, generate different rows for simultaneous players, bind a revision to a purchase, suppress the stock packet after the list hook, or make `BuyItemFromVendorSlot` validate module-owned rows.

A module can construct `WorldPackets::Item::ListInventory` and send it to one session. The client then uses ordinary MerchantFrame names, icons, tooltips, paging, selection, and `CMSG_BUY_ITEM`. The server must retain the authoritative per-player row vector because the purchase packet contains only vendor GUID, item, slot, and count.

On purchase, the pre-purchase hook resolves the zero-based slot, requires an item match, revalidates current eligibility, delegates to `PurchaseSealStoreItem`, and sets `item = 0`. This supports arbitrary patched item templates, per-player lists, spec refresh, native list/buy packets, and no `npc_vendor` rows.

`ObjectMgr::AddVendorItem` is global. Mutating it around list sends cannot make the later asynchronous buy player-specific and risks leaking catalogs. It is rejected.

## 5. Specialization handling

AzerothCore exposes distinct concepts:

- `Player::getClass()` / `getClassMask()`: character class.
- `Player::GetActiveSpec()`: dual-spec loadout slot 0 or 1, not a talent tree.
- `Player::GetSpecsCount()` / `GetActiveSpecMask()`: dual-spec loadout state.
- `Player::GetSpec(int8 spec = -1)`: dominant `TalentTab` ID for a loadout; may return zero if untalented.
- `Player::GetTalentTreePoints(uint8 (&)[3])`: active-loadout points in the three trees.
- `Player::GetMostPointsTalentTree()`: dominant page index 0 through 2.
- `GetTalentTabPages(uint8 classId)`: the class's three `TalentTab` IDs from DBC data.
- `Player::ActivateSpec(uint8)`: changes the actual loadout and has gameplay side effects.

The shopping specialization must be separate per-player vendor-session state. Validate it against the three IDs from `GetTalentTabPages(player->getClass())`. A reasonable default is `GetSpec()` when nonzero, with a deterministic class-tree fallback. Dropdown changes must not activate or alter gameplay talents.

The existing Native Hunts specialization mapping remains authoritative for catalog semantics. Client talent APIs can provide localized tree labels in the eventual UI. This audit proposes no new layout.

## 6. Extended costs and Seals

Each native row has one extended-cost ID. AzerothCore resolves it through `sItemExtendedCostStore`; an entry can describe up to five item requirements.

The installed Content Manager pipeline already supports semantic `extendedCosts` resources and allocator-managed `item-extended-cost.id` values. Its Phase 5 documentation and resource API show DBC/SQL output and runtime resolution without manually assigning IDs or inserting `npc_vendor` rows.

For the recommended architecture, one managed extended-cost entry per price tier can be put in the row so MerchantFrame displays the required Seal count. The existing Native Hunts backend remains the sole deduction authority. Here the extended-cost entry is presentation metadata; allowing stock core and the backend both to deduct would double-charge.

Resolve and validate managed IDs at startup through the existing Content Manager API. Do not hard-code IDs.

## 7. Recommended architecture

### 7.1 Per-player server session

Maintain transient state:

```text
HuntmasterVendorSession
  vendorGuid
  selectedTalentTabId
  revision
  rows[]
    itemEntry
    tier
    specialization
    sealPrice
    extendedCostId
```

The row vector—not client-supplied tier/spec data—is authoritative. Send contiguous one-based row IDs; use the hook's already-normalized zero-based slot as the vector index.

Invalidate/replace the session on logout, another vendor interaction, invalid Huntmaster/proximity, catalog refresh, and timeout. The purchase packet has no revision field and merchant closure is primarily client-local, so correctness must not depend on a close event.

### 7.2 Open and refresh

On the existing Huntmaster store action:

1. Validate Huntmaster and player.
2. Select/validate the shopping talent tree.
3. Call `BuildSealStoreItems`.
4. Atomically create a session revision and row vector.
5. Resolve templates and managed extended-cost IDs.
6. Send normal `SMSG_LIST_INVENTORY` with the Huntmaster GUID.

The dropdown sends a talent-tab ID through the existing Native Hunts communication channel. The server validates it against the class's three tabs, replaces the row vector, then sends a fresh list. No separate communication system is needed.

Current Native Hunts Huntmaster definitions are gossip-only. Eventual implementation needs vendor capability in addition to gossip while retaining the fallback. This is a future data/configuration point, not a change made here.

### 7.3 Purchase interception

Add the pre-purchase hook to the existing Native Hunts player script:

1. Find a live session and require the exact session `vendorguid`.
2. Re-resolve the creature and repeat alive/vendor/interactability/distance checks.
3. Require `count == 1`.
4. Require `vendorslot < rows.size()` and `rows[vendorslot].itemEntry == item`.
5. Revalidate class tree, tier/category eligibility, inventory capacity, Seal balance, and backend rules.
6. Delegate to `PurchaseSealStoreItem`.
7. Set `item = 0` after handling the recognized request, on success or handled failure.
8. Send native buy/error feedback and refresh the catalog/balance after success.

Do not duplicate item notifications already sent by `PurchaseSealStoreItem`. The adapter should add only merchant acknowledgement/refresh behavior.

### 7.4 Client addition

The only custom MerchantFrame behavior is the specialization selector and message/refresh glue. The item list remains Blizzard's MerchantFrame. Deliver it through the existing EPF/Content Manager FrameXML pipeline, not a standalone addon, without changing FrameForge or Hunt Record artwork/geometry.

## 8. Exact integration points

### AzerothCore APIs used unchanged

- `WorldPackets::Item::ListInventory`: native list packet.
- `PlayerScript::OnPlayerBeforeBuyItemFromVendor`: native buy interception.
- `Player::GetSpec`, `GetTalentTabPages`, and `getClass`: spec initialization/validation.
- `ObjectMgr::GetItemTemplate`: row validation/display data.
- `sItemExtendedCostStore`: resolved extended-cost validation.
- Stock creature/vendor interaction helpers: mirror before accepting a custom buy.

### Native Hunts functionality reused

- `NativeHuntsManager::BuildSealStoreItems`.
- `NativeHuntsManager::PurchaseSealStoreItem`.
- Existing Huntmaster `CreatureScript` and gossip fallback.
- Existing Native Hunts `PlayerScript` and session lifecycle events.
- Existing `NHUNTS` client/server channel.
- Existing managed Seal symbol and EPF package name.

### New module-only components

- Per-player vendor session/revision store.
- Native inventory packet adapter.
- Narrow pre-purchase adapter to the existing backend.
- Managed extended-cost entries for the four existing Seal tiers, if absent.
- Merchant acknowledgement/error and post-purchase refresh.
- Client specialization dropdown glue in the existing package.

## 9. If strict stock purchasing is required

If Huntmaster purchases must traverse the unmodified validation and `_StoreOrEquipNewItem` portion of `Player::BuyItemFromVendorSlot`, current hooks are insufficient.

The smallest coherent core extension is a player-aware vendor inventory resolver called from **both** `WorldSession::SendListInventory` and `Player::BuyItemFromVendorSlot`. It must return a stable catalog whose lifetime spans asynchronous interaction, or a session-owned snapshot keyed by player/vendor. Both call sites must see identical row order/revision. A list-only hook is unsafe because purchase validation would still read the global cache.

This is not recommended solely to obtain a native window: it replaces more of the working backend, adds a core maintenance patch, and inherits core transaction semantics.

## 10. Risks and limitations

1. **Slot normalization:** rows are one-based on wire; the purchase hook receives zero-based slots.
2. **No revision in buy packets:** exact GUID, slot, item, live session, and full revalidation are mandatory.
3. **Hook ordering:** custom code must reproduce security-relevant interaction checks before stopping core.
4. **Ordinary vendors:** the global hook must no-op unless an exact Native Hunts session matches.
5. **150-row limit:** define truncation/catalog policy if the eligible set exceeds it.
6. **Quantity width:** require exactly one before purchase processing.
7. **Refresh ordering:** replace server rows before sending the corresponding packet; fail stale requests closed.
8. **Session cleanup:** rely on replacement, logout, validation, and timeout—not a close event.
9. **Single currency authority:** extended costs may describe display price, but only `PurchaseSealStoreItem` deducts.
10. **Feedback parity:** bypassing `_StoreOrEquipNewItem` requires deliberate merchant acknowledgement/error and refresh.
11. **Refunds:** stock extended-cost refund metadata is not automatic on the custom backend.
12. **Client data:** every item and extended-cost record must exist in the patched client.
13. **Packet maintenance:** use AzerothCore packet types rather than hand-encoded bytes and add focused tests later.
14. **Vendor flag:** preserve gossip while adding vendor capability in the eventual implementation.
15. **Atomicity:** retain current backend ordering; never split deduction between adapter and manager.

## 11. Future verification targets

These are acceptance checks for implementation, not work performed by this audit:

- Different players receive different lists from one Huntmaster without cross-contamination.
- Any of the class's three trees refreshes MerchantFrame without changing gameplay spec.
- Stale slot/item pairs after refresh cause no delivery or Seal loss.
- Forged item, slot, vendor, quantity, or talent tab is rejected.
- Insufficient Seals/full inventory cause no deduction.
- Success deducts once, delivers once, updates Hunt Record state, and refreshes the merchant.
- Ordinary vendors still use stock behavior.
- Gossip fallback remains available.
- No list exceeds 150 rows.
- Hunt Record artwork, layout, visibility, and existing Native Hunts geometry remain unchanged.

## Final determination

The native 3.3.5a merchant interface is feasible with the current AzerothCore and Playerbot fork. Existing hooks are sufficient for a **module-owned dynamic inventory and purchase adapter**, which preserves the working Native Hunts catalog and transaction backend.

Existing hooks are not sufficient to feed player-specific rows into stock `VendorItemData`. A core extension is required only if the project insists that stock `Player::BuyItemFromVendorSlot` validate and complete Huntmaster purchases instead of delegating to `PurchaseSealStoreItem`.
