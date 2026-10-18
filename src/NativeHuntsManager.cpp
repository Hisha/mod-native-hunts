#include "NativeHuntsManager.h"
#include "SealCatalogPolicy.h"

#include "HuntCatalog.h"
#include "HuntGameplay.h"
#include "HuntRewards.h"
#include "HuntSnapshot.h"
#include "HuntUiProtocol.h"
#include "api/ContentResourceApiV1.h"

#include "Creature.h"
#include "CreatureAI.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Field.h"
#include "Formulas.h"
#include "GameObject.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "MailMgr.h"
#include "Map.h"
#include "MapCollisionData.h"
#include "MapMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "TemporarySummon.h"
#include "Transaction.h"
#include "World.h"
#include "WorldPacket.h"

#include "DetourNavMeshQuery.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace native_hunts {
namespace {
constexpr char Package[] = "mod-native-hunts";
constexpr char SealSymbol[] = "huntmaster-seal";
constexpr char RelicObjectiveKey[] = "shared-zone-relics-v2";

bool IsUuid(std::string const &value) {
	if (value.size() != 36)
		return false;
	for (std::size_t index = 0; index < value.size(); ++index) {
		if (index == 8 || index == 13 || index == 18 || index == 23) {
			if (value[index] != '-')
				return false;
		} else if (!std::isxdigit(static_cast<unsigned char>(value[index])))
			return false;
	}
	return true;
}

std::optional<std::string> NewHuntInstanceId() {
	QueryResult result = CharacterDatabase.Query("SELECT UUID()");
	if (!result)
		return std::nullopt;
	std::string value = result->Fetch()[0].Get<std::string>();
	return IsUuid(value) ? std::optional<std::string>(std::move(value))
		: std::nullopt;
}

ContentResourcesV1::Provider const *FindContentProvider(std::string &reason) {
	ContentResourcesV1::Provider const *provider = nullptr;
	for (auto const &script : ScriptRegistry<WorldScript>::ScriptPointerList) {
		auto const *candidate =
			dynamic_cast<ContentResourcesV1::Provider const *>(script.second);
		if (!candidate)
			continue;
		if (provider) {
			reason = "multiple ContentResourceApiV1 providers are registered";
			return nullptr;
		}
		provider = candidate;
	}
	if (!provider)
		reason = "Content Manager resource provider is not registered";
	return provider;
}

float FinalLevelScale(std::uint8_t level) {
	if (level < 20)
		return 0.50f;
	if (level < 40)
		return 0.667f;
	if (level < 60)
		return 0.750f;
	if (level < 70)
		return 0.833f;
	return 1.0f;
}

float AmbushLevelScale(std::uint8_t level) {
	if (level < 20)
		return 0.375f;
	if (level < 40)
		return 0.625f;
	if (level < 60)
		return 0.750f;
	if (level < 70)
		return 0.875f;
	return 1.0f;
}

Player *ConnectedPlayer(std::uint32_t guid) {
	return ObjectAccessor::FindConnectedPlayer(
		ObjectGuid::Create<HighGuid::Player>(guid));
}

bool CommitCharacterTransactionAndWait(
		CharacterDatabaseTransaction const &transaction) {
	try {
		auto callback = CharacterDatabase.AsyncCommitTransaction(transaction);
		if (!callback.m_future.valid())
			return false;
		return callback.m_future.get();
	} catch (...) {
		return false;
	}
}

std::optional<bool> AssignmentExists(std::uint32_t guid) {
	QueryResult result = CharacterDatabase.Query(
		"SELECT COUNT(*) FROM `native_hunt_assignment` WHERE "
		"`character_guid`={}",
		guid);
	if (!result)
		return std::nullopt;
	return result->Fetch()[0].Get<std::uint64_t>() != 0;
}

struct BundleStorageResult {
	InventoryResult Result = EQUIP_ERR_OK;
	std::uint32_t LimitedCategory = 0;
};

BundleStorageResult CanStoreRewardBundle(Player *player,
		std::uint32_t equipmentEntry, std::uint32_t sealEntry,
		std::uint32_t sealCount) {
	Item *probes[2] = {};
	int probeCount = 0;
	auto cleanup = [&]() {
		for (int index = 0; index < probeCount; ++index) {
			probes[index]->RemoveFromUpdateQueueOf(player);
			delete probes[index];
		}
	};

	probes[probeCount] = Item::CreateItem(equipmentEntry, 1, player);
	if (!probes[probeCount])
		return {EQUIP_ERR_ITEM_NOT_FOUND, 0};
	++probeCount;
	if (sealCount) {
		probes[probeCount] = Item::CreateItem(sealEntry, sealCount, player);
		if (!probes[probeCount]) {
			cleanup();
			return {EQUIP_ERR_ITEM_NOT_FOUND, 0};
		}
		++probeCount;
	}

	std::uint32_t limitedCategory = 0;
	InventoryResult const result = player->CanStoreItems(
		probes, probeCount, &limitedCategory);
	cleanup();
	return {result, limitedCategory};
}

char const *InventoryFailureName(InventoryResult result) {
	switch (ClassifyInventoryResult(static_cast<std::uint32_t>(result))) {
		case InventoryFailureKind::None: return "none";
		case InventoryFailureKind::BagSpace: return "bag-space";
		case InventoryFailureKind::UniqueOrLimit: return "unique-or-limit";
		case InventoryFailureKind::ItemValidation: return "item-validation";
		case InventoryFailureKind::Storage: return "storage";
	}
	return "storage";
}

void RemoveProvisionalRewards(Player *player, ManagedResources const &resources,
		ProvisionalTurnIn const &provisional) {
	if (provisional.SealCount)
		player->DestroyItemCount(resources.SealItemEntry,
			provisional.SealCount, true);
	if (!provisional.EquipmentGuid.IsEmpty()) {
		if (Item *item = player->GetItemByGuid(provisional.EquipmentGuid))
			player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
	} else if (provisional.EquipmentEntry)
		player->DestroyItemCount(provisional.EquipmentEntry, 1, true);
	if (provisional.Money)
		player->ModifyMoney(-static_cast<std::int32_t>(provisional.Money));

	if (provisional.MailId) {
		if (Mail *mail = player->GetMail(provisional.MailId)) {
			player->RemoveMail(provisional.MailId);
			delete mail;
		}
		for (ObjectGuid const &itemGuid : provisional.MailItemGuids)
			if (Item *item = player->GetMItem(itemGuid.GetCounter())) {
				player->RemoveMItem(itemGuid.GetCounter());
				delete item;
			}
		sMailMgr->RecountMailCount(player->GetGUID().GetCounter());
		player->UpdateNextMailTimeAndUnreads();
	}
}

bool IsSpecCompatibleEquipment(uint32 spec, ItemTemplate const& item)
{
    bool const weapon = item.Class == ITEM_CLASS_WEAPON;
    bool const ranged = item.InventoryType == INVTYPE_RANGED || item.InventoryType == INVTYPE_RANGEDRIGHT ||
        item.InventoryType == INVTYPE_THROWN;
    bool const shield = item.InventoryType == INVTYPE_SHIELD;
    bool const holdable = item.InventoryType == INVTYPE_HOLDABLE;
    bool const offhandWeapon = item.InventoryType == INVTYPE_WEAPONOFFHAND;
    bool const relic = item.InventoryType == INVTYPE_RELIC;

    auto OneHand = [&]()
    {
        return (item.InventoryType == INVTYPE_WEAPON || item.InventoryType == INVTYPE_WEAPONMAINHAND ||
            item.InventoryType == INVTYPE_WEAPONOFFHAND) &&
            (item.SubClass == ITEM_SUBCLASS_WEAPON_AXE || item.SubClass == ITEM_SUBCLASS_WEAPON_MACE ||
             item.SubClass == ITEM_SUBCLASS_WEAPON_SWORD || item.SubClass == ITEM_SUBCLASS_WEAPON_DAGGER ||
             item.SubClass == ITEM_SUBCLASS_WEAPON_FIST);
    };
    auto PhysicalTwoHand = [&]()
    {
        return item.InventoryType == INVTYPE_2HWEAPON &&
            (item.SubClass == ITEM_SUBCLASS_WEAPON_AXE2 || item.SubClass == ITEM_SUBCLASS_WEAPON_MACE2 ||
             item.SubClass == ITEM_SUBCLASS_WEAPON_SWORD2 || item.SubClass == ITEM_SUBCLASS_WEAPON_POLEARM ||
             item.SubClass == ITEM_SUBCLASS_WEAPON_STAFF);
    };
    auto CasterWeapon = [&]()
    {
        return ((item.InventoryType == INVTYPE_WEAPON || item.InventoryType == INVTYPE_WEAPONMAINHAND) &&
            (item.SubClass == ITEM_SUBCLASS_WEAPON_MACE || item.SubClass == ITEM_SUBCLASS_WEAPON_SWORD ||
             item.SubClass == ITEM_SUBCLASS_WEAPON_DAGGER)) ||
            (item.InventoryType == INVTYPE_2HWEAPON && item.SubClass == ITEM_SUBCLASS_WEAPON_STAFF);
    };
    auto Wand = [&]() { return item.InventoryType == INVTYPE_RANGEDRIGHT && item.SubClass == ITEM_SUBCLASS_WEAPON_WAND; };
    auto PhysicalRanged = [&]()
    {
        return (item.InventoryType == INVTYPE_RANGED || item.InventoryType == INVTYPE_RANGEDRIGHT) &&
            (item.SubClass == ITEM_SUBCLASS_WEAPON_BOW || item.SubClass == ITEM_SUBCLASS_WEAPON_GUN ||
             item.SubClass == ITEM_SUBCLASS_WEAPON_CROSSBOW);
    };

    switch (spec)
    {
        case TALENT_TREE_WARRIOR_ARMS:
            if (relic || shield || holdable || offhandWeapon) return false;
            if (ranged) return PhysicalRanged() || item.InventoryType == INVTYPE_THROWN;
            if (weapon) return PhysicalTwoHand();
            break;
        case TALENT_TREE_WARRIOR_FURY:
            if (relic || shield || holdable) return false;
            if (ranged) return PhysicalRanged() || item.InventoryType == INVTYPE_THROWN;
            if (weapon) return OneHand() || PhysicalTwoHand();
            break;
        case TALENT_TREE_WARRIOR_PROTECTION:
            if (relic || holdable || item.InventoryType == INVTYPE_2HWEAPON) return false;
            if (ranged) return PhysicalRanged() || item.InventoryType == INVTYPE_THROWN;
            if (weapon) return OneHand();
            break;

        case TALENT_TREE_PALADIN_HOLY:
        case TALENT_TREE_PALADIN_PROTECTION:
        case TALENT_TREE_PALADIN_RETRIBUTION:
            if (relic) return item.Class == ITEM_CLASS_ARMOR && item.SubClass == ITEM_SUBCLASS_ARMOR_LIBRAM;
            if (ranged || holdable || offhandWeapon) return false;
            if (weapon)
            {
                if (spec == TALENT_TREE_PALADIN_RETRIBUTION)
                    return item.InventoryType == INVTYPE_2HWEAPON &&
                        (item.SubClass == ITEM_SUBCLASS_WEAPON_AXE2 || item.SubClass == ITEM_SUBCLASS_WEAPON_MACE2 ||
                         item.SubClass == ITEM_SUBCLASS_WEAPON_SWORD2 || item.SubClass == ITEM_SUBCLASS_WEAPON_POLEARM);
                return (item.InventoryType == INVTYPE_WEAPON || item.InventoryType == INVTYPE_WEAPONMAINHAND) &&
                    (item.SubClass == ITEM_SUBCLASS_WEAPON_AXE || item.SubClass == ITEM_SUBCLASS_WEAPON_MACE ||
                     item.SubClass == ITEM_SUBCLASS_WEAPON_SWORD);
            }
            if (spec == TALENT_TREE_PALADIN_RETRIBUTION && shield) return false;
            break;

        case TALENT_TREE_HUNTER_BEAST_MASTERY:
        case TALENT_TREE_HUNTER_MARKSMANSHIP:
        case TALENT_TREE_HUNTER_SURVIVAL:
            if (relic || shield || holdable) return false;
            if (ranged) return PhysicalRanged();
            if (weapon) return OneHand() || PhysicalTwoHand();
            break;

        case TALENT_TREE_ROGUE_ASSASSINATION:
        case TALENT_TREE_ROGUE_COMBAT:
        case TALENT_TREE_ROGUE_SUBTLETY:
            if (relic || shield || holdable) return false;
            if (ranged) return PhysicalRanged() || item.InventoryType == INVTYPE_THROWN;
            if (weapon)
            {
                if (!OneHand()) return false;
                if (spec == TALENT_TREE_ROGUE_ASSASSINATION && item.SubClass == ITEM_SUBCLASS_WEAPON_MACE) return false;
                return true;
            }
            break;

        case TALENT_TREE_PRIEST_DISCIPLINE:
        case TALENT_TREE_PRIEST_HOLY:
        case TALENT_TREE_PRIEST_SHADOW:
        case TALENT_TREE_MAGE_ARCANE:
        case TALENT_TREE_MAGE_FIRE:
        case TALENT_TREE_MAGE_FROST:
        case TALENT_TREE_WARLOCK_AFFLICTION:
        case TALENT_TREE_WARLOCK_DEMONOLOGY:
        case TALENT_TREE_WARLOCK_DESTRUCTION:
            if (relic || shield || offhandWeapon) return false;
            if (ranged) return Wand();
            if (weapon) return CasterWeapon();
            break;

        case TALENT_TREE_DEATH_KNIGHT_BLOOD:
        case TALENT_TREE_DEATH_KNIGHT_FROST:
        case TALENT_TREE_DEATH_KNIGHT_UNHOLY:
            if (relic) return item.Class == ITEM_CLASS_ARMOR && item.SubClass == ITEM_SUBCLASS_ARMOR_SIGIL;
            if (ranged || shield || holdable) return false;
            if (weapon)
                return (OneHand() || PhysicalTwoHand()) &&
                    item.SubClass != ITEM_SUBCLASS_WEAPON_DAGGER && item.SubClass != ITEM_SUBCLASS_WEAPON_FIST &&
                    item.SubClass != ITEM_SUBCLASS_WEAPON_POLEARM && item.SubClass != ITEM_SUBCLASS_WEAPON_STAFF;
            break;

        case TALENT_TREE_SHAMAN_ELEMENTAL:
        case TALENT_TREE_SHAMAN_RESTORATION:
            if (relic) return item.Class == ITEM_CLASS_ARMOR && item.SubClass == ITEM_SUBCLASS_ARMOR_TOTEM;
            if (ranged || holdable || offhandWeapon) return false;
            if (weapon)
                return (item.InventoryType == INVTYPE_WEAPON || item.InventoryType == INVTYPE_WEAPONMAINHAND) &&
                    (item.SubClass == ITEM_SUBCLASS_WEAPON_AXE || item.SubClass == ITEM_SUBCLASS_WEAPON_MACE ||
                     item.SubClass == ITEM_SUBCLASS_WEAPON_DAGGER);
            break;
        case TALENT_TREE_SHAMAN_ENHANCEMENT:
            if (relic) return item.Class == ITEM_CLASS_ARMOR && item.SubClass == ITEM_SUBCLASS_ARMOR_TOTEM;
            if (ranged || holdable) return false;
            if (weapon)
                return OneHand() && (item.SubClass == ITEM_SUBCLASS_WEAPON_AXE ||
                    item.SubClass == ITEM_SUBCLASS_WEAPON_MACE || item.SubClass == ITEM_SUBCLASS_WEAPON_FIST);
            break;

        case TALENT_TREE_DRUID_BALANCE:
        case TALENT_TREE_DRUID_RESTORATION:
            if (relic) return item.Class == ITEM_CLASS_ARMOR && item.SubClass == ITEM_SUBCLASS_ARMOR_IDOL;
            if (ranged || shield || offhandWeapon) return false;
            if (weapon)
                return ((item.InventoryType == INVTYPE_WEAPON || item.InventoryType == INVTYPE_WEAPONMAINHAND) &&
                    (item.SubClass == ITEM_SUBCLASS_WEAPON_MACE || item.SubClass == ITEM_SUBCLASS_WEAPON_DAGGER)) ||
                    (item.InventoryType == INVTYPE_2HWEAPON && item.SubClass == ITEM_SUBCLASS_WEAPON_STAFF);
            break;
        case TALENT_TREE_DRUID_FERAL_COMBAT:
            if (relic) return item.Class == ITEM_CLASS_ARMOR && item.SubClass == ITEM_SUBCLASS_ARMOR_IDOL;
            if (ranged || shield || holdable || offhandWeapon) return false;
            if (weapon)
                return item.InventoryType == INVTYPE_2HWEAPON &&
                    (item.SubClass == ITEM_SUBCLASS_WEAPON_MACE2 || item.SubClass == ITEM_SUBCLASS_WEAPON_STAFF ||
                     item.SubClass == ITEM_SUBCLASS_WEAPON_POLEARM);
            break;
        default:
            break;
    }

    return true;
}

bool IsSpecForClass(Player const *player, std::uint32_t spec) {
	if (!player)
		return false;
	switch (player->getClass()) {
	case CLASS_WARRIOR:
		return spec == TALENT_TREE_WARRIOR_ARMS || spec == TALENT_TREE_WARRIOR_FURY || spec == TALENT_TREE_WARRIOR_PROTECTION;
	case CLASS_PALADIN:
		return spec == TALENT_TREE_PALADIN_HOLY || spec == TALENT_TREE_PALADIN_PROTECTION || spec == TALENT_TREE_PALADIN_RETRIBUTION;
	case CLASS_HUNTER:
		return spec == TALENT_TREE_HUNTER_BEAST_MASTERY || spec == TALENT_TREE_HUNTER_MARKSMANSHIP || spec == TALENT_TREE_HUNTER_SURVIVAL;
	case CLASS_ROGUE:
		return spec == TALENT_TREE_ROGUE_ASSASSINATION || spec == TALENT_TREE_ROGUE_COMBAT || spec == TALENT_TREE_ROGUE_SUBTLETY;
	case CLASS_PRIEST:
		return spec == TALENT_TREE_PRIEST_DISCIPLINE || spec == TALENT_TREE_PRIEST_HOLY || spec == TALENT_TREE_PRIEST_SHADOW;
	case CLASS_DEATH_KNIGHT:
		return spec == TALENT_TREE_DEATH_KNIGHT_BLOOD || spec == TALENT_TREE_DEATH_KNIGHT_FROST || spec == TALENT_TREE_DEATH_KNIGHT_UNHOLY;
	case CLASS_SHAMAN:
		return spec == TALENT_TREE_SHAMAN_ELEMENTAL || spec == TALENT_TREE_SHAMAN_ENHANCEMENT || spec == TALENT_TREE_SHAMAN_RESTORATION;
	case CLASS_MAGE:
		return spec == TALENT_TREE_MAGE_ARCANE || spec == TALENT_TREE_MAGE_FIRE || spec == TALENT_TREE_MAGE_FROST;
	case CLASS_WARLOCK:
		return spec == TALENT_TREE_WARLOCK_AFFLICTION || spec == TALENT_TREE_WARLOCK_DEMONOLOGY || spec == TALENT_TREE_WARLOCK_DESTRUCTION;
	case CLASS_DRUID:
		return spec == TALENT_TREE_DRUID_BALANCE || spec == TALENT_TREE_DRUID_FERAL_COMBAT || spec == TALENT_TREE_DRUID_RESTORATION;
	default:
		return false;
	}
}

bool MatchesSealStoreSlot(ItemTemplate const &item, SealStoreSlot slot) {
	switch (slot) {
	case SealStoreSlot::Weapon:
		return item.Class == ITEM_CLASS_WEAPON && item.InventoryType != INVTYPE_WEAPONOFFHAND &&
			item.InventoryType != INVTYPE_RANGED && item.InventoryType != INVTYPE_RANGEDRIGHT &&
			item.InventoryType != INVTYPE_THROWN && item.InventoryType != INVTYPE_RELIC;
	case SealStoreSlot::Head: return item.InventoryType == INVTYPE_HEAD;
	case SealStoreSlot::Neck: return item.InventoryType == INVTYPE_NECK;
	case SealStoreSlot::Shoulder: return item.InventoryType == INVTYPE_SHOULDERS;
	case SealStoreSlot::Back: return item.InventoryType == INVTYPE_CLOAK;
	case SealStoreSlot::Chest: return item.InventoryType == INVTYPE_CHEST || item.InventoryType == INVTYPE_ROBE;
	case SealStoreSlot::Wrist: return item.InventoryType == INVTYPE_WRISTS;
	case SealStoreSlot::Hands: return item.InventoryType == INVTYPE_HANDS;
	case SealStoreSlot::Waist: return item.InventoryType == INVTYPE_WAIST;
	case SealStoreSlot::Legs: return item.InventoryType == INVTYPE_LEGS;
	case SealStoreSlot::Feet: return item.InventoryType == INVTYPE_FEET;
	case SealStoreSlot::Ring: return item.InventoryType == INVTYPE_FINGER;
	case SealStoreSlot::Trinket: return item.InventoryType == INVTYPE_TRINKET;
	case SealStoreSlot::OffHand:
		return item.InventoryType == INVTYPE_SHIELD || item.InventoryType == INVTYPE_HOLDABLE ||
			item.InventoryType == INVTYPE_WEAPONOFFHAND;
	case SealStoreSlot::Relic:
		return item.InventoryType == INVTYPE_RELIC || item.InventoryType == INVTYPE_RANGED ||
			item.InventoryType == INVTYPE_RANGEDRIGHT || item.InventoryType == INVTYPE_THROWN;
	}
	return false;
}


char const *StateName(HuntState state) {
	switch (state) {
	case HuntState::Idle: return "Idle";
	case HuntState::Tracking: return "Tracking";
	case HuntState::FinalRevealed: return "FinalRevealed";
	case HuntState::PreyActive: return "PreyActive";
	case HuntState::ReadyToTurnIn: return "ReadyToTurnIn";
	}
	return "Unknown";
}

using RewardRole = native_hunts::SealCatalogRole;

RewardRole GetRewardRole(Player* player, uint32 spec)
{
    switch (spec)
    {
        case TALENT_TREE_WARRIOR_PROTECTION:
        case TALENT_TREE_PALADIN_PROTECTION:
        case TALENT_TREE_DEATH_KNIGHT_BLOOD:
            return RewardRole::Tank;

        case TALENT_TREE_WARRIOR_ARMS:
        case TALENT_TREE_WARRIOR_FURY:
        case TALENT_TREE_PALADIN_RETRIBUTION:
        case TALENT_TREE_DEATH_KNIGHT_FROST:
        case TALENT_TREE_DEATH_KNIGHT_UNHOLY:
            return RewardRole::StrengthMelee;

        case TALENT_TREE_ROGUE_ASSASSINATION:
        case TALENT_TREE_ROGUE_COMBAT:
        case TALENT_TREE_ROGUE_SUBTLETY:
        case TALENT_TREE_SHAMAN_ENHANCEMENT:
        case TALENT_TREE_DRUID_FERAL_COMBAT:
            return RewardRole::AgilityMelee;

        case TALENT_TREE_HUNTER_BEAST_MASTERY:
        case TALENT_TREE_HUNTER_MARKSMANSHIP:
        case TALENT_TREE_HUNTER_SURVIVAL:
            return RewardRole::HunterRanged;

        case TALENT_TREE_PALADIN_HOLY:
        case TALENT_TREE_PRIEST_DISCIPLINE:
        case TALENT_TREE_PRIEST_HOLY:
        case TALENT_TREE_SHAMAN_RESTORATION:
        case TALENT_TREE_DRUID_RESTORATION:
            return RewardRole::Healer;

        case TALENT_TREE_PRIEST_SHADOW:
        case TALENT_TREE_SHAMAN_ELEMENTAL:
        case TALENT_TREE_MAGE_ARCANE:
        case TALENT_TREE_MAGE_FIRE:
        case TALENT_TREE_MAGE_FROST:
        case TALENT_TREE_WARLOCK_AFFLICTION:
        case TALENT_TREE_WARLOCK_DEMONOLOGY:
        case TALENT_TREE_WARLOCK_DESTRUCTION:
        case TALENT_TREE_DRUID_BALANCE:
            return RewardRole::SpellDamage;
        default:
            break;
    }

    // Characters with too few talent points to establish a tree still get a
    // class-appropriate baseline instead of fully random equipment.
    switch (player->getClass())
    {
        case CLASS_ROGUE: return RewardRole::AgilityMelee;
        case CLASS_HUNTER: return RewardRole::HunterRanged;
        case CLASS_MAGE:
        case CLASS_WARLOCK:
        case CLASS_PRIEST: return RewardRole::SpellDamage;
        case CLASS_WARRIOR:
        case CLASS_PALADIN:
        case CLASS_DEATH_KNIGHT: return RewardRole::StrengthMelee;
        case CLASS_SHAMAN:
        case CLASS_DRUID: return RewardRole::Generic;
        default: return RewardRole::Generic;
    }
}

float GetRewardStatWeight(RewardRole role, uint32 stat)
{
    switch (role)
    {
        case RewardRole::StrengthMelee:
            switch (stat)
            {
                case ITEM_MOD_STRENGTH: return 1.00f;
                case ITEM_MOD_ATTACK_POWER: return 0.50f;
                case ITEM_MOD_CRIT_RATING:
                case ITEM_MOD_CRIT_MELEE_RATING: return 0.70f;
                case ITEM_MOD_HIT_RATING:
                case ITEM_MOD_HIT_MELEE_RATING: return 0.75f;
                case ITEM_MOD_EXPERTISE_RATING: return 0.75f;
                case ITEM_MOD_HASTE_RATING:
                case ITEM_MOD_HASTE_MELEE_RATING: return 0.55f;
                case ITEM_MOD_ARMOR_PENETRATION_RATING: return 0.55f;
                case ITEM_MOD_STAMINA: return 0.20f;
                default: return 0.0f;
            }
        case RewardRole::AgilityMelee:
            switch (stat)
            {
                case ITEM_MOD_AGILITY: return 1.00f;
                case ITEM_MOD_ATTACK_POWER: return 0.50f;
                case ITEM_MOD_CRIT_RATING:
                case ITEM_MOD_CRIT_MELEE_RATING: return 0.75f;
                case ITEM_MOD_HIT_RATING:
                case ITEM_MOD_HIT_MELEE_RATING: return 0.75f;
                case ITEM_MOD_EXPERTISE_RATING: return 0.70f;
                case ITEM_MOD_HASTE_RATING:
                case ITEM_MOD_HASTE_MELEE_RATING: return 0.55f;
                case ITEM_MOD_ARMOR_PENETRATION_RATING: return 0.50f;
                case ITEM_MOD_STAMINA: return 0.20f;
                default: return 0.0f;
            }
        case RewardRole::HunterRanged:
            switch (stat)
            {
                case ITEM_MOD_AGILITY: return 1.00f;
                case ITEM_MOD_ATTACK_POWER:
                case ITEM_MOD_RANGED_ATTACK_POWER: return 0.50f;
                case ITEM_MOD_CRIT_RATING:
                case ITEM_MOD_CRIT_RANGED_RATING: return 0.75f;
                case ITEM_MOD_HIT_RATING:
                case ITEM_MOD_HIT_RANGED_RATING: return 0.75f;
                case ITEM_MOD_HASTE_RATING:
                case ITEM_MOD_HASTE_RANGED_RATING: return 0.55f;
                case ITEM_MOD_INTELLECT: return 0.25f;
                case ITEM_MOD_STAMINA: return 0.15f;
                default: return 0.0f;
            }
        case RewardRole::SpellDamage:
            switch (stat)
            {
                case ITEM_MOD_SPELL_POWER: return 1.00f;
                case ITEM_MOD_INTELLECT: return 0.75f;
                case ITEM_MOD_HIT_RATING:
                case ITEM_MOD_HIT_SPELL_RATING: return 0.70f;
                case ITEM_MOD_CRIT_RATING:
                case ITEM_MOD_CRIT_SPELL_RATING: return 0.65f;
                case ITEM_MOD_HASTE_RATING:
                case ITEM_MOD_HASTE_SPELL_RATING: return 0.70f;
                case ITEM_MOD_SPIRIT: return 0.30f;
                case ITEM_MOD_MANA_REGENERATION: return 0.25f;
                case ITEM_MOD_STAMINA: return 0.10f;
                default: return 0.0f;
            }
        case RewardRole::Healer:
            switch (stat)
            {
                case ITEM_MOD_SPELL_POWER: return 1.00f;
                case ITEM_MOD_INTELLECT: return 0.85f;
                case ITEM_MOD_MANA_REGENERATION: return 0.80f;
                case ITEM_MOD_HASTE_RATING:
                case ITEM_MOD_HASTE_SPELL_RATING: return 0.70f;
                case ITEM_MOD_CRIT_RATING:
                case ITEM_MOD_CRIT_SPELL_RATING: return 0.50f;
                case ITEM_MOD_SPIRIT: return 0.45f;
                case ITEM_MOD_STAMINA: return 0.10f;
                default: return 0.0f;
            }
        case RewardRole::Tank:
            switch (stat)
            {
                case ITEM_MOD_STAMINA: return 1.00f;
                case ITEM_MOD_DEFENSE_SKILL_RATING: return 0.90f;
                case ITEM_MOD_DODGE_RATING:
                case ITEM_MOD_PARRY_RATING:
                case ITEM_MOD_BLOCK_RATING: return 0.75f;
                case ITEM_MOD_BLOCK_VALUE: return 0.65f;
                case ITEM_MOD_STRENGTH: return 0.55f;
                case ITEM_MOD_HIT_RATING:
                case ITEM_MOD_HIT_MELEE_RATING:
                case ITEM_MOD_EXPERTISE_RATING: return 0.35f;
                default: return 0.0f;
            }
        default:
            return stat == ITEM_MOD_STAMINA ? 0.15f : 0.0f;
    }
}

float GetArmorPreference(Player* player, ItemTemplate const& item)
{
    if (item.Class != ITEM_CLASS_ARMOR)
        return 0.0f;

    // Cloaks, rings, trinkets, necklaces and relics are not governed by armor
    // material preference. CanUseItem() still validates their real restrictions.
    if (item.InventoryType == INVTYPE_CLOAK || item.InventoryType == INVTYPE_NECK ||
        item.InventoryType == INVTYPE_FINGER || item.InventoryType == INVTYPE_TRINKET ||
        item.InventoryType == INVTYPE_RELIC || item.InventoryType == INVTYPE_SHIELD ||
        item.InventoryType == INVTYPE_HOLDABLE)
        return 12.0f;

    uint32 wantedSubclass = ITEM_SUBCLASS_ARMOR_CLOTH;
    switch (player->getClass())
    {
        case CLASS_ROGUE:
        case CLASS_DRUID:
            wantedSubclass = ITEM_SUBCLASS_ARMOR_LEATHER;
            break;
        case CLASS_HUNTER:
        case CLASS_SHAMAN:
            wantedSubclass = player->GetLevel() >= 40 ? ITEM_SUBCLASS_ARMOR_MAIL : ITEM_SUBCLASS_ARMOR_LEATHER;
            break;
        case CLASS_WARRIOR:
        case CLASS_PALADIN:
            wantedSubclass = player->GetLevel() >= 40 ? ITEM_SUBCLASS_ARMOR_PLATE : ITEM_SUBCLASS_ARMOR_MAIL;
            break;
        case CLASS_DEATH_KNIGHT:
            wantedSubclass = ITEM_SUBCLASS_ARMOR_PLATE;
            break;
        default:
            wantedSubclass = ITEM_SUBCLASS_ARMOR_CLOTH;
            break;
    }

    if (item.SubClass == wantedSubclass)
        return 30.0f;

    // It may be technically equipable (for example, cloth on a rogue), but it
    // should almost never beat gear of the class's intended armor type.
    return -30.0f;
}

float GetWeaponPreference(Player* player, uint32 spec, ItemTemplate const& item)
{
    if (item.Class != ITEM_CLASS_WEAPON)
        return 0.0f;

    float score = 0.0f;
    switch (spec)
    {
        case TALENT_TREE_ROGUE_ASSASSINATION:
            score += item.SubClass == ITEM_SUBCLASS_WEAPON_DAGGER ? 45.0f : -20.0f;
            break;
        case TALENT_TREE_ROGUE_SUBTLETY:
            score += item.SubClass == ITEM_SUBCLASS_WEAPON_DAGGER ? 35.0f : 0.0f;
            break;
        case TALENT_TREE_ROGUE_COMBAT:
            if (item.SubClass == ITEM_SUBCLASS_WEAPON_SWORD || item.SubClass == ITEM_SUBCLASS_WEAPON_AXE ||
                item.SubClass == ITEM_SUBCLASS_WEAPON_MACE || item.SubClass == ITEM_SUBCLASS_WEAPON_FIST ||
                item.SubClass == ITEM_SUBCLASS_WEAPON_DAGGER)
                score += 30.0f;
            break;
        case TALENT_TREE_SHAMAN_ENHANCEMENT:
            if (item.SubClass == ITEM_SUBCLASS_WEAPON_AXE || item.SubClass == ITEM_SUBCLASS_WEAPON_MACE ||
                item.SubClass == ITEM_SUBCLASS_WEAPON_FIST)
                score += 35.0f;
            else
                score -= 15.0f;
            break;
        case TALENT_TREE_WARRIOR_ARMS:
        case TALENT_TREE_PALADIN_RETRIBUTION:
        case TALENT_TREE_DEATH_KNIGHT_UNHOLY:
            score += item.InventoryType == INVTYPE_2HWEAPON ? 35.0f : 0.0f;
            break;
        case TALENT_TREE_WARRIOR_PROTECTION:
        case TALENT_TREE_PALADIN_PROTECTION:
            score += (item.InventoryType == INVTYPE_WEAPON || item.InventoryType == INVTYPE_WEAPONMAINHAND) ? 30.0f : -15.0f;
            break;
        case TALENT_TREE_HUNTER_BEAST_MASTERY:
        case TALENT_TREE_HUNTER_MARKSMANSHIP:
        case TALENT_TREE_HUNTER_SURVIVAL:
            if (item.SubClass == ITEM_SUBCLASS_WEAPON_BOW || item.SubClass == ITEM_SUBCLASS_WEAPON_GUN ||
                item.SubClass == ITEM_SUBCLASS_WEAPON_CROSSBOW)
                score += 45.0f;
            break;
        case TALENT_TREE_PRIEST_DISCIPLINE:
        case TALENT_TREE_PRIEST_HOLY:
        case TALENT_TREE_PRIEST_SHADOW:
        case TALENT_TREE_SHAMAN_ELEMENTAL:
        case TALENT_TREE_SHAMAN_RESTORATION:
        case TALENT_TREE_MAGE_ARCANE:
        case TALENT_TREE_MAGE_FIRE:
        case TALENT_TREE_MAGE_FROST:
        case TALENT_TREE_WARLOCK_AFFLICTION:
        case TALENT_TREE_WARLOCK_DEMONOLOGY:
        case TALENT_TREE_WARLOCK_DESTRUCTION:
        case TALENT_TREE_DRUID_BALANCE:
        case TALENT_TREE_DRUID_RESTORATION:
            if (item.SubClass == ITEM_SUBCLASS_WEAPON_STAFF || item.SubClass == ITEM_SUBCLASS_WEAPON_DAGGER ||
                item.SubClass == ITEM_SUBCLASS_WEAPON_MACE || item.SubClass == ITEM_SUBCLASS_WEAPON_SWORD ||
                item.SubClass == ITEM_SUBCLASS_WEAPON_WAND)
                score += 25.0f;
            break;
        default:
            break;
    }

    return score;
}

bool IsRewardIdentityStat(RewardRole role, uint32 stat)
{
    switch (role)
    {
        case RewardRole::StrengthMelee:
            return stat == ITEM_MOD_STRENGTH || stat == ITEM_MOD_ATTACK_POWER;
        case RewardRole::AgilityMelee:
            return stat == ITEM_MOD_AGILITY || stat == ITEM_MOD_ATTACK_POWER;
        case RewardRole::HunterRanged:
            return stat == ITEM_MOD_AGILITY || stat == ITEM_MOD_ATTACK_POWER || stat == ITEM_MOD_RANGED_ATTACK_POWER;
        case RewardRole::SpellDamage:
            return stat == ITEM_MOD_SPELL_POWER || stat == ITEM_MOD_INTELLECT;
        case RewardRole::Healer:
            return stat == ITEM_MOD_SPELL_POWER || stat == ITEM_MOD_INTELLECT ||
                stat == ITEM_MOD_MANA_REGENERATION || stat == ITEM_MOD_SPIRIT;
        case RewardRole::Tank:
            return stat == ITEM_MOD_STAMINA || stat == ITEM_MOD_DEFENSE_SKILL_RATING ||
                stat == ITEM_MOD_DODGE_RATING || stat == ITEM_MOD_PARRY_RATING ||
                stat == ITEM_MOD_BLOCK_RATING || stat == ITEM_MOD_BLOCK_VALUE;
        default:
            return true;
    }
}

bool MatchesSealCatalogRole(RewardRole role, ItemTemplate const &item)
{
    native_hunts::SealCatalogTraits traits;
	traits.Relic = item.InventoryType == INVTYPE_RELIC;
    for (uint32 i = 0; i < item.StatsCount && i < MAX_ITEM_PROTO_STATS; ++i)
    {
        if (item.ItemStat[i].ItemStatValue <= 0)
            continue;
        switch (item.ItemStat[i].ItemStatType)
        {
            case ITEM_MOD_STRENGTH: traits.Strength = true; break;
            case ITEM_MOD_AGILITY: traits.Agility = true; break;
            case ITEM_MOD_ATTACK_POWER: traits.AttackPower = true; break;
            case ITEM_MOD_RANGED_ATTACK_POWER: traits.RangedAttackPower = true; break;
            case ITEM_MOD_SPELL_POWER: traits.SpellPower = true; break;
            case ITEM_MOD_INTELLECT: traits.Intellect = true; break;
            case ITEM_MOD_MANA_REGENERATION: traits.ManaRegeneration = true; break;
            case ITEM_MOD_SPIRIT: traits.Spirit = true; break;
            case ITEM_MOD_DEFENSE_SKILL_RATING: traits.Defense = true; break;
            case ITEM_MOD_DODGE_RATING: traits.Dodge = true; break;
            case ITEM_MOD_PARRY_RATING: traits.Parry = true; break;
            case ITEM_MOD_BLOCK_RATING: traits.BlockRating = true; break;
            case ITEM_MOD_BLOCK_VALUE: traits.BlockValue = true; break;
            default: break;
        }
    }
    return native_hunts::MatchesSealCatalogRole(role, traits);
}

float GetRewardWrongDirectionPenalty(RewardRole role, uint32 stat, int32 value)
{
    if (value <= 0)
        return 0.0f;

    float amount = static_cast<float>(value);
    switch (role)
    {
        case RewardRole::StrengthMelee:
            if (stat == ITEM_MOD_INTELLECT || stat == ITEM_MOD_SPIRIT || stat == ITEM_MOD_SPELL_POWER ||
                stat == ITEM_MOD_MANA_REGENERATION)
                return -std::min(20.0f, amount * 0.75f);
            break;
        case RewardRole::AgilityMelee:
        case RewardRole::HunterRanged:
            if (stat == ITEM_MOD_STRENGTH || stat == ITEM_MOD_SPIRIT || stat == ITEM_MOD_SPELL_POWER ||
                stat == ITEM_MOD_MANA_REGENERATION)
                return -std::min(20.0f, amount * 0.75f);
            break;
        case RewardRole::SpellDamage:
        case RewardRole::Healer:
            if (stat == ITEM_MOD_STRENGTH || stat == ITEM_MOD_AGILITY || stat == ITEM_MOD_ATTACK_POWER ||
                stat == ITEM_MOD_RANGED_ATTACK_POWER)
                return -std::min(25.0f, amount * 0.85f);
            break;
        case RewardRole::Tank:
            if (stat == ITEM_MOD_SPIRIT || stat == ITEM_MOD_SPELL_POWER || stat == ITEM_MOD_MANA_REGENERATION)
                return -std::min(15.0f, amount * 0.60f);
            break;
        default:
            break;
    }

    return 0.0f;
}

float ScoreRewardItem(Player* player, uint32 spec, RewardRole role, ItemTemplate const& item)
{
    float score = 0.0f;

    // Being near the hunter's level matters, but it is intentionally weaker
    // than spec/stat suitability. A useful level-12 item beats a nonsense
    // level-15 item for a level-15 hunter.
    int32 levelGap = static_cast<int32>(player->GetLevel()) - static_cast<int32>(item.RequiredLevel);
    score += std::max(0.0f, 15.0f - static_cast<float>(std::max(0, levelGap)) * 2.0f);
    score += GetArmorPreference(player, item);
    score += GetWeaponPreference(player, spec, item);

    bool hasIdentityStat = role == RewardRole::Generic;
    bool hasAnyPositiveStat = false;
    for (uint32 i = 0; i < item.StatsCount && i < MAX_ITEM_PROTO_STATS; ++i)
    {
        int32 value = item.ItemStat[i].ItemStatValue;
        if (value <= 0)
            continue;

        hasAnyPositiveStat = true;
        uint32 stat = item.ItemStat[i].ItemStatType;
        if (IsRewardIdentityStat(role, stat))
            hasIdentityStat = true;

        score += GetRewardStatWeight(role, stat) * static_cast<float>(value);
        score += GetRewardWrongDirectionPenalty(role, stat, value);
    }

    // 0.6.2: secondary stats may improve a good item, but cannot define the
    // item's role by themselves. This prevents crit-only/stamina-only pieces
    // from outranking true caster, melee, healer, or tank gear simply because
    // one supporting stat happens to be desirable. Weapons with no explicit
    // stats still rely on their strong spec-specific weapon preference.
    if (role != RewardRole::Generic)
    {
        if (hasIdentityStat)
            score += 24.0f;
        else if (hasAnyPositiveStat)
            score -= 28.0f;
        else if (item.Class != ITEM_CLASS_WEAPON)
            score -= 18.0f;
    }

    return score;
}

float ScoreRewardPower(Player* player, uint32 spec, RewardRole role, ItemTemplate const& item)
{
    // Item level is deliberately part of the comparison against equipped gear.
    // The spec score keeps itemization important while the ilvl term prevents a
    // low-tier Hunt reward from replacing an obviously stronger raid item.
    return ScoreRewardItem(player, spec, role, item) + static_cast<float>(item.ItemLevel) * 1.5f;
}

float GetEquippedItemPower(Player* player, uint32 spec, RewardRole role, uint8 equipmentSlot)
{
    if (!player)
        return 0.0f;

    Item* equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, equipmentSlot);
    if (!equipped || !equipped->GetTemplate())
        return 0.0f;

    return ScoreRewardPower(player, spec, role, *equipped->GetTemplate());
}

float GetEquippedPowerForCandidate(Player* player, uint32 spec, RewardRole role, ItemTemplate const& candidate)
{
    switch (candidate.InventoryType)
    {
        case INVTYPE_HEAD: return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_HEAD);
        case INVTYPE_NECK: return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_NECK);
        case INVTYPE_SHOULDERS: return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_SHOULDERS);
        case INVTYPE_CLOAK: return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_BACK);
        case INVTYPE_CHEST:
        case INVTYPE_ROBE: return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_CHEST);
        case INVTYPE_WRISTS: return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_WRISTS);
        case INVTYPE_HANDS: return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_HANDS);
        case INVTYPE_WAIST: return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_WAIST);
        case INVTYPE_LEGS: return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_LEGS);
        case INVTYPE_FEET: return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_FEET);
        case INVTYPE_FINGER:
            return std::min(GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_FINGER1),
                GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_FINGER2));
        case INVTYPE_TRINKET:
            return std::min(GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_TRINKET1),
                GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_TRINKET2));
        case INVTYPE_SHIELD:
        case INVTYPE_HOLDABLE:
        case INVTYPE_WEAPONOFFHAND:
            return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_OFFHAND);
        case INVTYPE_RANGED:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_THROWN:
        case INVTYPE_RELIC:
            return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_RANGED);
        case INVTYPE_WEAPON:
        {
            float mainPower = GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_MAINHAND);
            float offPower = GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_OFFHAND);
            return player && player->CanDualWield() && offPower > 0.0f ?
				std::min(mainPower, offPower) : mainPower;
        }
        case INVTYPE_WEAPONMAINHAND:
        case INVTYPE_2HWEAPON:
            return GetEquippedItemPower(player, spec, role, EQUIPMENT_SLOT_MAINHAND);
        default:
            return 0.0f;
    }
}

uint32 GetEquippedItemLevel(Player* player, uint8 equipmentSlot)
{
    if (!player)
        return 0;

    Item* equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, equipmentSlot);
    return equipped && equipped->GetTemplate() ? equipped->GetTemplate()->ItemLevel : 0;
}

uint32 GetEquippedItemLevelForCandidate(Player* player, ItemTemplate const& candidate)
{
    switch (candidate.InventoryType)
    {
        case INVTYPE_HEAD: return GetEquippedItemLevel(player, EQUIPMENT_SLOT_HEAD);
        case INVTYPE_NECK: return GetEquippedItemLevel(player, EQUIPMENT_SLOT_NECK);
        case INVTYPE_SHOULDERS: return GetEquippedItemLevel(player, EQUIPMENT_SLOT_SHOULDERS);
        case INVTYPE_CLOAK: return GetEquippedItemLevel(player, EQUIPMENT_SLOT_BACK);
        case INVTYPE_CHEST:
        case INVTYPE_ROBE: return GetEquippedItemLevel(player, EQUIPMENT_SLOT_CHEST);
        case INVTYPE_WRISTS: return GetEquippedItemLevel(player, EQUIPMENT_SLOT_WRISTS);
        case INVTYPE_HANDS: return GetEquippedItemLevel(player, EQUIPMENT_SLOT_HANDS);
        case INVTYPE_WAIST: return GetEquippedItemLevel(player, EQUIPMENT_SLOT_WAIST);
        case INVTYPE_LEGS: return GetEquippedItemLevel(player, EQUIPMENT_SLOT_LEGS);
        case INVTYPE_FEET: return GetEquippedItemLevel(player, EQUIPMENT_SLOT_FEET);
        case INVTYPE_FINGER:
            return std::min(GetEquippedItemLevel(player, EQUIPMENT_SLOT_FINGER1),
                GetEquippedItemLevel(player, EQUIPMENT_SLOT_FINGER2));
        case INVTYPE_TRINKET:
            return std::min(GetEquippedItemLevel(player, EQUIPMENT_SLOT_TRINKET1),
                GetEquippedItemLevel(player, EQUIPMENT_SLOT_TRINKET2));
        case INVTYPE_SHIELD:
        case INVTYPE_HOLDABLE:
        case INVTYPE_WEAPONOFFHAND:
            return GetEquippedItemLevel(player, EQUIPMENT_SLOT_OFFHAND);
        case INVTYPE_RANGED:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_THROWN:
        case INVTYPE_RELIC:
            return GetEquippedItemLevel(player, EQUIPMENT_SLOT_RANGED);
        case INVTYPE_WEAPON:
        {
            uint32 const mainLevel = GetEquippedItemLevel(player, EQUIPMENT_SLOT_MAINHAND);
            uint32 const offLevel = GetEquippedItemLevel(player, EQUIPMENT_SLOT_OFFHAND);
            return player && player->CanDualWield() && offLevel ?
				std::min(mainLevel, offLevel) : mainLevel;
        }
        case INVTYPE_WEAPONMAINHAND:
        case INVTYPE_2HWEAPON:
            return GetEquippedItemLevel(player, EQUIPMENT_SLOT_MAINHAND);
        default:
            return 0;
    }
}

std::vector<uint8> GetCandidateEquipmentSlots(Player *player,
		ItemTemplate const &candidate) {
	std::vector<uint8> slots;
	auto add = [&](uint8 slot) { slots.push_back(slot); };
	switch (candidate.InventoryType) {
		case INVTYPE_HEAD: add(EQUIPMENT_SLOT_HEAD); break;
		case INVTYPE_NECK: add(EQUIPMENT_SLOT_NECK); break;
		case INVTYPE_SHOULDERS: add(EQUIPMENT_SLOT_SHOULDERS); break;
		case INVTYPE_CLOAK: add(EQUIPMENT_SLOT_BACK); break;
		case INVTYPE_CHEST:
		case INVTYPE_ROBE: add(EQUIPMENT_SLOT_CHEST); break;
		case INVTYPE_WRISTS: add(EQUIPMENT_SLOT_WRISTS); break;
		case INVTYPE_HANDS: add(EQUIPMENT_SLOT_HANDS); break;
		case INVTYPE_WAIST: add(EQUIPMENT_SLOT_WAIST); break;
		case INVTYPE_LEGS: add(EQUIPMENT_SLOT_LEGS); break;
		case INVTYPE_FEET: add(EQUIPMENT_SLOT_FEET); break;
		case INVTYPE_FINGER:
			add(EQUIPMENT_SLOT_FINGER1); add(EQUIPMENT_SLOT_FINGER2); break;
		case INVTYPE_TRINKET:
			add(EQUIPMENT_SLOT_TRINKET1); add(EQUIPMENT_SLOT_TRINKET2); break;
		case INVTYPE_SHIELD:
		case INVTYPE_HOLDABLE:
		case INVTYPE_WEAPONOFFHAND: add(EQUIPMENT_SLOT_OFFHAND); break;
		case INVTYPE_RANGED:
		case INVTYPE_RANGEDRIGHT:
		case INVTYPE_THROWN:
		case INVTYPE_RELIC: add(EQUIPMENT_SLOT_RANGED); break;
		case INVTYPE_WEAPON:
			add(EQUIPMENT_SLOT_MAINHAND);
			if (player && player->CanDualWield()) add(EQUIPMENT_SLOT_OFFHAND);
			break;
		case INVTYPE_WEAPONMAINHAND:
		case INVTYPE_2HWEAPON: add(EQUIPMENT_SLOT_MAINHAND); break;
		default: break;
	}
	return slots;
}

std::optional<uint8> GetEndgameEliteUpgradeSlot(Player *player,
		ItemTemplate const &candidate, std::uint32_t threshold) {
	auto const slots = GetCandidateEquipmentSlots(player, candidate);
	std::vector<EquippedRewardSlot> states;
	states.reserve(slots.size());
	for (uint8 slot : slots) {
		Item *equipped = player ?
			player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot) : nullptr;
		ItemTemplate const *item = equipped ? equipped->GetTemplate() : nullptr;
		states.push_back({item ? item->ItemId : 0, item ? item->ItemLevel : 0,
			true});
	}
	std::int32_t const selected = SelectBelowThresholdSlot(candidate.ItemId,
		threshold, states.data(), states.size());
	return selected < 0 ? std::nullopt :
		std::optional<uint8>(slots[static_cast<std::size_t>(selected)]);
}

} // namespace

NativeHuntsManager &NativeHuntsManager::Instance() {
	static NativeHuntsManager instance;
	return instance;
}

void NativeHuntsManager::Configure(NativeHuntsConfig config) {
	bool const relicCountChanged =
		_config.RelicsCountPerZone != config.RelicsCountPerZone;
	_config = std::move(config);
	if (relicCountChanged) {
		_relicDisableCleanupComplete = false;
		_relicPoolGenerationJobs.clear();
		_relicPoolGenerationOrder.clear();
		_relicPoolRetryAfter.clear();
	}
	if (_initialized && relicCountChanged)
		ReconcileRelicPools();
}

bool NativeHuntsManager::IsEnabled() const {
	return _config.Enabled && _resources.Ready;
}

bool NativeHuntsManager::IsSealStoreAvailable(Player const *player) const {
	if (!IsEnabled() || !player || !_resources.SealItemEntry)
		return false;
	std::uint32_t const requiredLevel = std::max(
		_config.EliteSealMinimumLevel, _config.EliteEndgameRewardLevel);
	return player->GetLevel() >= requiredLevel;
}

bool NativeHuntsManager::AreSealStoreExtendedCostsReady() const {
	return IsEnabled() && _resources.SealStoreReady;
}

std::uint32_t NativeHuntsManager::GetSealBalance(Player const *player) const {
	return player && _resources.Ready && _resources.SealItemEntry
		? player->GetItemCount(_resources.SealItemEntry, false) : 0;
}

std::uint32_t NativeHuntsManager::GetSealStoreTierCost(std::uint8_t tier) const {
	return tier >= 1 && tier <= 4 ? _config.SealStoreTierCost[tier - 1] : 0;
}

std::uint32_t NativeHuntsManager::GetSealStoreTierMinItemLevel(
		std::uint8_t tier) const {
	return tier >= 1 && tier <= 4
		? _config.SealStoreTierMinItemLevel[tier - 1] : 0;
}

std::uint32_t NativeHuntsManager::GetSealStoreTierMaxItemLevel(
		std::uint8_t tier) const {
	return tier >= 1 && tier <= 4
		? _config.SealStoreTierMaxItemLevel[tier - 1] : 0;
}

std::uint32_t NativeHuntsManager::GetSealStoreExtendedCost(
		std::uint8_t tier) const {
	return tier >= 1 && tier <= 4 && _resources.SealStoreReady
		? _resources.SealStoreExtendedCosts[tier - 1] : 0;
}

std::string const &NativeHuntsManager::GetSealStoreUnavailableReason() const {
	return _resources.SealStoreReason;
}

bool NativeHuntsManager::IsSealStoreItemEligible(Player *player,
		std::uint32_t spec, std::uint8_t tier, SealStoreSlot slot,
		std::uint32_t itemId, SealStoreCatalogDiagnostics *diagnostics) const {
	if (!IsSealStoreAvailable(player) || !IsSpecForClass(player, spec) ||
		tier < 1 || tier > 4)
		return false;
	ItemTemplate const *item = sObjectMgr->GetItemTemplate(itemId);
	if (!item || item->Quality != ITEM_QUALITY_EPIC ||
		(item->Class != ITEM_CLASS_WEAPON && item->Class != ITEM_CLASS_ARMOR) ||
		!MatchesSealStoreSlot(*item, slot) || item->RequiredLevel > player->GetLevel() ||
		item->ItemLevel < GetSealStoreTierMinItemLevel(tier) ||
		item->ItemLevel > GetSealStoreTierMaxItemLevel(tier))
		return false;
	if (diagnostics)
		++diagnostics->TotalCandidates;
	RewardRole const role = GetRewardRole(player, spec);
	if (!MatchesSealCatalogRole(role, *item)) {
		if (diagnostics)
			++diagnostics->RejectedByRolePolicy;
		return false;
	}
	if (diagnostics)
		++diagnostics->AcceptedByRolePolicy;
	if (!IsSpecCompatibleEquipment(spec, *item) ||
		item->HasFlag(ITEM_FLAG_HEROIC_TOOLTIP) ||
		player->CanUseItem(item) != EQUIP_ERR_OK)
		return false;
	for (std::uint32_t index = 0;
		 index < item->StatsCount && index < MAX_ITEM_PROTO_STATS; ++index)
		if (item->ItemStat[index].ItemStatType == ITEM_MOD_RESILIENCE_RATING &&
			item->ItemStat[index].ItemStatValue > 0)
			return false;
	if (item->Class == ITEM_CLASS_ARMOR && GetArmorPreference(player, *item) < 0.0f)
		return false;
	if (ScoreRewardItem(player, spec, role, *item) <= 0.0f)
		return false;
	if (diagnostics)
		++diagnostics->RemainingAfterEligibility;
	return true;
}

std::vector<SealStoreItem> NativeHuntsManager::BuildSealStoreItems(
		Player *player, std::uint32_t spec, std::uint8_t tier,
		SealStoreSlot slot, SealStoreCatalogDiagnostics *diagnostics) const {
	std::vector<SealStoreItem> items;
	if (!IsSealStoreAvailable(player) || !IsSpecForClass(player, spec) ||
		tier < 1 || tier > 4 || !GetSealStoreTierCost(tier))
		return items;
	RewardRole const role = GetRewardRole(player, spec);
	for (auto const &[itemId, item] : *sObjectMgr->GetItemTemplateStore()) {
		if (!IsSealStoreItemEligible(player, spec, tier, slot, itemId,
				diagnostics))
			continue;
		items.push_back({itemId, item.Name1, item.ItemLevel,
			ScoreRewardItem(player, spec, role, item)});
	}
	std::sort(items.begin(), items.end(), [](SealStoreItem const &left,
			SealStoreItem const &right) {
		if (std::fabs(left.Score - right.Score) > 0.01f)
			return left.Score > right.Score;
		if (left.ItemLevel != right.ItemLevel)
			return left.ItemLevel > right.ItemLevel;
		return left.ItemId < right.ItemId;
	});
	if (items.size() > 20)
		items.resize(20);
	return items;
}

bool NativeHuntsManager::PurchaseSealStoreItem(Player *player,
		std::uint32_t spec, std::uint8_t tier, std::uint32_t itemId,
		std::string &message) {
	if (!IsSealStoreAvailable(player)) {
		message = "Huntmaster's Seal rewards are reserved for level-cap hunters.";
		return false;
	}
	std::uint32_t const cost = GetSealStoreTierCost(tier);
	if (!cost) {
		message = "That Huntmaster reward tier is disabled.";
		return false;
	}
	if (!IsSpecForClass(player, spec)) {
		message = "That reward specialization is not valid for your class.";
		return false;
	}
	bool eligible = false;
	for (std::uint8_t raw = static_cast<std::uint8_t>(SealStoreSlot::Weapon);
		raw <= static_cast<std::uint8_t>(SealStoreSlot::Relic); ++raw)
		if (IsSealStoreItemEligible(player, spec, tier,
				static_cast<SealStoreSlot>(raw), itemId)) {
			eligible = true;
			break;
		}
	if (!eligible) {
		message = "That item is not an eligible Huntmaster's Seal reward for this specialization and tier.";
		return false;
	}
	std::uint32_t const balance = GetSealBalance(player);
	if (balance < cost) {
		message = "You need " + std::to_string(cost) +
			" Huntmaster's Seals, but you only have " +
			std::to_string(balance) + ".";
		return false;
	}
	ItemPosCountVec destination;
	if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, itemId, 1) !=
		EQUIP_ERR_OK) {
		message = "Make room in your bags before purchasing this reward.";
		return false;
	}

	// WorldSession handles a player's gossip packet serially. Revalidate every
	// input and create the item before consuming currency so an exceptional
	// creation failure cannot produce a partial transaction. The already-
	// validated carried Seal count cannot change during this handler.
	Item *item = player->StoreNewItem(destination, itemId, true);
	if (!item) {
		message = "The purchase could not be completed; no Huntmaster's Seals were consumed.";
		return false;
	}
	player->DestroyItemCount(_resources.SealItemEntry, cost, true);
	item->SetBinding(true);
	player->SendNewItem(item, 1, true, false);
	QueueUiState(player);
	ItemTemplate const *itemTemplate = sObjectMgr->GetItemTemplate(itemId);
	std::uint32_t const remaining = GetSealBalance(player);
	message = "Purchased " + std::string(itemTemplate ? itemTemplate->Name1 :
		"Huntmaster reward") + " for " + std::to_string(cost) +
		" Huntmaster's Seals. " + std::to_string(remaining) + " Seal" +
		(remaining == 1 ? " remains." : "s remain.");
	return true;
}

ManagedResources const &NativeHuntsManager::Resources() const {
	return _resources;
}

bool NativeHuntsManager::ResolveManagedResources() {
	_resources = {};
	std::string reason;
	auto const *provider = FindContentProvider(reason);
	if (!provider) {
		_resources.Reason = std::move(reason);
		return false;
	}

	auto resolve = [&](std::string const &symbol, char const *kind,
					   std::uint32_t &value) {
		std::string resourceReason;
		auto const result = provider->ResolveResource(
			Package, symbol, kind, value, resourceReason);
		if (result == ContentResourcesV1::Result::Ready && value)
			return true;
		_resources.Reason = symbol + ": " + resourceReason;
		return false;
	};
	auto resolveAura = [&](char const *symbol, std::uint32_t &value) {
		std::string resourceReason;
		std::uint32_t resolved = 0;
		auto const result = provider->ResolveResource(
			Package, symbol, "spell.id", resolved, resourceReason);
		if (result == ContentResourcesV1::Result::Ready && resolved) {
			value = resolved;
			return;
		}
		LOG_ERROR("module.native_hunts",
			"Native Hunts informational aura '{}' is unavailable: {}. Hunt "
			"gameplay will continue without that aura.",
			symbol, resourceReason.empty() ? "Content Manager did not return Ready"
				: resourceReason);
	};
	resolveAura("active-standard-hunt", _resources.StandardHuntAuraSpell);
	resolveAura("active-elite-hunt", _resources.EliteHuntAuraSpell);

	for (auto const &huntmaster : Huntmasters()) {
		std::uint32_t entry = 0;
		std::uint32_t spawn = 0;
		if (!resolve(huntmaster.Symbol, "creature-template.id", entry) ||
			!resolve(huntmaster.SpawnSymbol, "creature-spawn.guid", spawn))
			return false;
		_resources.CreatureEntries[huntmaster.Symbol] = entry;
		_resources.CreatureSpawns[huntmaster.SpawnSymbol] = spawn;
	}
	for (auto const &prey : StandardPrey()) {
		std::uint32_t entry = 0;
		if (!resolve(prey.Symbol, "creature-template.id", entry))
			return false;
		_resources.CreatureEntries[prey.Symbol] = entry;
	}
	for (auto const &prey : ElitePrey()) {
		std::uint32_t entry = 0;
		if (!resolve(prey.Symbol, "creature-template.id", entry))
			return false;
		_resources.CreatureEntries[prey.Symbol] = entry;
	}
	{
		std::uint32_t entry=0;
		if (!resolve("elite-farstrider-wolf","creature-template.id",entry)) return false;
		_resources.CreatureEntries["elite-farstrider-wolf"]=entry;
	}
	if (!resolve("prey-trail-crystal", "gameobject-template.id",
				 _resources.TrailCrystalEntry) ||
		!resolve("return-rift", "gameobject-template.id",
				 _resources.ReturnRiftEntry) ||
		!resolve(SealSymbol, "item.id", _resources.SealItemEntry))
		return false;

	auto const *crystal =
		sObjectMgr->GetGameObjectTemplate(_resources.TrailCrystalEntry);
	auto const *rift = sObjectMgr->GetGameObjectTemplate(_resources.ReturnRiftEntry);
	auto const scriptedButton = [](GameObjectTemplate const *value,
								 char const *scriptName) {
		return value && value->type == GAMEOBJECT_TYPE_BUTTON &&
			value->ScriptId == sObjectMgr->GetScriptId(scriptName) &&
			value->button.startOpen == 0 && value->button.lockId == 0 &&
			value->button.autoCloseTime == 0 &&
			value->button.linkedTrap == 0 &&
			value->button.noDamageImmune == 0 && value->button.large == 0 &&
			value->button.openTextID == 0 && value->button.closeTextID == 0 &&
			value->button.losOK == 0;
	};
	if (!scriptedButton(crystal, "mod_native_hunts_trail_crystal") ||
		!scriptedButton(rift, "mod_native_hunts_return_rift") ||
		!sObjectMgr->GetItemTemplate(_resources.SealItemEntry)) {
		_resources.Reason =
			"managed scripted buttons/items are missing, stale, or carry default "
			"lock/autoclose/trap behavior; rebuild content and restart worldserver";
		return false;
	}

	_resources.SealStoreReady = true;
	_resources.SealStoreReason = "validated managed Huntmaster vendor costs";
	for (std::size_t index = 0; index < _resources.SealStoreExtendedCosts.size();
			++index) {
		if (_config.SealStoreTierCost[index] == 0)
			continue;
		std::string const symbol = "seal-cost-tier-" +
			std::to_string(index + 1);
		std::uint32_t extendedCost = 0;
		std::string resourceReason;
		auto const result = provider->ResolveResource(Package, symbol,
			"item-extended-cost.id", extendedCost, resourceReason);
		ItemExtendedCostEntry const *entry = result ==
			ContentResourcesV1::Result::Ready && extendedCost
			? sItemExtendedCostStore.LookupEntry(extendedCost) : nullptr;
		bool valid = entry && entry->reqhonorpoints == 0 &&
			entry->reqarenapoints == 0 && entry->reqarenaslot == 0 &&
			entry->reqpersonalarenarating == 0 &&
			entry->reqitem[0] == _resources.SealItemEntry &&
			entry->reqitemcount[0] == _config.SealStoreTierCost[index];
		for (std::size_t requirement = 1;
			valid && requirement < MAX_ITEM_EXTENDED_COST_REQUIREMENTS;
			++requirement)
			valid = entry->reqitem[requirement] == 0 &&
				entry->reqitemcount[requirement] == 0;
		if (!valid) {
			_resources.SealStoreReady = false;
			_resources.SealStoreReason = symbol + ": " +
				(resourceReason.empty()
					? "managed extended cost is missing or does not match the configured Seal price"
					: resourceReason);
			LOG_ERROR("module.native_hunts",
				"Native Huntmaster merchant disabled while Hunts remain active: {}",
				_resources.SealStoreReason);
			break;
		}
		_resources.SealStoreExtendedCosts[index] = extendedCost;
	}
	for (auto const &[symbol, entry] : _resources.CreatureEntries)
		if (!sObjectMgr->GetCreatureTemplate(entry)) {
			_resources.Reason = symbol +
				": managed creature is applied but not loaded; restart worldserver";
			return false;
		}

	_resources.Ready = true;
	_resources.Reason = "validated ACTIVE/APPLIED Native Hunts resources";
	std::uint32_t currencyCategory = 0;
	std::uint32_t currencyKnownBit = 0;
	std::string currencyCategoryReason;
	std::string currencyKnownBitReason;
	auto const categoryResult = provider->ResolveResource(Package, "hunts",
		"currency-category.id", currencyCategory, currencyCategoryReason);
	auto const knownBitResult = provider->ResolveResource(Package,
		"huntmaster-seal-currency", "currency.known-bit", currencyKnownBit,
		currencyKnownBitReason);
	if (categoryResult == ContentResourcesV1::Result::Ready && currencyCategory &&
		knownBitResult == ContentResourcesV1::Result::Ready && currencyKnownBit) {
		LOG_INFO("module.native_hunts",
			"Native Hunts Content Manager resources: Package: {}; Seal symbol: {}; "
			"Seal item ID: {}; Currency category ID: {}; Currency known-bit: {}; "
			"Extended-cost tier IDs: [{}, {}, {}, {}]",
			Package, SealSymbol, _resources.SealItemEntry, currencyCategory,
			currencyKnownBit, _resources.SealStoreExtendedCosts[0],
			_resources.SealStoreExtendedCosts[1],
			_resources.SealStoreExtendedCosts[2],
			_resources.SealStoreExtendedCosts[3]);
	} else {
		LOG_WARN("module.native_hunts",
			"Native Hunts Content Manager startup diagnostic could not resolve "
			"currency metadata: category='{}'; known-bit='{}'",
			currencyCategoryReason, currencyKnownBitReason);
	}
	return true;
}

void NativeHuntsManager::Initialize() {
	RemoveAllRelicPoolObjects();
	_relicPools.clear();
	_relicPoolGenerationJobs.clear();
	_relicPoolGenerationOrder.clear();
	_relicPoolRetryAfter.clear();
	_relicDisableCleanupComplete = false;
	_runtimes.clear();
	_uncertainTurnIns.clear();
	_uiLastRequestMs.clear();
	_uiSequences.clear();
	_uiLastProjection.clear();
	_uiPending.clear();
	if (!_config.Enabled) {
		_resources.Reason = "Native Hunts is disabled by configuration";
		_initialized = true;
		return;
	}
	LOG_INFO("module.native_hunts",
		"Native Hunts server build marker: seal-catalog-policy-v1");
	ResolveManagedResources();
	ResolveGuardLocators();
	LoadAssignments();
	InitializeRelicPools();
	LoadRelicPools();
	ReconcileRelicPools();
	_initialized = true;
	if (_resources.Ready)
		LOG_INFO("module.native_hunts",
				 "Native Hunts initialized with {} Huntmasters, {} standard prey, "
				 "{} known final sites, and {} restored assignment(s).",
				 Huntmasters().size(), StandardPrey().size(),
				 KnownFinalSites().size(), _runtimes.size());
	else
		LOG_ERROR("module.native_hunts", "Native Hunts unavailable: {}",
				  _resources.Reason);
}

void NativeHuntsManager::ResolveGuardLocators() {
	_guardLocators.clear();
	for (auto const &seed : GuardLocatorSeeds())
		_guardLocators.emplace(seed.CreatureEntry, seed.HuntmasterKey);
	for (auto const &seed : GuardLocatorSeeds()) {
		QueryResult aliases = WorldDatabase.Query(
			"SELECT sibling.`entry` FROM `creature_template` seed JOIN "
			"`creature_template` sibling ON sibling.`gossip_menu_id`="
			"seed.`gossip_menu_id` WHERE seed.`entry`={} AND "
			"seed.`gossip_menu_id`<>0", seed.CreatureEntry);
		if (!aliases)
			continue;
		do
			_guardLocators.emplace(aliases->Fetch()[0].Get<std::uint32_t>(),
				seed.HuntmasterKey);
		while (aliases->NextRow());
	}
}

void NativeHuntsManager::Shutdown() {
	RemoveAllRelicPoolObjects();
	_relicPools.clear();
	_relicPoolGenerationJobs.clear();
	_relicPoolGenerationOrder.clear();
	_relicPoolRetryAfter.clear();
	for (auto &[guid, runtime] : _runtimes)
		RemoveRuntimeObjects(ConnectedPlayer(guid), runtime);
	_runtimes.clear();
	_uncertainTurnIns.clear();
	_uiLastRequestMs.clear();
	_uiSequences.clear();
	_uiLastProjection.clear();
	_uiPending.clear();
	_initialized = false;
}

void NativeHuntsManager::LoadAssignments() {
	QueryResult result = CharacterDatabase.Query(
		"SELECT `character_guid`,`hunt_instance_id`,`huntmaster_key`,"
		"`huntmaster_entry`,"
		"`huntmaster_spawn_guid`,`prey_key`,`zone_key`,`zone_id`,`map_id`,"
		"`state`,`tracking_progress`,`ambushes_completed`,`ambush_pending`,"
		"`final_site_key`,`revision`,`tier`,`relic_pool_id` "
		"FROM `native_hunt_assignment`");
	if (!result)
		return;
	do {
		Field *field = result->Fetch();
		HuntRuntime runtime;
		runtime.CharacterGuid = field[0].Get<std::uint32_t>();
		runtime.HuntInstanceId = field[1].Get<std::string>();
		std::string const huntmasterKey = field[2].Get<std::string>();
		runtime.HuntmasterEntry = field[3].Get<std::uint32_t>();
		runtime.HuntmasterSpawn = field[4].Get<std::uint32_t>();
		std::string const preyKey = field[5].Get<std::string>();
		std::string const zoneKey = field[6].Get<std::string>();
		runtime.ZoneId = field[7].Get<std::uint32_t>();
		runtime.MapId = field[8].Get<std::uint32_t>();
		runtime.Aggregate.State = static_cast<HuntState>(field[9].Get<std::uint8_t>());
		runtime.Aggregate.Progress = field[10].Get<std::uint8_t>();
		runtime.AmbushesCompleted = field[11].Get<std::uint8_t>();
		runtime.AmbushPending = field[12].Get<std::uint8_t>() != 0;
		runtime.Aggregate.FinalLocationKey = field[13].Get<std::string>();
		runtime.Aggregate.Revision = field[14].Get<std::uint64_t>();
		if (!field[16].IsNull())
			runtime.RelicPoolId = field[16].Get<std::string>();

		auto const *huntmaster = FindHuntmaster(huntmasterKey);
		auto const *prey = FindPrey(preyKey);
		auto const *zone = FindZone(zoneKey);
		if (!huntmaster || !prey || !zone)
			continue;
		PreyTier const tier = static_cast<PreyTier>(field[15].Get<std::uint8_t>());
		runtime.Aggregate.Identity =
			{huntmaster->Key, huntmaster->Name, huntmaster->City, prey->Key,
			 prey->Name, tier, zone->ZoneKey, zone->ZoneName};
		if (!runtime.Aggregate.FinalLocationKey.empty()) {
			auto const *site = FindFinalSite(runtime.Aggregate.FinalLocationKey);
			if (!site)
				continue;
			runtime.Aggregate.FinalLocationName = site->ZoneName;
		}
		if (!IsUuid(runtime.HuntInstanceId) ||
			!HuntDomain::IsValid(runtime.Aggregate))
			continue;
		bool changed = false;
		if (runtime.Aggregate.State == HuntState::PreyActive) {
			HuntDomain::Execute(runtime.Aggregate, RecoverAfterRestart{});
			changed = true;
		}
		if (runtime.Aggregate.State == HuntState::Tracking)
			changed = ReconcileTrackingMilestones(nullptr, runtime) || changed;
		if (changed)
			SaveAssignment(runtime);
		_runtimes[runtime.CharacterGuid] = std::move(runtime);
	} while (result->NextRow());
}

bool NativeHuntsManager::CreateAssignment(HuntRuntime const &runtime) {
	if (!IsUuid(runtime.HuntInstanceId))
		return false;
	auto const &aggregate = runtime.Aggregate;
	auto transaction = CharacterDatabase.BeginTransaction();
	transaction->Append(
		"INSERT INTO `native_hunt_assignment` "
		"(`character_guid`,`hunt_instance_id`,`huntmaster_key`,"
		"`huntmaster_entry`,`huntmaster_spawn_guid`,`prey_key`,`tier`,"
		"`zone_key`,`zone_id`,`map_id`,`state`,`tracking_progress`,"
		"`ambushes_completed`,`ambush_pending`,`final_site_key`,`revision`,"
		"`relic_pool_id`) VALUES ({},'{}','{}',{},{},'{}',{},'{}',{},{},{},"
		"{},{},{},'{}',{},NULLIF('{}',''))",
		runtime.CharacterGuid, runtime.HuntInstanceId,
		aggregate.Identity.HuntmasterKey, runtime.HuntmasterEntry,
		runtime.HuntmasterSpawn, aggregate.Identity.PreyKey,
		static_cast<std::uint32_t>(aggregate.Identity.Tier),
		aggregate.Identity.ZoneKey, runtime.ZoneId, runtime.MapId,
		static_cast<std::uint32_t>(aggregate.State),
		static_cast<std::uint32_t>(aggregate.Progress),
		static_cast<std::uint32_t>(runtime.AmbushesCompleted),
		static_cast<std::uint32_t>(runtime.AmbushPending),
		aggregate.FinalLocationKey, aggregate.Revision, runtime.RelicPoolId);
	bool const acknowledged = CommitCharacterTransactionAndWait(transaction);
	QueryResult persisted = CharacterDatabase.Query(
		"SELECT 1 FROM `native_hunt_assignment` WHERE `character_guid`={} "
		"AND `hunt_instance_id`='{}'", runtime.CharacterGuid,
		runtime.HuntInstanceId);
	if (!acknowledged && persisted)
		LOG_WARN("module.native_hunts",
			"Assignment transaction acknowledgement was lost for Hunt instance "
			"{}, but the committed row was verified.", runtime.HuntInstanceId);
	return persisted != nullptr;
}

void NativeHuntsManager::SaveAssignment(HuntRuntime const &runtime) {
	auto const &aggregate = runtime.Aggregate;
	CharacterDatabase.DirectExecute(
		"INSERT INTO `native_hunt_assignment` "
		"(`character_guid`,`hunt_instance_id`,`huntmaster_key`,`huntmaster_entry`,"
		"`huntmaster_spawn_guid`,`prey_key`,`tier`,`zone_key`,`zone_id`,"
		"`map_id`,`state`,`tracking_progress`,`ambushes_completed`,"
		"`ambush_pending`,`final_site_key`,`revision`,`relic_pool_id`) "
		"VALUES ({},'{}','{}',{},{},'{}',{},'{}',{},{},{},{},{},{},'{}',{},"
		"NULLIF('{}','')) "
		"ON DUPLICATE KEY UPDATE `huntmaster_key`=VALUES(`huntmaster_key`),"
		"`huntmaster_entry`=VALUES(`huntmaster_entry`),"
		"`huntmaster_spawn_guid`=VALUES(`huntmaster_spawn_guid`),"
		"`prey_key`=VALUES(`prey_key`),`tier`=VALUES(`tier`),"
		"`zone_key`=VALUES(`zone_key`),`zone_id`=VALUES(`zone_id`),"
		"`map_id`=VALUES(`map_id`),`state`=VALUES(`state`),"
		"`tracking_progress`=VALUES(`tracking_progress`),"
		"`ambushes_completed`=VALUES(`ambushes_completed`),"
		"`ambush_pending`=VALUES(`ambush_pending`),"
		"`final_site_key`=VALUES(`final_site_key`),"
		"`relic_pool_id`=VALUES(`relic_pool_id`),"
		"`revision`=VALUES(`revision`),`updated_at`=CURRENT_TIMESTAMP",
		runtime.CharacterGuid, runtime.HuntInstanceId,
		aggregate.Identity.HuntmasterKey,
		runtime.HuntmasterEntry, runtime.HuntmasterSpawn,
		aggregate.Identity.PreyKey,
		static_cast<std::uint32_t>(aggregate.Identity.Tier),
		aggregate.Identity.ZoneKey, runtime.ZoneId, runtime.MapId,
		static_cast<std::uint32_t>(aggregate.State),
		static_cast<std::uint32_t>(aggregate.Progress),
		static_cast<std::uint32_t>(runtime.AmbushesCompleted),
		static_cast<std::uint32_t>(runtime.AmbushPending),
		aggregate.FinalLocationKey, aggregate.Revision, runtime.RelicPoolId);
}

void NativeHuntsManager::RemoveRuntimeObjects(Player *player,
										 HuntRuntime &runtime) {
	auto remove = [&](ObjectGuid &guid) {
		if (!guid.IsEmpty() && player)
			if (GameObject *object = ObjectAccessor::GetGameObject(*player, guid))
				object->Delete();
		guid.Clear();
	};
	remove(runtime.CrystalGuid);
	remove(runtime.ReturnRiftGuid);
	if (!runtime.AmbushGuid.IsEmpty() && player)
		if (Creature *ambush = ObjectAccessor::GetCreature(*player, runtime.AmbushGuid))
			ambush->DespawnOrUnsummon();
	runtime.AmbushGuid.Clear();
	if (!runtime.PreyGuid.IsEmpty() && player)
		if (Creature *prey = ObjectAccessor::GetCreature(*player, runtime.PreyGuid))
			prey->DespawnOrUnsummon();
	runtime.PreyGuid.Clear();
	if (!runtime.CompanionGuid.IsEmpty() && player)
		if (Creature *companion=ObjectAccessor::GetCreature(*player,runtime.CompanionGuid))
			companion->DespawnOrUnsummon();
	runtime.CompanionGuid.Clear();
}

void NativeHuntsManager::DeleteAssignment(HuntRuntime &runtime) {
	Player *player = ConnectedPlayer(runtime.CharacterGuid);
	RemoveRuntimeObjects(player, runtime);
	CharacterDatabase.DirectExecute(
		"DELETE FROM `native_hunt_assignment` WHERE `character_guid`={}",
		runtime.CharacterGuid);
	ReconcileHuntAura(player, nullptr);
}

void NativeHuntsManager::InitializeRelicPools() {
	_relicPoolsReady = false;
	if (_config.RelicsCountPerZone == 0)
		return;
	auto const *relic = sObjectMgr->GetGameObjectTemplate(
		_config.RelicGameObjectEntry);
	if (!relic || relic->type != GAMEOBJECT_TYPE_BUTTON ||
		relic->ScriptId !=
			sObjectMgr->GetScriptId("mod_native_hunts_shared_relic") ||
		relic->button.startOpen != 0 || relic->button.lockId != 0 ||
		relic->button.autoCloseTime != 0 || relic->button.linkedTrap != 0) {
		LOG_ERROR("module.native_hunts",
			"Native Hunts relic entry {} is missing or is not the expected "
			"non-consuming scripted button.", _config.RelicGameObjectEntry);
		return;
	}
	_relicPoolsReady = true;
}

void NativeHuntsManager::RemoveRelicPoolObjects(RelicPool &pool) {
	Map *map = sMapMgr->FindMap(pool.Scope.MapId, pool.Scope.InstanceId);
	for (auto &position : pool.Positions) {
		if (map && !position.Guid.IsEmpty())
			if (GameObject *relic = map->GetGameObject(position.Guid))
				relic->Delete();
		position.Guid.Clear();
	}
}

void NativeHuntsManager::RemoveAllRelicPoolObjects() {
	for (auto &[id, pool] : _relicPools) {
		(void)id;
		RemoveRelicPoolObjects(pool);
	}
}

void NativeHuntsManager::LoadRelicPools() {
	_relicPools.clear();
	if (!_relicPoolsReady)
		return;
	std::vector<std::string> invalidPools;
	CharacterDatabase.DirectExecute(
		"DELETE pool FROM `native_hunt_relic_pool` pool LEFT JOIN "
		"`native_hunt_assignment` assignment ON assignment.`relic_pool_id`="
		"pool.`pool_id` WHERE assignment.`relic_pool_id` IS NULL");
	QueryResult pools = CharacterDatabase.Query(
		"SELECT DISTINCT pool.`pool_id`,pool.`map_id`,pool.`instance_id`,"
		"pool.`zone_id`,pool.`relic_count` FROM `native_hunt_relic_pool` pool "
		"JOIN `native_hunt_assignment` assignment ON "
		"assignment.`relic_pool_id`=pool.`pool_id` WHERE pool.`retired_at` IS NULL");
	if (pools) {
		do {
			Field *field = pools->Fetch();
			RelicPool pool;
			pool.Id = field[0].Get<std::string>();
			pool.Scope.MapId = field[1].Get<std::uint32_t>();
			pool.Scope.InstanceId = field[2].Get<std::uint32_t>();
			pool.Scope.ZoneId = field[3].Get<std::uint32_t>();
			pool.RequiredCount = field[4].Get<std::uint32_t>();
			if (IsUuid(pool.Id) && pool.RequiredCount > 0 &&
				pool.RequiredCount <= MaximumRelicsPerZone)
				_relicPools.emplace(pool.Id, std::move(pool));
			else
				invalidPools.push_back(pool.Id);
		} while (pools->NextRow());
	}
	for (auto &[id, pool] : _relicPools) {
		bool positionsValid = true;
		QueryResult positions = CharacterDatabase.Query(
			"SELECT `relic_index`,`position_x`,`position_y`,`position_z`,"
			"`orientation` FROM `native_hunt_relic_position` WHERE "
			"`pool_id`='{}' ORDER BY `relic_index`", id);
		if (positions) {
			do {
				Field *field = positions->Fetch();
				RelicPosition position{field[0].Get<std::uint32_t>(),
					field[1].Get<float>(), field[2].Get<float>(),
					field[3].Get<float>(), field[4].Get<float>(), {}};
				positionsValid = positionsValid &&
					position.Index == pool.Positions.size() &&
					std::isfinite(position.X) && std::isfinite(position.Y) &&
					std::isfinite(position.Z) &&
					std::isfinite(position.Orientation) &&
					MapMgr::IsValidMapCoord(pool.Scope.MapId, position.X,
						position.Y, position.Z, position.Orientation);
				pool.Positions.push_back(position);
			} while (positions->NextRow());
		}
		if (!positionsValid || pool.Positions.size() != pool.RequiredCount) {
			LOG_ERROR("module.native_hunts",
				"Relic pool {} expected {} persisted positions but loaded {}; "
				"the corrupt pool will be retired and regenerated.", id,
				pool.RequiredCount,
				pool.Positions.size());
			invalidPools.push_back(id);
		}
	}
	for (auto const &id : invalidPools) {
		for (auto &[guid, runtime] : _runtimes) {
			(void)guid;
			if (runtime.RelicPoolId == id)
				runtime.RelicPoolId.clear();
		}
		RetireRelicPool(id);
	}
}

bool NativeHuntsManager::AdvanceRelicPoolGeneration(
		RelicPoolGenerationJob &job, std::uint32_t budget) {
	RelicPool &pool = job.Pool;
	Map *map = sMapMgr->FindMap(pool.Scope.MapId, pool.Scope.InstanceId);
	if (!map && pool.Scope.InstanceId == 0)
		map = sMapMgr->CreateBaseMap(pool.Scope.MapId);
	if (!map || pool.RequiredCount == 0)
		return false;
	auto *navQuery = map->GetMapCollisionData().GetMMapData().GetNavMeshQuery();
	if (!navQuery) {
		LOG_ERROR("module.native_hunts",
			"Cannot generate relic pool {} for map {} zone {}: MMap navigation "
			"data is unavailable.", pool.Id, pool.Scope.MapId, pool.Scope.ZoneId);
		return false;
	}
	dtQueryFilter filter;
	filter.setIncludeFlags(0xffff);
	filter.setExcludeFlags(0);
	auto const placement = AdvanceRelicPlacementAttempts(pool.RequiredCount,
		static_cast<std::uint32_t>(pool.Positions.size()), job.Attempts, budget,
		[&]() {
		float x = frand(0.0f, 100.0f);
		float y = frand(0.0f, 100.0f);
		Zone2MapCoordinates(x, y, pool.Scope.ZoneId);
		map->LoadGrid(x, y);
		float const terrain = map->GetHeight(x, y, MAX_HEIGHT, false);
		if (terrain <= INVALID_HEIGHT)
			return false;
		float point[3] = {y, terrain + 0.5f, x};
		float extents[3] = {3.0f, 100.0f, 3.0f};
		float closest[3] = {};
		dtPolyRef polygon = 0;
		if (dtStatusFailed(navQuery->findNearestPoly(point, extents, &filter,
				&polygon, closest)) || polygon == 0)
			return false;
		float const candidateX = closest[2];
		float const candidateY = closest[0];
		float const candidateZ = closest[1] + 0.5f;
		if (std::hypot(candidateX - x, candidateY - y) > 3.0f ||
			!MapMgr::IsValidMapCoord(pool.Scope.MapId, candidateX, candidateY,
				candidateZ) ||
			map->GetZoneId(PHASEMASK_NORMAL, candidateX, candidateY,
				candidateZ) != pool.Scope.ZoneId)
			return false;
		pool.Positions.push_back({
			static_cast<std::uint32_t>(pool.Positions.size()), candidateX,
			candidateY, candidateZ, frand(0.0f, 6.2831853f), {}});
		return true;
	});
	job.Attempts = placement.Attempts;
	job.PlacementComplete = placement.Complete;
	if (placement.Exhausted) {
		LOG_ERROR("module.native_hunts",
			"Relic pool {} generated only {} of {} valid MMap-backed positions "
			"after {} attempts; no partial pool will be persisted or spawned.",
			pool.Id, placement.Accepted, pool.RequiredCount, placement.Attempts);
		pool.Positions.clear();
		return false;
	}
	return true;
}

void NativeHuntsManager::SpawnRelicPool(RelicPool &pool) {
	if (!_relicPoolsReady || pool.Positions.size() != pool.RequiredCount)
		return;
	Map *map = sMapMgr->FindMap(pool.Scope.MapId, pool.Scope.InstanceId);
	if (!map && pool.Scope.InstanceId == 0)
		map = sMapMgr->CreateBaseMap(pool.Scope.MapId);
	if (!map)
		return;
	std::size_t spawned = 0;
	for (auto &position : pool.Positions) {
		GameObject *relic = position.Guid.IsEmpty()
			? nullptr : map->GetGameObject(position.Guid);
		if (!relic) {
			position.Guid.Clear();
			float const halfOrientation = position.Orientation * 0.5f;
			relic = map->SummonGameObject(_config.RelicGameObjectEntry,
				position.X, position.Y, position.Z, position.Orientation,
				0.0f, 0.0f, std::sin(halfOrientation),
				std::cos(halfOrientation), 0, false);
			if (relic) {
				relic->SetGoState(GO_STATE_READY);
				relic->RemoveGameObjectFlag(GO_FLAG_IN_USE | GO_FLAG_LOCKED |
					GO_FLAG_INTERACT_COND | GO_FLAG_NOT_SELECTABLE);
				position.Guid = relic->GetGUID();
			}
		}
		if (relic)
			++spawned;
	}
	if (spawned != pool.RequiredCount && !pool.SpawnShortfallReported) {
		LOG_ERROR("module.native_hunts",
			"Relic pool {} spawned {} of {} objects on map {} instance {}; "
			"the missing objects will be retried during reconciliation.", pool.Id,
			spawned, pool.RequiredCount, pool.Scope.MapId, pool.Scope.InstanceId);
		pool.SpawnShortfallReported = true;
	} else if (spawned == pool.RequiredCount)
		pool.SpawnShortfallReported = false;
}

RelicPersistenceObservation NativeHuntsManager::LoadPersistedRelicPool(
		RelicPoolScope const &scope, RelicPool &pool) const {
	QueryResult summary = CharacterDatabase.Query(
		"SELECT COUNT(*),COALESCE(MAX(`pool_id`),''),"
		"COALESCE(MAX(`relic_count`),0) FROM `native_hunt_relic_pool` WHERE "
		"`map_id`={} AND `instance_id`={} AND `zone_id`={} AND "
		"`retired_at` IS NULL", scope.MapId, scope.InstanceId, scope.ZoneId);
	if (!summary)
		return RelicPersistenceObservation::Unavailable;
	Field *summaryField = summary->Fetch();
	if (summaryField[0].Get<std::uint64_t>() == 0)
		return RelicPersistenceObservation::Missing;
	pool = {};
	pool.Id = summaryField[1].Get<std::string>();
	pool.Scope = scope;
	pool.RequiredCount = summaryField[2].Get<std::uint32_t>();
	if (!IsUuid(pool.Id) || pool.RequiredCount == 0 ||
			pool.RequiredCount > MaximumRelicsPerZone)
		return RelicPersistenceObservation::Invalid;
	QueryResult count = CharacterDatabase.Query(
		"SELECT COUNT(*) FROM `native_hunt_relic_position` WHERE `pool_id`='{}'",
		pool.Id);
	if (!count)
		return RelicPersistenceObservation::Unavailable;
	if (count->Fetch()[0].Get<std::uint64_t>() != pool.RequiredCount)
		return RelicPersistenceObservation::Invalid;
	QueryResult positions = CharacterDatabase.Query(
		"SELECT `relic_index`,`position_x`,`position_y`,`position_z`,"
		"`orientation` FROM `native_hunt_relic_position` WHERE `pool_id`='{}' "
		"ORDER BY `relic_index`", pool.Id);
	if (!positions)
		return RelicPersistenceObservation::Unavailable;
	bool valid = true;
	do {
		Field *field = positions->Fetch();
		RelicPosition position{field[0].Get<std::uint32_t>(),
			field[1].Get<float>(), field[2].Get<float>(), field[3].Get<float>(),
			field[4].Get<float>(), {}};
		valid = valid && position.Index == pool.Positions.size() &&
			std::isfinite(position.X) && std::isfinite(position.Y) &&
			std::isfinite(position.Z) && std::isfinite(position.Orientation) &&
			MapMgr::IsValidMapCoord(scope.MapId, position.X, position.Y,
				position.Z, position.Orientation);
		pool.Positions.push_back(position);
	} while (positions->NextRow());
	return valid && pool.Positions.size() == pool.RequiredCount
		? RelicPersistenceObservation::Complete
		: RelicPersistenceObservation::Invalid;
}

std::size_t NativeHuntsManager::CountTrackingHuntsForRelicScope(
		RelicPoolScope const &scope) const {
	std::size_t count = 0;
	for (auto const &[guid, runtime] : _runtimes) {
		if (runtime.Aggregate.State != HuntState::Tracking ||
			runtime.MapId != scope.MapId || runtime.ZoneId != scope.ZoneId)
			continue;
		if (scope.InstanceId == 0) {
			auto const *entry = sMapStore.LookupEntry(runtime.MapId);
			if (entry && !entry->Instanceable())
				++count;
			continue;
		}
		Player *player = ConnectedPlayer(guid);
		if (player && player->GetMapId() == scope.MapId &&
				player->GetZoneId() == scope.ZoneId &&
				player->GetMap()->GetInstanceId() == scope.InstanceId)
			++count;
	}
	return count;
}

void NativeHuntsManager::AssociateTrackingHuntsWithRelicPool(RelicPool &pool) {
	for (auto &[guid, runtime] : _runtimes) {
		if (runtime.Aggregate.State != HuntState::Tracking ||
			runtime.MapId != pool.Scope.MapId ||
			runtime.ZoneId != pool.Scope.ZoneId ||
			!runtime.RelicPoolId.empty())
			continue;
		if (pool.Scope.InstanceId != 0) {
			Player *player = ConnectedPlayer(guid);
			if (!player || player->GetMapId() != pool.Scope.MapId ||
					player->GetZoneId() != pool.Scope.ZoneId ||
					player->GetMap()->GetInstanceId() != pool.Scope.InstanceId)
				continue;
		}
		CharacterDatabase.DirectExecute(
			"UPDATE `native_hunt_assignment` SET `relic_pool_id`='{}' WHERE "
			"`character_guid`={} AND `hunt_instance_id`='{}' AND "
			"(`relic_pool_id` IS NULL OR `relic_pool_id`='')", pool.Id,
			runtime.CharacterGuid, runtime.HuntInstanceId);
		if (CharacterDatabase.Query(
				"SELECT 1 FROM `native_hunt_assignment` WHERE `character_guid`={} "
				"AND `hunt_instance_id`='{}' AND `relic_pool_id`='{}'",
				runtime.CharacterGuid, runtime.HuntInstanceId, pool.Id))
			runtime.RelicPoolId = pool.Id;
	}
}

bool NativeHuntsManager::QueueRelicPoolGeneration(RelicPoolScope const &scope) {
	if (_relicPoolGenerationJobs.count(scope))
		return true;
	auto const poolId = NewHuntInstanceId();
	if (!poolId)
		return false;
	RelicPoolGenerationJob job;
	job.Pool = {*poolId, scope, _config.RelicsCountPerZone, {}};
	_relicPoolGenerationJobs.emplace(scope, std::move(job));
	_relicPoolGenerationOrder.push_back(scope);
	return true;
}

bool NativeHuntsManager::PersistRelicPoolGeneration(
		RelicPoolGenerationJob &job) {
	auto activate = [&](RelicPool recovered) {
		std::string const recoveredId = recovered.Id;
		auto [iterator, inserted] = _relicPools.emplace(
			recoveredId, std::move(recovered));
		RelicPool &active = iterator->second;
		if (!inserted && !(active.Scope == job.Pool.Scope))
			return false;
		AssociateTrackingHuntsWithRelicPool(active);
		SpawnRelicPool(active);
		return true;
	};
	RelicPool recovered;
	auto observation = LoadPersistedRelicPool(job.Pool.Scope, recovered);
	auto action = DecideRelicPersistenceAction(observation,
		job.PlacementComplete);
	if (action == RelicPersistenceAction::RecoverPersisted)
		return activate(std::move(recovered));
	if (action == RelicPersistenceAction::Wait)
		return false;
	if (action == RelicPersistenceAction::ReplaceInvalid) {
		LOG_ERROR("module.native_hunts",
			"Persisted relic pool {} for map {} instance {} zone {} is invalid; "
			"it will be retired before the completed generation job is retried.",
			recovered.Id, job.Pool.Scope.MapId, job.Pool.Scope.InstanceId,
			job.Pool.Scope.ZoneId);
		RetireRelicPool(recovered.Id);
		return false;
	}

	HuntRuntime *owner = nullptr;
	for (auto &[guid, runtime] : _runtimes) {
		(void)guid;
		if (runtime.Aggregate.State != HuntState::Tracking ||
			runtime.MapId != job.Pool.Scope.MapId ||
			runtime.ZoneId != job.Pool.Scope.ZoneId)
			continue;
		if (job.Pool.Scope.InstanceId != 0) {
			Player *player = ConnectedPlayer(runtime.CharacterGuid);
			if (!player || player->GetMapId() != job.Pool.Scope.MapId ||
					player->GetZoneId() != job.Pool.Scope.ZoneId ||
					player->GetMap()->GetInstanceId() !=
						job.Pool.Scope.InstanceId)
				continue;
		}
		owner = &runtime;
		break;
	}
	if (!owner)
		return false;
	auto transaction = CharacterDatabase.BeginTransaction();
	transaction->Append(
		"INSERT INTO `native_hunt_relic_pool` (`pool_id`,`map_id`,"
		"`instance_id`,`zone_id`,`relic_count`) VALUES ('{}',{},{},{},{})",
		job.Pool.Id, job.Pool.Scope.MapId, job.Pool.Scope.InstanceId,
		job.Pool.Scope.ZoneId, job.Pool.RequiredCount);
	for (auto const &position : job.Pool.Positions)
		transaction->Append(
			"INSERT INTO `native_hunt_relic_position` (`pool_id`,"
			"`relic_index`,`position_x`,`position_y`,`position_z`,"
			"`orientation`) VALUES ('{}',{},{},{},{},{})", job.Pool.Id,
			position.Index, position.X, position.Y, position.Z,
			position.Orientation);
	transaction->Append(
		"UPDATE `native_hunt_assignment` SET `relic_pool_id`='{}' WHERE "
		"`character_guid`={} AND `hunt_instance_id`='{}' AND "
		"(`relic_pool_id` IS NULL OR `relic_pool_id`='')", job.Pool.Id,
		owner->CharacterGuid, owner->HuntInstanceId);
	bool const acknowledged = CommitCharacterTransactionAndWait(transaction);
	recovered = {};
	observation = LoadPersistedRelicPool(job.Pool.Scope, recovered);
	if (observation == RelicPersistenceObservation::Complete) {
		if (!acknowledged)
			LOG_WARN("module.native_hunts",
				"Relic pool {} commit acknowledgement was lost, but the complete "
				"scope pool was recovered.", recovered.Id);
		return activate(std::move(recovered));
	}
	LOG_WARN("module.native_hunts",
		"Relic pool {} persistence could not be verified after commit; the "
		"completed generation job will be retained and verified before retry.",
		job.Pool.Id);
	return false;
}

void NativeHuntsManager::ProcessRelicPoolGenerationJobs() {
	std::uint32_t remaining = RelicPlacementCandidatesPerUpdate;
	std::size_t const scheduled = _relicPoolGenerationOrder.size();
	for (std::size_t index = 0; index < scheduled && remaining > 0 &&
			!_relicPoolGenerationOrder.empty(); ++index) {
		RelicPoolScope const scope = _relicPoolGenerationOrder.front();
		_relicPoolGenerationOrder.pop_front();
		auto found = _relicPoolGenerationJobs.find(scope);
		if (found == _relicPoolGenerationJobs.end())
			continue;
		if (ShouldCancelRelicGeneration(_config.RelicsCountPerZone > 0,
				CountTrackingHuntsForRelicScope(scope))) {
			RelicPool persisted;
			auto const observation = LoadPersistedRelicPool(scope, persisted);
			if (observation == RelicPersistenceObservation::Complete ||
					observation == RelicPersistenceObservation::Invalid)
				RetireRelicPool(persisted.Id);
			_relicPoolGenerationJobs.erase(found);
			_relicPoolRetryAfter.erase(scope);
			continue;
		}
		auto const now = std::chrono::steady_clock::now();
		if (auto retry = _relicPoolRetryAfter.find(scope);
				retry != _relicPoolRetryAfter.end() && now < retry->second) {
			_relicPoolGenerationOrder.push_back(scope);
			continue;
		}
		RelicPoolGenerationJob &job = found->second;
		bool keep = true;
		if (!job.PlacementComplete) {
			std::uint32_t const slice = std::min(
				RelicPlacementCandidatesPerJob, remaining);
			std::uint32_t const before = job.Attempts;
			keep = AdvanceRelicPoolGeneration(job, slice);
			remaining -= job.Attempts - before;
		}
		if (keep && job.PlacementComplete && PersistRelicPoolGeneration(job)) {
			_relicPoolGenerationJobs.erase(found);
			_relicPoolRetryAfter.erase(scope);
			continue;
		}
		if (!keep || job.PlacementComplete)
			_relicPoolRetryAfter[scope] = now + std::chrono::minutes(1);
		if (!keep) {
			_relicPoolGenerationJobs.erase(found);
			continue;
		}
		_relicPoolGenerationOrder.push_back(scope);
	}
}

bool NativeHuntsManager::AssociateRelicPool(Player *player,
		HuntRuntime &runtime) {
	if (!_relicPoolsReady || runtime.Aggregate.State != HuntState::Tracking)
		return false;
	bool const playerInAssignedZone = player &&
		player->GetMapId() == runtime.MapId &&
		player->GetZoneId() == runtime.ZoneId;
	if (!runtime.RelicPoolId.empty()) {
		auto existing = _relicPools.find(runtime.RelicPoolId);
		if (existing != _relicPools.end()) {
			if (!playerInAssignedZone ||
				existing->second.Scope.InstanceId ==
					player->GetMap()->GetInstanceId())
				return true;
			// The Hunt moved into a different live instance. Release its old
			// association; reconciliation retires that pool if this was its last
			// resumable Hunt and creates/reuses the correctly scoped pool below.
			CharacterDatabase.DirectExecute(
				"UPDATE `native_hunt_assignment` SET `relic_pool_id`=NULL WHERE "
				"`character_guid`={} AND `hunt_instance_id`='{}'",
				runtime.CharacterGuid, runtime.HuntInstanceId);
		}
		runtime.RelicPoolId.clear();
	}
	RelicPoolScope scope{runtime.MapId, 0, runtime.ZoneId};
	if (playerInAssignedZone)
		scope.InstanceId = player->GetMap()->GetInstanceId();
	else {
		auto const *entry = sMapStore.LookupEntry(runtime.MapId);
		if (!entry || entry->Instanceable())
			return false;
	}
	for (auto &[id, pool] : _relicPools) {
		if (!(pool.Scope == scope))
			continue;
		CharacterDatabase.DirectExecute(
			"UPDATE `native_hunt_assignment` SET `relic_pool_id`='{}' WHERE "
			"`character_guid`={} AND `hunt_instance_id`='{}'", id,
			runtime.CharacterGuid, runtime.HuntInstanceId);
		if (!CharacterDatabase.Query(
				"SELECT 1 FROM `native_hunt_assignment` WHERE `character_guid`={} "
				"AND `hunt_instance_id`='{}' AND `relic_pool_id`='{}'",
				runtime.CharacterGuid, runtime.HuntInstanceId, id))
			return false;
		runtime.RelicPoolId = id;
		return true;
	}
	auto const now = std::chrono::steady_clock::now();
	auto retry = _relicPoolRetryAfter.find(scope);
	bool const retryScheduled = retry != _relicPoolRetryAfter.end();
	bool const retryDue = !retryScheduled || now >= retry->second;
	auto const request = DecideRelicGenerationRequest(false,
		_relicPoolGenerationJobs.count(scope) != 0, retryScheduled, retryDue);
	if (request == RelicGenerationRequest::JoinPending)
		return false;
	if (request == RelicGenerationRequest::WaitForRetry)
		return false;
	RelicPool persisted;
	auto const observation = LoadPersistedRelicPool(scope, persisted);
	if (observation == RelicPersistenceObservation::Complete) {
		std::string const persistedId = persisted.Id;
		auto [iterator, inserted] = _relicPools.emplace(
			persistedId, std::move(persisted));
		if (!inserted && !(iterator->second.Scope == scope))
			return false;
		AssociateTrackingHuntsWithRelicPool(iterator->second);
		SpawnRelicPool(iterator->second);
		_relicPoolRetryAfter.erase(scope);
		return runtime.RelicPoolId == iterator->second.Id;
	}
	if (observation == RelicPersistenceObservation::Invalid) {
		RetireRelicPool(persisted.Id);
	} else if (observation == RelicPersistenceObservation::Unavailable) {
		_relicPoolRetryAfter[scope] = now + std::chrono::minutes(1);
		return false;
	}
	if (!QueueRelicPoolGeneration(scope))
		_relicPoolRetryAfter[scope] = now + std::chrono::minutes(1);
	return false;
}

void NativeHuntsManager::RetireRelicPool(std::string const &poolId) {
	auto found = _relicPools.find(poolId);
	if (found != _relicPools.end())
		RemoveRelicPoolObjects(found->second);
	auto transaction = CharacterDatabase.BeginTransaction();
	transaction->Append(
		"UPDATE `native_hunt_assignment` SET `relic_pool_id`=NULL WHERE "
		"`relic_pool_id`='{}'", poolId);
	transaction->Append(
		"DELETE FROM `native_hunt_relic_position` WHERE `pool_id`='{}'",
		poolId);
	transaction->Append(
		"DELETE FROM `native_hunt_relic_pool` WHERE `pool_id`='{}'", poolId);
	CommitCharacterTransactionAndWait(transaction);
	_relicPools.erase(poolId);
}

void NativeHuntsManager::ReconcileRelicPools() {
	if (_config.RelicsCountPerZone == 0) {
		RemoveAllRelicPoolObjects();
		_relicPools.clear();
		_relicPoolGenerationJobs.clear();
		_relicPoolGenerationOrder.clear();
		_relicPoolRetryAfter.clear();
		for (auto &[guid, runtime] : _runtimes) {
			(void)guid;
			runtime.RelicPoolId.clear();
		}
		if (!_relicDisableCleanupComplete) {
			CharacterDatabase.DirectExecute(
				"UPDATE `native_hunt_assignment` SET `relic_pool_id`=NULL WHERE "
				"`relic_pool_id` IS NOT NULL");
			CharacterDatabase.DirectExecute(
				"DELETE FROM `native_hunt_relic_position`");
			CharacterDatabase.DirectExecute(
				"DELETE FROM `native_hunt_relic_pool`");
			_relicDisableCleanupComplete = true;
		}
		return;
	}
	// A transient managed-resource failure makes gameplay unavailable but must
	// not erase a logically resumable pool. CountPerZone=0 is the only setting
	// that explicitly authorizes retirement while assignments still exist.
	if (!IsEnabled()) {
		RemoveAllRelicPoolObjects();
		_relicPoolGenerationJobs.clear();
		_relicPoolGenerationOrder.clear();
		return;
	}
	_relicDisableCleanupComplete = false;
	if (!_relicPoolsReady)
		InitializeRelicPools();
	if (!_relicPoolsReady)
		return;
	for (auto &[guid, runtime] : _runtimes)
		if (runtime.Aggregate.State == HuntState::Tracking)
			AssociateRelicPool(ConnectedPlayer(guid), runtime);
	ProcessRelicPoolGenerationJobs();
	std::unordered_map<std::string, std::size_t> references;
	for (auto const &[guid, runtime] : _runtimes) {
		(void)guid;
		if (!runtime.RelicPoolId.empty())
			++references[runtime.RelicPoolId];
	}
	std::vector<std::string> retire;
	for (auto &[id, pool] : _relicPools) {
		if (ShouldRetireRelicPool(references[id]))
			retire.push_back(id);
		else
			SpawnRelicPool(pool);
	}
	for (auto const &id : retire)
		RetireRelicPool(id);
}

RelicPool *NativeHuntsManager::FindRelicPool(GameObject const *object,
		std::uint32_t *relicIndex) {
	if (!object)
		return nullptr;
	for (auto &[id, pool] : _relicPools) {
		(void)id;
		for (auto const &position : pool.Positions)
			if (!position.Guid.IsEmpty() && position.Guid == object->GetGUID()) {
				if (relicIndex)
					*relicIndex = position.Index;
				return &pool;
			}
	}
	return nullptr;
}

void NativeHuntsManager::ReconcileHuntAura(Player *player,
		HuntAggregate const *assignment) {
	if (!player)
		return;
	HuntState const state = assignment ? assignment->State : HuntState::Idle;
	PreyTier const tier = assignment ? assignment->Identity.Tier
		: PreyTier::Standard;
	bool const hasStandard = _resources.StandardHuntAuraSpell &&
		player->HasAura(_resources.StandardHuntAuraSpell);
	bool const hasElite = _resources.EliteHuntAuraSpell &&
		player->HasAura(_resources.EliteHuntAuraSpell);
	auto const decision = DecideHuntAura(state, tier,
		_resources.StandardHuntAuraSpell, _resources.EliteHuntAuraSpell,
		hasStandard, hasElite);
	if (decision.RemoveStandard)
		player->RemoveAurasDueToSpell(_resources.StandardHuntAuraSpell);
	if (decision.RemoveElite)
		player->RemoveAurasDueToSpell(_resources.EliteHuntAuraSpell);
	if (decision.AddSpell && !player->AddAura(decision.AddSpell, player))
		LOG_ERROR("module.native_hunts",
			"Native Hunts could not apply managed informational aura {} to "
			"character {}; authoritative Hunt state is unchanged.",
			decision.AddSpell, player->GetGUID().GetCounter());
}

HuntmasterDefinition const *NativeHuntsManager::ResolveHuntmaster(
		Creature const *creature) const {
	if (!creature)
		return nullptr;
	for (auto const &huntmaster : Huntmasters()) {
		auto const entry = _resources.CreatureEntries.find(huntmaster.Symbol);
		auto const spawn = _resources.CreatureSpawns.find(huntmaster.SpawnSymbol);
		if (entry != _resources.CreatureEntries.end() &&
			spawn != _resources.CreatureSpawns.end() &&
			MatchesManagedHuntmaster(creature->GetEntry(), creature->GetSpawnId(),
				entry->second, spawn->second))
			return &huntmaster;
	}
	return nullptr;
}

bool NativeHuntsManager::IsHuntmaster(Creature const *creature) const {
	return ResolveHuntmaster(creature) != nullptr;
}

bool NativeHuntsManager::CanOpenHuntmasterGossip(Player const *player,
		Creature const *creature) const {
	if (IsHuntmaster(creature))
		return true;
	auto const *runtime = GetRuntime(player);
	return runtime && creature && CanUseIssuingHuntmaster(
		runtime->Aggregate.State, creature->GetEntry(), creature->GetSpawnId(),
		runtime->HuntmasterEntry, runtime->HuntmasterSpawn);
}

bool NativeHuntsManager::IsElitePrey(std::uint32_t entry) const {
	for (auto const &prey : ElitePrey()) {
		auto found=_resources.CreatureEntries.find(prey.Symbol);
		if (found!=_resources.CreatureEntries.end() && found->second==entry) return true;
	}
	return false;
}

bool NativeHuntsManager::IsTrailCrystal(std::uint32_t entry) const {
	return _resources.Ready && entry == _resources.TrailCrystalEntry;
}

bool NativeHuntsManager::IsReturnRift(std::uint32_t entry) const {
	return _resources.Ready && entry == _resources.ReturnRiftEntry;
}

HuntRuntime const *NativeHuntsManager::GetRuntime(Player const *player) const {
	if (!player)
		return nullptr;
	auto const it = _runtimes.find(player->GetGUID().GetCounter());
	return it == _runtimes.end() ? nullptr : &it->second;
}

bool NativeHuntsManager::RequestHunt(Player *player, Creature *giver,
									 std::string &message) {
	if (!IsEnabled()) {
		message = "Native Hunts content is unavailable: " + _resources.Reason;
		return false;
	}
	auto const *huntmaster = ResolveHuntmaster(giver);
	if (!player || !huntmaster) {
		message = "That creature is not an active Huntmaster.";
		return false;
	}
	if (player->GetLevel() < _config.MinimumLevel) {
		message = "You are not experienced enough to begin a Hunt.";
		return false;
	}
	std::uint32_t const guid = player->GetGUID().GetCounter();
	if (_runtimes.count(guid)) {
		message = "You already have an active Hunt.";
		return false;
	}

	auto const eligibleZones = EligibleZonesForAssignment(
		player->GetLevel(), _config.SearchScope, *huntmaster);
	if (eligibleZones.empty()) {
		message = "No authored hunting ground is suitable for your level and "
				  "the configured assignment scope.";
		return false;
	}
	auto const &prey = StandardPrey()[urand(
		0, static_cast<std::uint32_t>(StandardPrey().size() - 1))];
	auto const *site = eligibleZones[urand(
		0, static_cast<std::uint32_t>(eligibleZones.size() - 1))];

	HuntRuntime runtime;
	runtime.CharacterGuid = guid;
	auto const instanceId = NewHuntInstanceId();
	if (!instanceId) {
		message = "The Hunt database could not create a new assignment identity.";
		return false;
	}
	runtime.HuntInstanceId = *instanceId;
	runtime.HuntmasterEntry = giver->GetEntry();
	runtime.HuntmasterSpawn = giver->GetSpawnId();
	runtime.ZoneId = site->ZoneId;
	runtime.MapId = site->MapId;
	HuntIdentity identity{huntmaster->Key, huntmaster->Name, huntmaster->City,
						  prey.Key, prey.Name, PreyTier::Standard,
						  site->ZoneKey, site->ZoneName};
	if (HuntDomain::Execute(runtime.Aggregate, AcceptHunt{identity}).Status !=
		TransitionStatus::Applied) {
		message = "The Hunt assignment could not be created.";
		return false;
	}
	if (!CreateAssignment(runtime)) {
		message = "The Hunt assignment could not be persisted.";
		return false;
	}
	_runtimes.emplace(guid, runtime);
	AssociateRelicPool(player, _runtimes.at(guid));
	ReconcileHuntAura(player, &_runtimes.at(guid).Aggregate);
	QueueUiState(player);
	message = "Your quarry is " + std::string(prey.Name) + ". Travel to " +
			  site->ZoneName + " and hunt suitable creatures to find its trail.";
	return true;
}

bool NativeHuntsManager::IsEliteUnlocked(Player const *player) const {
	if (!player)
		return false;
	QueryResult result = CharacterDatabase.Query(
		"SELECT `standard_completed` FROM `native_hunt_stats` WHERE "
		"`character_guid`={}", player->GetGUID().GetCounter());
	return EliteUnlocked(result ? result->Fetch()[0].Get<std::uint32_t>() : 0,
		_config.EliteRequiredStandardCompletions);
}

bool NativeHuntsManager::IsEliteAvailableToday(Player const *player) const {
	if (!player)
		return false;
	QueryResult result = CharacterDatabase.Query(
		"SELECT IF(`elite_daily_accept_reset_date`=CURRENT_DATE(),"
		"`elite_daily_accepted`,0) FROM `native_hunt_stats` WHERE "
		"`character_guid`={}", player->GetGUID().GetCounter());
	return EliteAvailable(result ? result->Fetch()[0].Get<std::uint32_t>() : 0,
		_config.EliteDailyLimit);
}

bool NativeHuntsManager::RequestEliteHunt(Player *player, Creature *giver,
		std::string &message) {
	auto const *huntmaster = ResolveHuntmaster(giver);
	if (!IsEnabled() || !player || !huntmaster) {
		message = "Elite Hunts are unavailable.";
		return false;
	}
	if (player->GetLevel() < _config.MinimumLevel || !IsEliteUnlocked(player)) {
		message = "Elite Hunts unlock after " +
			std::to_string(_config.EliteRequiredStandardCompletions) +
			" completed Standard Hunts.";
		return false;
	}
	if (!IsEliteAvailableToday(player)) {
		message = "You have already accepted your Elite assignment today.";
		return false;
	}
	std::uint32_t const guid = player->GetGUID().GetCounter();
	if (_runtimes.count(guid)) {
		message = "You already have an active Hunt.";
		return false;
	}
	auto zones = EligibleZonesForAssignment(player->GetLevel(),
		_config.SearchScope, *huntmaster);
	std::vector<PreyDefinition const *> prey;
	for (auto const &candidate : ElitePrey())
		if (player->GetLevel() >= candidate.MinLevel &&
			player->GetLevel() <= candidate.MaxLevel)
			prey.push_back(&candidate);
	if (zones.empty() || prey.empty()) {
		message = "No suitable Elite assignment is available.";
		return false;
	}
	auto const *chosen = prey[urand(0, prey.size() - 1)];
	auto const *site = zones[urand(0, zones.size() - 1)];
	HuntRuntime runtime;
	runtime.CharacterGuid = guid;
	auto const instanceId = NewHuntInstanceId();
	if (!instanceId) {
		message = "The Hunt database could not create a new assignment identity.";
		return false;
	}
	runtime.HuntInstanceId = *instanceId;
	runtime.HuntmasterEntry = giver->GetEntry();
	runtime.HuntmasterSpawn = giver->GetSpawnId();
	runtime.ZoneId = site->ZoneId;
	runtime.MapId = site->MapId;
	HuntIdentity identity{huntmaster->Key, huntmaster->Name, huntmaster->City,
		chosen->Key, chosen->Name, PreyTier::Elite, site->ZoneKey, site->ZoneName};
	if (HuntDomain::Execute(runtime.Aggregate, AcceptHunt{identity}).Status !=
		TransitionStatus::Applied) {
		message = "The Elite assignment could not be created.";
		return false;
	}
	// Acceptance, not completion, consumes the daily allowance. This is the
	// reference anti-reroll rule: abandoning forfeits today's assignment.
	if (!CreateAssignment(runtime)) {
		message = "The Elite assignment could not be persisted.";
		return false;
	}
	CharacterDatabase.DirectExecute(
		"INSERT INTO `native_hunt_stats` (`character_guid`,"
		"`elite_daily_accepted`,`elite_daily_accept_reset_date`) VALUES "
		"({},1,CURRENT_DATE()) ON DUPLICATE KEY UPDATE "
		"`elite_daily_accepted`=IF(`elite_daily_accept_reset_date`="
		"CURRENT_DATE(),`elite_daily_accepted`+1,1),"
		"`elite_daily_accept_reset_date`=CURRENT_DATE()", guid);
	_runtimes.emplace(guid, runtime);
	AssociateRelicPool(player, _runtimes.at(guid));
	ReconcileHuntAura(player, &_runtimes.at(guid).Aggregate);
	QueueUiState(player);
	message = "Elite quarry: " + std::string(chosen->Name) + ". Travel to " +
		site->ZoneName + ". Abandoning forfeits today's Elite assignment.";
	return true;
}

bool NativeHuntsManager::IsGuardLocator(std::uint32_t entry) const {
	return _guardLocators.count(entry) != 0;
}

bool NativeHuntsManager::SendHuntmasterLocation(Player *player,
		std::uint32_t guardEntry, std::string &message) const {
	auto locator = _guardLocators.find(guardEntry);
	if (!player || locator == _guardLocators.end()) {
		message = "That guard does not know a Huntmaster location.";
		return false;
	}
	auto const *definition = FindHuntmaster(locator->second);
	if (!definition) return false;
	auto spawnIt = _resources.CreatureSpawns.find(definition->SpawnSymbol);
	if (spawnIt == _resources.CreatureSpawns.end()) return false;
	auto const *spawn = sObjectMgr->GetCreatureData(spawnIt->second);
	if (!spawn || player->GetMapId() != spawn->mapid) {
		message = "The Huntmaster is not on this map.";
		return false;
	}
	WorldPacket poi(SMSG_GOSSIP_POI, 64);
	poi << std::uint32_t(6) << spawn->posX << spawn->posY << std::uint32_t(7)
		<< std::uint32_t(0) << std::string("Huntmaster - ") + definition->City;
	player->GetSession()->SendPacket(&poi);
	message = std::string(definition->Name) + " has been marked on your map.";
	return true;
}

bool NativeHuntsManager::Abandon(Player *player, std::string &message) {
	if (!player) {
		message = "A player is required.";
		return false;
	}
	auto it = _runtimes.find(player->GetGUID().GetCounter());
	if (it == _runtimes.end()) {
		ReconcileHuntAura(player, nullptr);
		message = "You do not have an active Hunt.";
		return false;
	}
	HuntDomain::Execute(it->second.Aggregate, AbandonHunt{});
	DeleteAssignment(it->second);
	_runtimes.erase(it);
	ReconcileRelicPools();
	QueueUiState(player);
	message = "Your Hunt has been abandoned.";
	return true;
}

void NativeHuntsManager::SendFinalLocationFeedback(Player *player,
										HuntRuntime &runtime,
										bool includeMessage) {
	if (!player || runtime.Aggregate.State != HuntState::FinalRevealed)
		return;
	auto const *site = FindFinalSite(runtime.Aggregate.FinalLocationKey);
	if (!site)
		return;
	bool const sameMap = player->GetMapId() == site->MapId;
	if (sameMap) {
		WorldPacket poi(SMSG_GOSSIP_POI, 64);
		poi << std::uint32_t(6) << site->X << site->Y << std::uint32_t(7)
			<< std::uint32_t(0) << std::string("Prey Trail - ") + site->ZoneName;
		player->GetSession()->SendPacket(&poi);
	}
	if (includeMessage) {
		ChatHandler(player->GetSession()).PSendSysMessage(
			"|cff00ff00[Native Hunts]|r Tracking complete. {}'s trail has "
			"been located in {}. {} Travel to the revealed location and use "
			"the Prey Trail Crystal.",
			runtime.Aggregate.Identity.PreyName, site->ZoneName,
			sameMap ? "The location is marked on your map."
					: "A stock-client map pin is unavailable until you reach its map.");
		runtime.FinalRevealNotified = true;
	}
}

bool NativeHuntsManager::EnsureCrystal(Player *player, HuntRuntime &runtime) {
	if (!player || runtime.Aggregate.State != HuntState::FinalRevealed)
		return false;
	auto const *site = FindFinalSite(runtime.Aggregate.FinalLocationKey);
	if (!site || player->GetMapId() != site->MapId ||
		player->GetZoneId() != site->ZoneId)
		return false;
	float const dx = player->GetPositionX() - site->X;
	float const dy = player->GetPositionY() - site->Y;
	if (dx * dx + dy * dy > 180.0f * 180.0f)
		return false;
	if (!runtime.CrystalGuid.IsEmpty()) {
		if (GameObject *existing =
				ObjectAccessor::GetGameObject(*player, runtime.CrystalGuid))
			if (existing->IsInWorld())
				return true;
		runtime.CrystalGuid.Clear();
	}
	float z = site->Z;
	if (Map *map = player->GetMap()) {
		float const ground = map->GetHeight(site->X, site->Y, z + 10.0f, true, 50.0f);
		if (ground > INVALID_HEIGHT)
			z = ground + 0.15f;
	}
	GameObject *crystal = player->SummonGameObject(
		_resources.TrailCrystalEntry, site->X, site->Y, z, site->Orientation,
		0.0f, 0.0f, std::sin(site->Orientation * 0.5f),
		std::cos(site->Orientation * 0.5f), 3600);
	if (!crystal)
		return false;
	crystal->SetGoState(GO_STATE_READY);
	crystal->RemoveGameObjectFlag(GO_FLAG_IN_USE | GO_FLAG_LOCKED |
		GO_FLAG_INTERACT_COND | GO_FLAG_NOT_SELECTABLE);
	runtime.CrystalGuid = crystal->GetGUID();
	return true;
}

void NativeHuntsManager::InitializePreyCombat(Player *player,
								   HuntRuntime &runtime, Creature *prey,
								   bool finalEncounter) {
	auto const *definition = FindPrey(runtime.Aggregate.Identity.PreyKey);
	if (!player || !prey || !definition)
		return;
	prey->SetLevel(player->GetLevel());
	prey->UpdateAllStats();
	prey->SetFaction(14);
	prey->RemoveFlag(UNIT_FIELD_FLAGS,
		UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE |
			UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
	prey->SetReactState(REACT_AGGRESSIVE);
	bool const elite = definition->Tier == PreyTier::Elite;
	float const multiplier = std::max(1.0f,
		(finalEncounter ? definition->FinalHealthMultiplier *
			FinalLevelScale(player->GetLevel())
						: definition->AmbushHealthMultiplier *
							  AmbushLevelScale(player->GetLevel())));
	std::uint64_t const scaled = static_cast<std::uint64_t>(
		multiplier * definition->HealthModifier *
			(elite ? _config.EliteHealthMultiplier : 1.0f) *
			static_cast<float>(player->GetMaxHealth()));
	std::uint32_t const health = static_cast<std::uint32_t>(
		std::min<std::uint64_t>(std::numeric_limits<std::uint32_t>::max(),
								 scaled));
	prey->SetMaxHealth(health);
	prey->SetFullHealth();
	if (elite) {
		prey->SetArmor(static_cast<std::int32_t>(prey->GetArmor() *
			definition->ArmorModifier * _config.EliteArmorMultiplier));
		float const damage = definition->DamageModifier * _config.EliteDamageMultiplier;
		for (WeaponAttackType attack : {BASE_ATTACK, OFF_ATTACK, RANGED_ATTACK}) {
			prey->SetBaseWeaponDamage(attack, MINDAMAGE,
				prey->GetWeaponDamageRange(attack,MINDAMAGE) * damage);
			prey->SetBaseWeaponDamage(attack, MAXDAMAGE,
				prey->GetWeaponDamageRange(attack,MAXDAMAGE) * damage);
			prey->UpdateDamagePhysical(attack);
		}
		prey->SetMaxHealth(health);
		prey->SetFullHealth();
	}
	std::vector<PreyAbilityDefinition const *> abilities;
	auto const &catalog = elite ? ElitePreyAbilities() : StandardPreyAbilities();
	for (auto const &ability : catalog)
		if (runtime.Aggregate.Identity.PreyKey == ability.PreyKey)
			abilities.push_back(&ability);
	runtime.AbilityTimers.clear();
	runtime.AbilityUsed.assign(abilities.size(), false);
	for (auto const *ability : abilities)
		runtime.AbilityTimers.push_back(
			urand(ability->InitialMinMs, ability->InitialMaxMs));
	runtime.AbilityOneTimer = abilities.empty()
		? 0
		: urand(abilities[0]->InitialMinMs, abilities[0]->InitialMaxMs);
	runtime.AbilityTwoTimer = abilities.size() < 2
		? 0
		: urand(abilities[1]->InitialMinMs, abilities[1]->InitialMaxMs);
	prey->AI()->AttackStart(player);
	if (elite && definition->Ranged) {
		float minRange=std::max(5.0f,definition->PreferredRange*.70f);
		float maxRange=std::max(minRange+2.0f,definition->PreferredRange*1.10f);
		prey->GetMotionMaster()->MoveChase(player,ChaseRange(minRange,maxRange));
	}
	if (elite && definition->Key == std::string("wildclaw")) {
		prey->CastSpell(prey,768,true);
		runtime.DruidBearPhase=false;
	} else if (elite && definition->Key == std::string("dusk-confessor"))
		prey->CastSpell(prey,15473,true);
	if (elite && definition->Key == std::string("farstrider") &&
		runtime.CompanionGuid.IsEmpty()) {
		auto wolf=_resources.CreatureEntries.find("elite-farstrider-wolf");
		if (wolf!=_resources.CreatureEntries.end())
			if (TempSummon *companion=prey->SummonCreature(wolf->second,
				prey->GetNearPosition(3.0f,0.0f),TEMPSUMMON_TIMED_OR_DEAD_DESPAWN,300000)) {
				runtime.CompanionGuid=companion->GetGUID();
				companion->SetLevel(player->GetLevel()); companion->UpdateAllStats();
				companion->SetFaction(14); companion->AI()->AttackStart(player);
			}
	}
}

bool NativeHuntsManager::SpawnAmbush(Player *player, HuntRuntime &runtime,
								  std::string &message) {
	if (!player || runtime.Aggregate.State != HuntState::Tracking ||
		!runtime.AmbushPending || !runtime.AmbushGuid.IsEmpty() ||
		player->GetZoneId() != runtime.ZoneId) {
		message = "The pending ambush cannot begin here.";
		return false;
	}
	auto const *definition = FindPrey(runtime.Aggregate.Identity.PreyKey);
	auto const entry = definition
		? _resources.CreatureEntries.find(definition->Symbol)
		: _resources.CreatureEntries.end();
	if (!definition || entry == _resources.CreatureEntries.end()) {
		message = "The managed ambush creature is unavailable.";
		return false;
	}
	float const angle = frand(0.0f, 6.2831853f);
	float const distance = frand(7.0f, 11.0f);
	float const x = player->GetPositionX() + std::cos(angle) * distance;
	float const y = player->GetPositionY() + std::sin(angle) * distance;
	float z = player->GetPositionZ();
	if (Map *map = player->GetMap()) {
		float const ground = map->GetHeight(x, y, z + 10.0f, true, 50.0f);
		if (ground > INVALID_HEIGHT)
			z = ground + 0.5f;
	}
	TempSummon *ambush = player->SummonCreature(entry->second, x, y, z,
		player->GetOrientation(), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 300000);
	if (!ambush) {
		message = "The ambush could not be spawned; tracking remains paused.";
		return false;
	}
	runtime.AmbushGuid = ambush->GetGUID();
	InitializePreyCombat(player, runtime, ambush, false);
	message = runtime.Aggregate.Identity.PreyName +
		" has found you! Drive it off to continue tracking.";
	return true;
}

bool NativeHuntsManager::ReconcileTrackingMilestones(Player *player,
		HuntRuntime &runtime) {
	if (runtime.Aggregate.State != HuntState::Tracking)
		return false;

	switch (NextTrackingMilestone(runtime.Aggregate.Progress,
			runtime.AmbushesCompleted, _config.AmbushCount,
			runtime.AmbushPending)) {
	case TrackingMilestoneAction::StartAmbush: {
		// Pending is authoritative and must survive a failed spawn. Update will
		// keep retrying while the hunter remains in the assigned zone.
		runtime.AmbushPending = true;
		if (player) {
			std::string message;
			if (SpawnAmbush(player, runtime, message))
				ChatHandler(player->GetSession()).PSendSysMessage(
					"|cffff8000[Native Hunts]|r {}", message);
		}
		return true;
	}
	case TrackingMilestoneAction::RevealFinal: {
		auto const sites = FinalSitesForZone(runtime.Aggregate.Identity.ZoneKey);
		if (sites.empty())
			return false;
		auto const *site = sites[urand(
			0, static_cast<std::uint32_t>(sites.size() - 1))];
		if (HuntDomain::Execute(runtime.Aggregate,
				RevealFinal{site->Key, site->ZoneName}).Status !=
			TransitionStatus::Applied)
			return false;
		if (player)
			SendFinalLocationFeedback(player, runtime, true);
		return true;
	}
	case TrackingMilestoneAction::None:
		return false;
	}
	return false;
}

bool NativeHuntsManager::AwardTrackingProgress(Player *player,
		HuntRuntime &runtime, std::uint8_t amount) {
	HuntState const oldState = runtime.Aggregate.State;
	if (HuntDomain::Execute(runtime.Aggregate, AdvanceTracking{amount}).Status !=
		TransitionStatus::Applied)
		return false;
	ReconcileTrackingMilestones(player, runtime);
	SaveAssignment(runtime);
	if (runtime.Aggregate.State != oldState)
		ReconcileHuntAura(player, &runtime.Aggregate);
	QueueUiState(player);
	return true;
}

bool NativeHuntsManager::SpawnFinalPrey(Player *player, HuntRuntime &runtime,
										std::string &message) {
	auto const *preyDefinition = FindPrey(runtime.Aggregate.Identity.PreyKey);
	auto const *site = FindFinalSite(runtime.Aggregate.FinalLocationKey);
	if (!player || !preyDefinition || !site) {
		message = "The persisted Hunt encounter cannot be resolved.";
		return false;
	}
	auto const entryIt = _resources.CreatureEntries.find(preyDefinition->Symbol);
	if (entryIt == _resources.CreatureEntries.end()) {
		message = "The managed prey template is unavailable.";
		return false;
	}
	float angle = frand(0.0f, 6.2831853f);
	float distance = frand(7.0f, 11.0f);
	float x = player->GetPositionX() + std::cos(angle) * distance;
	float y = player->GetPositionY() + std::sin(angle) * distance;
	float z = player->GetPositionZ();
	if (Map *map = player->GetMap()) {
		float const ground = map->GetHeight(x, y, z + 10.0f, true, 50.0f);
		if (ground > INVALID_HEIGHT)
			z = ground + 0.5f;
	}
	TempSummon *prey = player->SummonCreature(
		entryIt->second, x, y, z, player->GetOrientation(),
		TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 300000);
	if (!prey) {
		message = "The prey could not be spawned; the trail remains usable.";
		return false;
	}
	auto const transition = ApplyPreySpawnOutcome(runtime.Aggregate, true);
	if (transition.Status != TransitionStatus::Applied) {
		prey->DespawnOrUnsummon();
		message = "The Hunt state rejected the final encounter.";
		return false;
	}
	runtime.PreyGuid = prey->GetGUID();
	InitializePreyCombat(player, runtime, prey, true);
	SaveAssignment(runtime);
	ReconcileHuntAura(player, &runtime.Aggregate);
	QueueUiState(player);
	message = runtime.Aggregate.Identity.PreyName +
			  " emerges for the final confrontation!";
	return true;
}

bool NativeHuntsManager::CollectRelic(Player *player,
		GameObject *object, std::string &message) {
	if (!player || !object) {
		message = "The relic cannot be examined.";
		return false;
	}
	auto runtimeIt = _runtimes.find(player->GetGUID().GetCounter());
	HuntRuntime *runtime = runtimeIt == _runtimes.end()
		? nullptr : &runtimeIt->second;
	std::uint32_t relicIndex = 0;
	RelicPool *pool = FindRelicPool(object, &relicIndex);
	RelicCollectionContext context{
		IsEnabled() && _config.RelicsCountPerZone > 0 && _relicPoolsReady,
		runtime != nullptr,
		runtime ? runtime->Aggregate.State : HuntState::Idle,
		pool && object->GetEntry() == _config.RelicGameObjectEntry && runtime &&
			runtime->RelicPoolId == pool->Id,
		runtime && pool && object->GetMap() == player->GetMap() &&
			runtime->MapId == pool->Scope.MapId &&
			player->GetMap()->GetInstanceId() == pool->Scope.InstanceId,
		runtime && pool && player->GetZoneId() == pool->Scope.ZoneId &&
			object->GetZoneId() == pool->Scope.ZoneId &&
			runtime->ZoneId == pool->Scope.ZoneId};
	switch (CanCollectRelic(context)) {
	case GameplayDecision::WrongState:
		message = "This relic no longer reveals anything about your active Hunt.";
		return false;
	case GameplayDecision::WrongLocation:
		message = "This relic does not belong to your assigned hunting ground.";
		return false;
	case GameplayDecision::Allowed:
		break;
	default:
		message = "This relic has no connection to an active Hunt.";
		return false;
	}
	std::string const relicKey = pool->Id + ":" +
		std::to_string(relicIndex);

	QueryResult duplicate = CharacterDatabase.Query(
		"SELECT assignment.`tracking_progress`,assignment.`revision` FROM "
		"`native_hunt_collection` collection JOIN `native_hunt_assignment` "
		"assignment ON assignment.`hunt_instance_id`=collection.`hunt_instance_id` "
		"WHERE collection.`hunt_instance_id`='{}' AND "
		"collection.`objective_key`='{}' AND collection.`relic_key`='{}'",
		runtime->HuntInstanceId, RelicObjectiveKey, relicKey);
	if (duplicate) {
		Field *field = duplicate->Fetch();
		std::uint8_t const persistedProgress = field[0].Get<std::uint8_t>();
		std::uint64_t const persistedRevision = field[1].Get<std::uint64_t>();
		if (runtime->Aggregate.Progress != persistedProgress ||
			runtime->Aggregate.Revision != persistedRevision) {
			HuntState const oldState = runtime->Aggregate.State;
			runtime->Aggregate.Progress = persistedProgress;
			runtime->Aggregate.Revision = persistedRevision;
			ReconcileTrackingMilestones(player, *runtime);
			SaveAssignment(*runtime);
			if (runtime->Aggregate.State != oldState)
				ReconcileHuntAura(player, &runtime->Aggregate);
			QueueUiState(player);
			ReconcileRelicPools();
		}
		message = "You have already examined this relic during this Hunt.";
		return false;
	}
	QueryResult countResult = CharacterDatabase.Query(
		"SELECT COUNT(*) FROM `native_hunt_collection` WHERE "
		"`hunt_instance_id`='{}' AND `objective_key`='{}'",
		runtime->HuntInstanceId, RelicObjectiveKey);
	if (!countResult) {
		message = "The relic's collection record is temporarily unavailable.";
		return false;
	}
	std::uint32_t const collected = static_cast<std::uint32_t>(
		countResult->Fetch()[0].Get<std::uint64_t>() + 1u);
	std::uint8_t const amount = RelicProgressIncrement(collected,
		_config.RelicProgressTarget);
	if (amount == 0) {
		message = "This objective cannot grant additional tracking progress.";
		return false;
	}

	std::uint8_t const oldProgress = runtime->Aggregate.Progress;
	std::uint64_t const oldRevision = runtime->Aggregate.Revision;
	auto transaction = CharacterDatabase.BeginTransaction();
	transaction->Append(
		"INSERT IGNORE INTO `native_hunt_collection` "
		"(`hunt_instance_id`,`objective_key`,`relic_key`,`tracking_award`) "
		"SELECT '{}','{}','{}',{} FROM `native_hunt_assignment` WHERE "
		"`character_guid`={} AND `hunt_instance_id`='{}' AND `state`={} "
		"AND `zone_id`={} AND `map_id`={} AND `revision`={} "
		"AND `tracking_progress`={}",
		runtime->HuntInstanceId, RelicObjectiveKey, relicKey,
		static_cast<std::uint32_t>(amount), runtime->CharacterGuid,
		runtime->HuntInstanceId,
		static_cast<std::uint32_t>(HuntState::Tracking), runtime->ZoneId,
		runtime->MapId, oldRevision, static_cast<std::uint32_t>(oldProgress));
	transaction->Append("SET @native_hunts_relic_inserted := ROW_COUNT()");
	transaction->Append(
		"UPDATE `native_hunt_assignment` SET `tracking_progress`=LEAST(100,"
		"`tracking_progress`+{}),`revision`=`revision`+1,"
		"`updated_at`=CURRENT_TIMESTAMP WHERE "
		"@native_hunts_relic_inserted=1 AND `character_guid`={} AND "
		"`hunt_instance_id`='{}' AND `state`={} AND `zone_id`={} AND `map_id`={} "
		"AND `revision`={} AND `tracking_progress`={}",
		static_cast<std::uint32_t>(amount), runtime->CharacterGuid,
		runtime->HuntInstanceId,
		static_cast<std::uint32_t>(HuntState::Tracking), runtime->ZoneId,
		runtime->MapId, oldRevision, static_cast<std::uint32_t>(oldProgress));
	bool const acknowledged = CommitCharacterTransactionAndWait(transaction);
	QueryResult persisted = CharacterDatabase.Query(
		"SELECT collection.`tracking_award`,assignment.`tracking_progress`,"
		"assignment.`revision` FROM `native_hunt_collection` collection "
		"JOIN `native_hunt_assignment` assignment ON "
		"assignment.`hunt_instance_id`=collection.`hunt_instance_id` WHERE "
		"collection.`hunt_instance_id`='{}' AND "
		"collection.`objective_key`='{}' AND collection.`relic_key`='{}'",
		runtime->HuntInstanceId, RelicObjectiveKey, relicKey);
	if (!persisted) {
		message = acknowledged
			? "The relic credit could not be verified; no local progress was changed."
			: "The relic credit transaction failed; try again.";
		return false;
	}
	Field *field = persisted->Fetch();
	std::uint8_t const persistedAward = field[0].Get<std::uint8_t>();
	std::uint8_t const persistedProgress = field[1].Get<std::uint8_t>();
	std::uint64_t const persistedRevision = field[2].Get<std::uint64_t>();
	std::uint8_t const expectedProgress = static_cast<std::uint8_t>(
		std::min<std::uint16_t>(100,
			static_cast<std::uint16_t>(oldProgress) + amount));
	if (persistedAward != amount || persistedProgress != expectedProgress ||
		persistedRevision != oldRevision + 1u) {
		HuntState const oldState = runtime->Aggregate.State;
		runtime->Aggregate.Progress = persistedProgress;
		runtime->Aggregate.Revision = persistedRevision;
		ReconcileTrackingMilestones(player, *runtime);
		SaveAssignment(*runtime);
		if (runtime->Aggregate.State != oldState)
			ReconcileHuntAura(player, &runtime->Aggregate);
		QueueUiState(player);
		message = "This relic was already recorded; persisted Hunt progress was restored.";
		return false;
	}
	if (!acknowledged)
		LOG_WARN("module.native_hunts",
			"Relic transaction acknowledgement was lost for Hunt instance {}, "
			"but the committed ledger and progress were verified.",
			runtime->HuntInstanceId);
	if (!AwardTrackingProgress(player, *runtime, amount)) {
		HuntState const oldState = runtime->Aggregate.State;
		runtime->Aggregate.Progress = persistedProgress;
		runtime->Aggregate.Revision = persistedRevision;
		ReconcileTrackingMilestones(player, *runtime);
		SaveAssignment(*runtime);
		if (runtime->Aggregate.State != oldState)
			ReconcileHuntAura(player, &runtime->Aggregate);
		QueueUiState(player);
	}
	ReconcileRelicPools();
	message = "The cursed relic sharpens your quarry's trail.";
	return true;
}

bool NativeHuntsManager::UseCrystal(Player *player, GameObject *object,
									std::string &message) {
	if (!IsEnabled() || !player || !object) {
		message = "Native Hunt trails are unavailable.";
		return false;
	}
	auto it = _runtimes.find(player->GetGUID().GetCounter());
	if (it == _runtimes.end()) {
		message = "This is not your prey's trail.";
		return false;
	}
	HuntRuntime &runtime = it->second;
	auto const *site = FindFinalSite(runtime.Aggregate.FinalLocationKey);
	bool nearSite = false;
	if (site) {
		float const dx = object->GetPositionX() - site->X;
		float const dy = object->GetPositionY() - site->Y;
		nearSite = dx * dx + dy * dy <= 8.0f * 8.0f;
	}
	CrystalUseContext context{runtime.Aggregate.State,
		true,
		!runtime.CrystalGuid.IsEmpty() && runtime.CrystalGuid == object->GetGUID(),
		object->GetEntry() == _resources.TrailCrystalEntry,
		site && player->GetMapId() == site->MapId &&
			player->GetZoneId() == site->ZoneId && object->GetMap() == player->GetMap(),
		nearSite};
	if (CanActivateCrystal(context) != GameplayDecision::Allowed) {
		message = "The trail does not belong to this Hunt or is not at its persisted site.";
		return false;
	}
	if (!SpawnFinalPrey(player, runtime, message))
		return false;
	object->Delete();
	runtime.CrystalGuid.Clear();
	return true;
}

void NativeHuntsManager::CreateReturnRift(Player *player, Creature *prey,
										 HuntRuntime &runtime) {
	if (!_config.ReturnRiftEnabled || !player || !prey)
		return;
	GameObject *rift = prey->GetMap()->SummonGameObject(
		_resources.ReturnRiftEntry, *prey, 0.0f, 0.0f, 0.0f, 1.0f,
		_config.ReturnRiftDurationSeconds, false);
	if (!rift)
		return;
	rift->SetGoState(GO_STATE_READY);
	rift->RemoveGameObjectFlag(GO_FLAG_IN_USE | GO_FLAG_LOCKED |
		GO_FLAG_INTERACT_COND | GO_FLAG_NOT_SELECTABLE);
	rift->SetPhaseMask(prey->GetPhaseMask(), true);
	runtime.ReturnRiftGuid = rift->GetGUID();
	runtime.ReturnRiftMap = prey->GetMapId();
	runtime.ReturnRiftInstance = prey->GetInstanceId();
	runtime.ReturnRiftExpires = std::chrono::steady_clock::now() +
		std::chrono::seconds(_config.ReturnRiftDurationSeconds);
}

void NativeHuntsManager::OnCreatureKill(Player *killer, Creature *killed) {
	if (!IsEnabled() || !killer || !killed)
		return;

	for (auto &[guid, runtime] : _runtimes) {
		Player *hunter = ConnectedPlayer(guid);
		if (!hunter)
			continue;
		if (!runtime.AmbushGuid.IsEmpty() &&
			runtime.AmbushGuid == killed->GetGUID()) {
			// An ambush is completed only by driving it below the escape threshold.
			// If it is killed, retain AmbushPending so Update recreates it.
			runtime.AmbushGuid.Clear();
			SaveAssignment(runtime);
			continue;
		}

		if (runtime.Aggregate.State == HuntState::PreyActive) {
			auto const *preyDefinition = FindPrey(runtime.Aggregate.Identity.PreyKey);
			std::uint32_t expectedEntry = 0;
			if (preyDefinition) {
				auto const found = _resources.CreatureEntries.find(preyDefinition->Symbol);
				if (found != _resources.CreatureEntries.end())
					expectedEntry = found->second;
			}
			FinalKillContext context{runtime.Aggregate.State, hunter == killer,
				!runtime.PreyGuid.IsEmpty() && runtime.PreyGuid == killed->GetGUID(),
				killed->GetEntry() == expectedEntry};
			if (CanCompleteFinalKill(context) == GameplayDecision::Allowed &&
				HuntDomain::Execute(runtime.Aggregate, MarkPreyKilled{}).Status ==
					TransitionStatus::Applied) {
				runtime.PreyGuid.Clear();
				CreateReturnRift(hunter, killed, runtime);
				SaveAssignment(runtime);
				ReconcileHuntAura(hunter, &runtime.Aggregate);
				QueueUiState(hunter);
			}
			continue;
		}

		TrackingKillContext context;
		context.HunterIsKillerOrGrouped =
			hunter == killer || (killer->GetGroup() && hunter->GetGroup() == killer->GetGroup());
		context.SameMap = hunter->GetMapId() == killed->GetMapId();
		context.InAssignedZone = hunter->GetZoneId() == runtime.ZoneId;
		context.WithinCreditRadius = hunter->GetDistance(killed) <= _config.GroupCreditRadius;
		context.IsGrey = Acore::XP::GetColorCode(hunter->GetLevel(), killed->GetLevel()) == XP_GRAY;
		context.AmbushPending = runtime.AmbushPending;
		context.IsHuntPrey = false;
		for (auto const &prey : StandardPrey()) {
			auto found = _resources.CreatureEntries.find(prey.Symbol);
			if (found != _resources.CreatureEntries.end() && found->second == killed->GetEntry()) {
				context.IsHuntPrey = true;
				break;
			}
		}
		if (runtime.Aggregate.State != HuntState::Tracking ||
			CanAdvanceTracking(context) != GameplayDecision::Allowed)
			continue;

		std::uint8_t amount = static_cast<std::uint8_t>(urand(
			_config.TrackingProgressMin, _config.TrackingProgressMax));
		AwardTrackingProgress(hunter, runtime, amount);
	}
}

void NativeHuntsManager::UpdatePreyAbilities(Player *player,
										  HuntRuntime &runtime,
										  Creature *prey,
										  std::uint32_t elapsedMs) {
	if (!player || !prey || !prey->IsAlive() || !prey->IsInCombat())
		return;
	std::vector<PreyAbilityDefinition const *> abilities;
	auto const *definition = FindPrey(runtime.Aggregate.Identity.PreyKey);
	auto const &catalog = definition && definition->Tier == PreyTier::Elite
		? ElitePreyAbilities() : StandardPreyAbilities();
	for (auto const &ability : catalog)
		if (runtime.Aggregate.Identity.PreyKey == ability.PreyKey)
			abilities.push_back(&ability);
	if (abilities.empty())
		return;
	if (runtime.AbilityTimers.size() != abilities.size()) {
		runtime.AbilityTimers.assign(abilities.size(), 0);
		runtime.AbilityUsed.assign(abilities.size(), false);
	}
	if (runtime.FearDrResetMs) {
		if (runtime.FearDrResetMs<=elapsedMs) { runtime.FearDrResetMs=0; runtime.FearDrStage=0; }
		else runtime.FearDrResetMs-=elapsedMs;
	}
	if (runtime.RogueReopenMs) {
		if (runtime.RogueReopenMs>elapsedMs) { runtime.RogueReopenMs-=elapsedMs; return; }
		runtime.RogueReopenMs=0;
		prey->CastSpell(player,prey->GetDistance(player)<=8.0f?48691:1833,true);
		prey->AI()->AttackStart(player);
	}
	if (definition && definition->Key==std::string("wildclaw") &&
		!runtime.DruidBearPhase && prey->GetHealthPct()<=45.0f) {
		prey->RemoveAurasDueToSpell(768); prey->CastSpell(prey,5487,true);
		runtime.DruidBearPhase=true;
	}
	for (std::size_t i = 0; i < abilities.size(); ++i) {
		auto const &ability = *abilities[i];
		auto &timer = runtime.AbilityTimers[i];
		if (timer > elapsedMs) {
			timer -= elapsedMs;
			continue;
		}
		std::uint8_t const mask = runtime.Aggregate.State == HuntState::PreyActive ? 2 : 1;
		bool const eligible = (ability.EncounterMask & mask) &&
			player->GetLevel() >= ability.MinHunterLevel &&
			player->GetLevel() <= ability.MaxHunterLevel &&
			(!ability.HealthBelowPercent || prey->GetHealthPct() <= ability.HealthBelowPercent) &&
			(!ability.VictimHealthBelowPercent || player->GetHealthPct() <= ability.VictimHealthBelowPercent) &&
			(!ability.RequireMelee || prey->IsWithinMeleeRange(player)) &&
			(!ability.OncePerEncounter || !runtime.AbilityUsed[i]) &&
			(!ability.RequireAuraMissing || !prey->HasAura(ability.SpellId));
		float const distance=prey->GetDistance(player);
		if (ability.SpellId==11578 && (distance<8.0f || distance>25.0f)) continue;
		if (ability.SpellId==1953 && distance>10.0f) continue;
		if (ability.SpellId==47528 && !player->HasUnitState(UNIT_STATE_CASTING)) continue;
		bool const fear=ability.SpellId==6215 || ability.SpellId==10890;
		if (fear && runtime.FearDrStage>=3 && runtime.FearDrResetMs) continue;
		if (definition && definition->Key==std::string("wildclaw")) {
			bool const bearAbility=ability.SpellId==8983 || ability.SpellId==48564 || ability.SpellId==22842;
			if (bearAbility != runtime.DruidBearPhase) continue;
		}
		if (ability.SpellId == 883) { runtime.AbilityUsed[i]=true; continue; }
		if (eligible && urand(1, 100) <= ability.ChancePercent) {
			if (ability.SpellId==26889) {
				prey->CastSpell(prey,ability.SpellId,true);
				Position behind=player->GetNearPosition(4.0f,3.14159265f);
				prey->GetMotionMaster()->MovePoint(103,behind);
				runtime.RogueReopenMs=1600;
			} else if (ability.SpellId==49938)
				prey->CastSpell(player->GetPositionX(),player->GetPositionY(),
					player->GetPositionZ(),ability.SpellId,true);
			else {
				if (ability.SpellId==1953)
					prey->SetFacingTo(std::atan2(prey->GetPositionY()-player->GetPositionY(),
						prey->GetPositionX()-player->GetPositionX()));
			prey->CastSpell(ability.SelfTarget ? static_cast<Unit *>(prey)
											  : static_cast<Unit *>(player),
							   ability.SpellId, true);
			}
			if (fear) {
				std::uint32_t duration=runtime.FearDrStage==0?6000:runtime.FearDrStage==1?3000:1500;
				if (Aura *aura=player->GetAura(ability.SpellId,prey->GetGUID())) {
					aura->SetMaxDuration(duration); aura->SetDuration(duration);
				}
				runtime.FearDrStage=std::min<std::uint8_t>(3,runtime.FearDrStage+1);
				runtime.FearDrResetMs=duration+15000;
			}
			if (ability.OncePerEncounter)
				runtime.AbilityUsed[i] = true;
		}
		timer = urand(ability.CooldownMinMs, ability.CooldownMaxMs);
	}
}

void NativeHuntsManager::Update(std::uint32_t elapsedMs) {
	if (!IsEnabled()) {
		RemoveAllRelicPoolObjects();
		return;
	}
	_updateAccumulator += elapsedMs;
	_poiAccumulator += elapsedMs;
	if (_updateAccumulator < 500)
		return;
	std::uint32_t const tick = _updateAccumulator;
	_updateAccumulator = 0;
	bool const refreshPoi = _poiAccumulator >= 5000;
	if (refreshPoi)
		_poiAccumulator = 0;
	for (auto &[guid, runtime] : _runtimes) {
		Player *player = ConnectedPlayer(guid);
		if (!player)
			continue;
		if (runtime.Aggregate.State == HuntState::Tracking &&
			runtime.AmbushPending) {
			Creature *ambush = runtime.AmbushGuid.IsEmpty()
				? nullptr
				: ObjectAccessor::GetCreature(*player, runtime.AmbushGuid);
			if (player->GetZoneId() != runtime.ZoneId) {
				if (ambush)
					ambush->DespawnOrUnsummon();
				runtime.AmbushGuid.Clear();
			} else if (!ambush || !ambush->IsInWorld()) {
				runtime.AmbushGuid.Clear();
				std::string ignored;
				SpawnAmbush(player, runtime, ignored);
			} else if (ambush->GetHealthPct() <=
					   _config.AmbushEscapeHealthPercent) {
				ambush->CombatStop(true);
				ambush->SetFlag(UNIT_FIELD_FLAGS,
					UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC);
				ChatHandler(player->GetSession()).PSendSysMessage(
					"|cffffff00[Native Hunts]|r {} breaks away and disappears. "
					"Continue tracking it.",
					runtime.Aggregate.Identity.PreyName);
				ambush->DespawnOrUnsummon(Milliseconds(1500));
				runtime.AmbushGuid.Clear();
				runtime.AmbushPending = false;
				if (runtime.AmbushesCompleted < _config.AmbushCount)
					++runtime.AmbushesCompleted;
				HuntState const oldState = runtime.Aggregate.State;
				ReconcileTrackingMilestones(player, runtime);
				SaveAssignment(runtime);
				if (runtime.Aggregate.State != oldState)
					ReconcileHuntAura(player, &runtime.Aggregate);
				QueueUiState(player);
			} else
				UpdatePreyAbilities(player, runtime, ambush, tick);
		}
		if (runtime.Aggregate.State == HuntState::FinalRevealed) {
			if (!runtime.FinalRevealNotified || refreshPoi)
				SendFinalLocationFeedback(player, runtime,
					!runtime.FinalRevealNotified);
			EnsureCrystal(player, runtime);
		} else if (runtime.Aggregate.State == HuntState::PreyActive) {
			Creature *prey = runtime.PreyGuid.IsEmpty()
				? nullptr
				: ObjectAccessor::GetCreature(*player, runtime.PreyGuid);
			if (!prey || !prey->IsInWorld()) {
				runtime.PreyGuid.Clear();
				HuntDomain::Execute(runtime.Aggregate, RecoverAfterRestart{});
				SaveAssignment(runtime);
				ReconcileHuntAura(player, &runtime.Aggregate);
				QueueUiState(player);
			} else
				UpdatePreyAbilities(player, runtime, prey, tick);
		}
		if (!runtime.ReturnRiftGuid.IsEmpty() &&
			std::chrono::steady_clock::now() >= runtime.ReturnRiftExpires) {
			if (GameObject *rift = ObjectAccessor::GetGameObject(
					*player, runtime.ReturnRiftGuid))
				rift->Delete();
			runtime.ReturnRiftGuid.Clear();
		}
	}
	for (auto it = _uiPending.begin(); it != _uiPending.end();) {
		if (std::chrono::steady_clock::now() < it->second) {
			++it;
			continue;
		}
		std::uint32_t const guid = it->first;
		it = _uiPending.erase(it);
		if (Player *player = ConnectedPlayer(guid))
			PublishUiState(player);
	}
	ReconcileRelicPools();
}

bool NativeHuntsManager::UseReturnRift(Player *player, GameObject *object,
									   std::string &message) {
	if (!IsEnabled() || !player || !object) {
		message = "Return Rifts are unavailable.";
		return false;
	}
	auto it = _runtimes.find(player->GetGUID().GetCounter());
	if (it == _runtimes.end()) {
		message = "You have no return opportunity.";
		return false;
	}
	HuntRuntime &runtime = it->second;
	ReturnRiftUseContext context{
		runtime.Aggregate.State,
		true,
		!runtime.ReturnRiftGuid.IsEmpty() && runtime.ReturnRiftGuid == object->GetGUID(),
		object->GetEntry() == _resources.ReturnRiftEntry,
		object->GetMap() == player->GetMap() && player->InSamePhase(object),
		player->IsWithinDistInMap(object, INTERACTION_DISTANCE),
		std::chrono::steady_clock::now() < runtime.ReturnRiftExpires};
	if (CanUseReturnRift(context) != GameplayDecision::Allowed) {
		message = "This Return Rift is not yours or is no longer usable.";
		return false;
	}
	if (!player->IsAlive() || player->IsInCombat() || player->IsInFlight() ||
		player->IsBeingTeleported()) {
		message = "You cannot return while dead, in combat, flying, or teleporting.";
		return false;
	}
	CreatureData const *spawn = sObjectMgr->GetCreatureData(runtime.HuntmasterSpawn);
	if (!spawn || spawn->id != runtime.HuntmasterEntry) {
		message = "Your issuing Huntmaster is unavailable. Return normally.";
		return false;
	}
	Map *map = sMapMgr->CreateBaseMap(spawn->mapid);
	if (!map) {
		message = "The Huntmaster's map is unavailable.";
		return false;
	}
	map->LoadGrid(spawn->posX, spawn->posY);
	auto const range = map->GetCreatureBySpawnIdStore().equal_range(runtime.HuntmasterSpawn);
	Creature *giver = nullptr;
	for (auto candidate = range.first; candidate != range.second; ++candidate)
		if (candidate->second->GetEntry() == runtime.HuntmasterEntry &&
			candidate->second->IsAlive() && candidate->second->IsInWorld()) {
			giver = candidate->second;
			break;
		}
	if (!giver) {
		message = "Your issuing Huntmaster is unavailable. Return normally.";
		return false;
	}
	Position const arrival = giver->GetNearPosition(
		_config.ReturnRiftArrivalDistance, 0.0f);
	if (!player->TeleportTo(spawn->mapid, arrival.GetPositionX(),
			arrival.GetPositionY(), arrival.GetPositionZ(),
			arrival.GetOrientation())) {
		message = "Teleport failed; the rift remains available.";
		return false;
	}
	object->Delete();
	runtime.ReturnRiftGuid.Clear();
	message = "The rift returns you to your issuing Huntmaster.";
	return true;
}


bool NativeHuntsManager::TurnIn(Player *player, Creature *giver,
		std::string &message) {
	if (!IsEnabled() || !player || !giver) return false;
	auto it = _runtimes.find(player->GetGUID().GetCounter());
	if (it == _runtimes.end()) { message = "You have no Hunt to turn in."; return false; }
	HuntRuntime &runtime = it->second;
	std::uint32_t const guid = runtime.CharacterGuid;
	if (auto uncertain = _uncertainTurnIns.find(guid);
		uncertain != _uncertainTurnIns.end()) {
		auto assignment = AssignmentExists(guid);
		if (!assignment) {
			message = "The earlier turn-in commit is still unreadable; duplicate rewards are blocked.";
			return false;
		}
		if (!*assignment) {
			_uncertainTurnIns.erase(uncertain);
			HuntDomain::Execute(runtime.Aggregate,TurnInHunt{});
			ReconcileHuntAura(player, nullptr);
			RemoveRuntimeObjects(player,runtime); _runtimes.erase(it);
			ReconcileRelicPools();
			QueueUiState(player);
			message = "The earlier Hunt completion is now confirmed.";
			return true;
		}
		auto const provisional = uncertain->second;
		RemoveProvisionalRewards(player, _resources, provisional);
		_uncertainTurnIns.erase(uncertain);
		message = "The earlier completion rolled back; provisional rewards were removed.";
		return false;
	}
	bool const correctGiver = MatchesIssuingHuntmaster(giver->GetEntry(),
		giver->GetSpawnId(), runtime.HuntmasterEntry, runtime.HuntmasterSpawn);
	if (runtime.Aggregate.State != HuntState::ReadyToTurnIn || !correctGiver) {
		message = "Only the issuing Huntmaster can complete a finished Hunt.";
		return false;
	}
	auto const *prey = FindPrey(runtime.Aggregate.Identity.PreyKey);
	if (!prey) { message = "The reward definition is unavailable."; return false; }
	if (!CharacterDatabase.Query(
		"SELECT 1 FROM `native_hunt_assignment` WHERE `character_guid`={} "
		"AND `state`={} AND `revision`={}", guid,
		static_cast<std::uint32_t>(HuntState::ReadyToTurnIn),
		runtime.Aggregate.Revision)) {
		message = "The persisted Hunt is not ready for completion.";
		return false;
	}
	bool const elite = runtime.Aggregate.Identity.Tier == PreyTier::Elite;
	std::uint32_t const level = player->GetLevel();
	bool const endgameElite = elite && level >= _config.EliteEndgameRewardLevel;
	std::uint32_t dailyBefore = 0;
	if (QueryResult stats = CharacterDatabase.Query(
		"SELECT IF(`daily_standard_reset_date`=CURRENT_DATE(),"
		"`daily_standard_completed`,0) FROM `native_hunt_stats` WHERE "
		"`character_guid`={}", guid))
		dailyBefore = stats->Fetch()[0].Get<std::uint32_t>();
	std::uint32_t quality = endgameElite ? ITEM_QUALITY_EPIC : elite ? ITEM_QUALITY_RARE :
		static_cast<std::uint32_t>(StandardRewardQuality(dailyBefore, urand(1,1000)));
	std::uint32_t const spec = player->GetSpec();
	RewardRole const role = GetRewardRole(player, spec);
	struct Candidate { std::uint32_t id; float score; float upgrade; };
	std::vector<Candidate> scored;
	std::uint32_t const minimumLevel = level > 5 ? level - 5 : 1;
	for (auto const &[itemId, item] : *sObjectMgr->GetItemTemplateStore()) {
		if (item.Quality != quality || (item.Class != ITEM_CLASS_WEAPON && item.Class != ITEM_CLASS_ARMOR) ||
			item.InventoryType == INVTYPE_NON_EQUIP || item.InventoryType == INVTYPE_BAG ||
			item.InventoryType == INVTYPE_TABARD || item.InventoryType == INVTYPE_AMMO ||
			item.InventoryType == INVTYPE_QUIVER || item.RequiredLevel > level ||
			item.RequiredLevel < minimumLevel || !IsSpecCompatibleEquipment(spec,item) ||
			player->CanUseItem(&item) != EQUIP_ERR_OK)
			continue;
		if (endgameElite && (item.ItemLevel < _config.EliteEndgameRewardMinItemLevel ||
			item.ItemLevel > _config.EliteEndgameRewardMaxItemLevel)) continue;
		std::uint32_t limitedCategory = 0;
		InventoryResult const carryResult = player->CanTakeMoreSimilarItems(
			itemId, 1, &limitedCategory);
		if (carryResult != EQUIP_ERR_OK) {
			LOG_DEBUG("module.native_hunts",
				"Excluding reward item {} for character {}: carry result {} "
				"({}), limited category {}.", itemId, guid,
				static_cast<std::uint32_t>(carryResult),
				InventoryFailureName(carryResult), limitedCategory);
			continue;
		}
		std::optional<uint8> upgradeSlot;
		if (endgameElite && _config.EliteRewardRequireUpgrade) {
			upgradeSlot = GetEndgameEliteUpgradeSlot(player, item,
				_config.EliteEndgameRewardMinItemLevel);
			if (!upgradeSlot)
				continue;
		}
		float const score = ScoreRewardItem(player, spec, role, item);
		if (elite && score <= 0) continue;
		float const equippedPower = upgradeSlot ?
			GetEquippedItemPower(player, spec, role, *upgradeSlot) :
			GetEquippedPowerForCandidate(player, spec, role, item);
		std::uint32_t const equippedLevel = upgradeSlot ?
			GetEquippedItemLevel(player, *upgradeSlot) :
			GetEquippedItemLevelForCandidate(player, item);
		float const upgrade = ScoreRewardPower(player,spec,role,item) -
			equippedPower;
		if (elite && _config.EliteRewardRequireUpgrade &&
			(item.ItemLevel <= equippedLevel || upgrade <= 1.0f)) continue;
		scored.push_back({itemId,score,upgrade});
	}
	std::sort(scored.begin(), scored.end(), [elite](auto const &a, auto const &b) {
		return elite && std::fabs(a.upgrade-b.upgrade) > .01f ? a.upgrade>b.upgrade : a.score>b.score;
	});
	std::vector<std::uint32_t> pool;
	if (!scored.empty()) {
		float cutoff = elite ? scored.front().upgrade * _config.EliteRewardUpgradePoolPct : scored.front().score - 12.0f;
		for (auto const &candidate : scored) {
			if (pool.size() >= 12 || (elite ? candidate.upgrade < cutoff : candidate.score < cutoff)) break;
			pool.push_back(candidate.id);
		}
		if (pool.empty()) pool.push_back(scored.front().id);
	}
	bool const noUpgrade = endgameElite && _config.EliteRewardRequireUpgrade && pool.empty();
	std::uint32_t const seals = EliteSealReward(elite, level,
		_config.EliteSealMinimumLevel, _config.EliteSealsPerCompletion,
		noUpgrade, _config.EliteNoUpgradeBonusSeals);
	std::uint32_t const rewardItem = pool.empty() ? 0 :
		pool[urand(0, static_cast<std::uint32_t>(pool.size()-1))];
	ItemPosCountVec equipmentDest;
	ItemPosCountVec sealDest;
	InventoryResult equipmentResult = EQUIP_ERR_OK;
	InventoryResult sealResult = EQUIP_ERR_OK;
	InventoryResult combinedResult = EQUIP_ERR_OK;
	std::uint32_t equipmentNoSpaceCount = 0;
	std::uint32_t sealNoSpaceCount = 0;
	std::uint32_t bundleLimitedCategory = 0;
	if (rewardItem)
		equipmentResult = player->CanStoreNewItem(NULL_BAG, NULL_SLOT,
			equipmentDest, rewardItem, 1, &equipmentNoSpaceCount);
	if (seals)
		sealResult = player->CanStoreNewItem(NULL_BAG, NULL_SLOT,
			sealDest, _resources.SealItemEntry, seals, &sealNoSpaceCount);
	if (rewardItem && seals) {
		auto const bundle = CanStoreRewardBundle(player, rewardItem,
			_resources.SealItemEntry, seals);
		combinedResult = bundle.Result;
		bundleLimitedCategory = bundle.LimitedCategory;
	}
	auto plan = PlanRewardDelivery(rewardItem != 0, seals != 0,
		static_cast<std::uint32_t>(equipmentResult),
		static_cast<std::uint32_t>(sealResult),
		static_cast<std::uint32_t>(combinedResult));
	if (rewardItem && !plan.EquipmentToInventory)
		LOG_INFO("module.native_hunts",
			"Mailing Native Hunt reward item {} to character {}: inventory "
			"result {} ({}), no-space count {}, free slots {}.", rewardItem,
			guid, static_cast<std::uint32_t>(equipmentResult),
			InventoryFailureName(equipmentResult), equipmentNoSpaceCount,
			player->GetFreeInventorySpace());
	if (seals && !plan.SealsToInventory)
		LOG_INFO("module.native_hunts",
			"Mailing {} Native Hunt Seal(s) to character {}: inventory "
			"result {} ({}, no-space count {}, free slots {}; bundle result "
			"{} ({}, limited category {})).", seals, guid,
			static_cast<std::uint32_t>(sealResult),
			InventoryFailureName(sealResult), sealNoSpaceCount,
			player->GetFreeInventorySpace(),
			static_cast<std::uint32_t>(combinedResult),
			InventoryFailureName(combinedResult), bundleLimitedCategory);

	Item *equipment = nullptr;
	if (plan.EquipmentToInventory) {
		equipment = player->StoreNewItem(equipmentDest, rewardItem, true);
		if (!equipment) {
			LOG_WARN("module.native_hunts",
				"Native Hunt reward item {} passed inventory preflight but "
				"storage failed for character {}; falling back to mail.",
				rewardItem, guid);
			plan.EquipmentToInventory = false;
		}
		if (equipment && endgameElite)
			equipment->SetBinding(true);
	}
	Item *seal = nullptr;
	if (plan.SealsToInventory) {
		sealDest.clear();
		InventoryResult const currentResult = player->CanStoreNewItem(
			NULL_BAG, NULL_SLOT, sealDest, _resources.SealItemEntry, seals);
		if (currentResult == EQUIP_ERR_OK)
			seal = player->StoreNewItem(sealDest, _resources.SealItemEntry,
				true);
		if (!seal) {
			LOG_WARN("module.native_hunts",
				"Native Hunt Seal storage failed for character {}; falling back "
				"to mail.", guid);
			plan.SealsToInventory = false;
		}
	}
	bool const mailedEquipment = rewardItem && !plan.EquipmentToInventory;
	bool const mailedSeals = seals && !plan.SealsToInventory;
	std::vector<Item *> mailItems;
	auto createMailItem = [&](std::uint32_t itemEntry,
			std::uint32_t count, bool bind) {
		Item *item = Item::CreateItem(itemEntry, count, player);
		if (!item)
			return false;
		item->RemoveFromUpdateQueueOf(player);
		if (bind)
			item->SetBinding(true);
		mailItems.push_back(item);
		return true;
	};
	if ((mailedEquipment && !createMailItem(rewardItem, 1, endgameElite)) ||
		(mailedSeals && !createMailItem(_resources.SealItemEntry, seals, false))) {
		for (Item *item : mailItems)
			delete item;
		ProvisionalTurnIn direct;
		direct.SealCount = seal ? seals : 0;
		direct.EquipmentEntry = equipment ? rewardItem : 0;
		direct.EquipmentGuid = equipment ? equipment->GetGUID() : ObjectGuid();
		RemoveProvisionalRewards(player, _resources, direct);
		message = "The mailed reward could not be created; your Hunt remains ready.";
		return false;
	}

	ProvisionalTurnIn provisional;
	provisional.SealCount = seal ? seals : 0;
	provisional.EquipmentEntry = equipment ? rewardItem : 0;
	provisional.EquipmentGuid = equipment ? equipment->GetGUID() : ObjectGuid();
	std::uint32_t const money = HuntMoneyReward(level,prey->RewardMultiplier,
		elite ? _config.EliteGoldMultiplier : 1.0f);
	provisional.Money = money;
	auto transaction = CharacterDatabase.BeginTransaction();
	if (!mailItems.empty()) {
		std::size_t const previousMailCount = player->GetMailSize();
		MailDraft draft("Huntmaster's Reward — " +
			runtime.Aggregate.Identity.PreyName,
			"Your quarry has fallen, hunter. Your reward awaits you. May your "
			"next hunt be just as successful.");
		for (Item *item : mailItems) {
			item->SaveToDB(transaction);
			provisional.MailItemGuids.push_back(item->GetGUID());
			draft.AddItem(item);
		}
		draft.SendMailTo(transaction, MailReceiver(player),
			MailSender(MAIL_CREATURE, runtime.HuntmasterEntry),
			MAIL_CHECK_MASK_HAS_BODY);
		if (player->GetMailSize() != previousMailCount + 1) {
			RemoveProvisionalRewards(player, _resources, provisional);
			for (Item *item : mailItems)
				delete item;
			message = "The reward mail could not be prepared; your Hunt remains ready.";
			return false;
		}
		provisional.MailId = player->GetMails().front()->messageID;
	}
	player->ModifyMoney(static_cast<std::int32_t>(money));
	transaction->Append("INSERT IGNORE INTO `native_hunt_stats` (`character_guid`) VALUES ("+
		std::to_string(guid)+")");
	transaction->Append("INSERT INTO `native_hunt_stats` (`character_guid`) SELECT "+
		std::to_string(guid)+" WHERE NOT EXISTS (SELECT 1 FROM `native_hunt_assignment` WHERE `character_guid`="+
		std::to_string(guid)+" AND `state`="+std::to_string(static_cast<std::uint32_t>(HuntState::ReadyToTurnIn))+
		" AND `revision`="+std::to_string(runtime.Aggregate.Revision)+")");
	player->SaveInventoryAndGoldToDB(transaction);
	std::string qualityColumn = rewardItem ? (quality == ITEM_QUALITY_EPIC ? "epics_received" :
		quality == ITEM_QUALITY_RARE ? "blues_received" : "greens_received") : "";
	std::ostringstream stats;
	stats << "INSERT INTO `native_hunt_stats` (`character_guid`,`lifetime_completed`,`standard_completed`,`daily_standard_completed`,`daily_standard_reset_date`,`greens_received`,`blues_received`,`epics_received`,`elite_completed`,`last_completed_at`) VALUES ("
		<< guid << ",1," << (elite?0:1) << "," << (elite?0:1) << ","
		<< (elite?"NULL":"CURRENT_DATE()") << ","
		<< (qualityColumn=="greens_received"?1:0) << ","
		<< (qualityColumn=="blues_received"?1:0) << ","
		<< (qualityColumn=="epics_received"?1:0) << ","
		<< (elite?1:0) << ",CURRENT_TIMESTAMP) ON DUPLICATE KEY UPDATE `lifetime_completed`=`lifetime_completed`+1,";
	if (elite) stats << "`elite_completed`=`elite_completed`+1,";
	else stats << "`standard_completed`=`standard_completed`+1,`daily_standard_completed`=IF(`daily_standard_reset_date`=CURRENT_DATE(),`daily_standard_completed`+1,1),`daily_standard_reset_date`=CURRENT_DATE(),";
	if (!qualityColumn.empty()) stats << "`" << qualityColumn << "`=`" << qualityColumn << "`+1,";
	stats << "`last_completed_at`=CURRENT_TIMESTAMP";
	transaction->Append(stats.str());
	transaction->Append("DELETE FROM `native_hunt_assignment` WHERE `character_guid`="+
		std::to_string(guid)+" AND `state`="+std::to_string(static_cast<std::uint32_t>(HuntState::ReadyToTurnIn))+
		" AND `revision`="+std::to_string(runtime.Aggregate.Revision));
	bool const commitAcknowledged = CommitCharacterTransactionAndWait(transaction);
	if (!commitAcknowledged) {
		auto const assignment = AssignmentExists(guid);
		AssignmentPersistence const persistence = !assignment ?
			AssignmentPersistence::Unknown : *assignment ?
			AssignmentPersistence::Present : AssignmentPersistence::Missing;
		switch (ResolveRewardCommit(false, persistence)) {
			case RewardCommitResolution::Complete:
				// Commit succeeded but its async acknowledgement was lost.
				break;
			case RewardCommitResolution::AwaitRecovery:
				_uncertainTurnIns[guid] = provisional;
				message = "The database result is unreadable; duplicate rewards are blocked until recovery.";
				return false;
			case RewardCommitResolution::RollBack:
				RemoveProvisionalRewards(player, _resources, provisional);
				message = "Hunt completion could not be committed; provisional rewards were removed.";
				return false;
		}
	}
	if (equipment)
		player->SendNewItem(equipment, 1, true, false);
	if (seal)
		player->SendNewItem(seal, seals, true, false);
	std::uint32_t const xp = HuntXpReward(player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP),
		level >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL), _config.XpMultiplier,
		prey->RewardMultiplier, elite ? _config.EliteXpMultiplier : 1.0f);
	if (xp) player->GiveXP(xp,nullptr,1.0f);
	HuntDomain::Execute(runtime.Aggregate,TurnInHunt{});
	ReconcileHuntAura(player, nullptr);
	RemoveRuntimeObjects(player,runtime);
	_runtimes.erase(it);
	ReconcileRelicPools();
	QueueUiState(player);
	std::ostringstream out;
	out << "Hunt complete: " << xp << " XP, " << money/10000 << "g " << (money/100)%100 << "s " << money%100 << "c";
	if (rewardItem) if (auto const *t=sObjectMgr->GetItemTemplate(rewardItem)) out << ", " << t->Name1;
	if (seals) out << ", " << seals << " physical Huntmaster's Seal" << (seals==1?"":"s");
	if (mailedEquipment || mailedSeals) {
		out << ". Direct inventory delivery was unavailable; the Huntmaster mailed ";
		if (mailedEquipment && mailedSeals)
			out << "the equipment and Seals";
		else if (mailedEquipment)
			out << "the equipment";
		else
			out << "the Seals";
	}
	message=out.str()+".";
	return true;
}

std::uint32_t NativeHuntsManager::LifetimeCompletions(Player const *player) const {
	if (!player)
		return 0;
	if (QueryResult result = CharacterDatabase.Query(
			"SELECT `lifetime_completed` FROM `native_hunt_stats` WHERE "
			"`character_guid`={}",
			player->GetGUID().GetCounter()))
		return result->Fetch()[0].Get<std::uint32_t>();
	return 0;
}

HuntProgressionSnapshot NativeHuntsManager::ReadProgression(
		Player const *player) const {
	HuntProgressionSnapshot progression;
	progression.EliteDailyLimit = _config.EliteDailyLimit;
	if (!player)
		return progression;
	if (QueryResult result = CharacterDatabase.Query(
		"SELECT `standard_completed`,`elite_completed`,"
		"IF(`elite_daily_accept_reset_date`=CURRENT_DATE(),"
		"`elite_daily_accepted`,0) FROM `native_hunt_stats` WHERE "
		"`character_guid`={}", player->GetGUID().GetCounter())) {
		Field *fields = result->Fetch();
		progression.StandardCompleted = fields[0].Get<std::uint32_t>();
		progression.EliteCompleted = fields[1].Get<std::uint32_t>();
		progression.EliteAcceptedToday = fields[2].Get<std::uint32_t>();
	}
	progression.EliteUnlocked = EliteUnlocked(progression.StandardCompleted,
		_config.EliteRequiredStandardCompletions);
	progression.EliteAvailableToday = progression.EliteUnlocked &&
		EliteAvailable(progression.EliteAcceptedToday, _config.EliteDailyLimit);
	return progression;
}

HuntSnapshot NativeHuntsManager::BuildUiSnapshot(Player const *player) const {
	HuntAggregate aggregate;
	if (auto const *runtime = GetRuntime(player))
		aggregate = runtime->Aggregate;
	NativeSealStatus seals = NativeSealStatus::Unavailable(_resources.Reason);
	if (_resources.Ready && _resources.SealItemEntry && player)
		// Physical Seals are spendable from carried inventory; banked items are
		// intentionally excluded to match StoreNewItem/DestroyItemCount gameplay.
		seals = NativeSealStatus::Available(
			player->GetItemCount(_resources.SealItemEntry, false));
	return BuildHuntSnapshot(aggregate, seals, ReadProgression(player),
		"No active Hunt.");
}

void NativeHuntsManager::QueueUiState(Player *player) {
	if (!player)
		return;
	_uiPending[player->GetGUID().GetCounter()] =
		std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
}

void NativeHuntsManager::PublishUiState(Player *player, std::uint32_t nonce,
		bool force) {
	if (!player)
		return;
	std::uint32_t const guid = player->GetGUID().GetCounter();
	HuntSnapshot const snapshot = BuildUiSnapshot(player);
	auto canonicalFrames = ui::SerializeSnapshot(snapshot, 0, 0);
	if (canonicalFrames.empty())
		return;
	std::string canonical;
	for (auto const &frame : canonicalFrames)
		canonical.append(frame).push_back('\n');
	if (!force) {
		auto const previous = _uiLastProjection.find(guid);
		if (previous != _uiLastProjection.end() && previous->second == canonical)
			return;
	}
	std::uint32_t sequence = ++_uiSequences[guid];
	if (!sequence)
		sequence = ++_uiSequences[guid];
	auto frames = ui::SerializeSnapshot(snapshot, sequence, nonce);
	if (frames.empty())
		return;
	_uiLastProjection[guid] = std::move(canonical);
	for (auto const &payload : frames)
		player->Whisper(std::string(ui::Prefix) + "\t" + payload,
			LANG_ADDON, player);
}

void NativeHuntsManager::HandleUiAddonMessage(Player *player,
		std::string const &payload) {
	if (!player)
		return;
	auto const request = ui::ParseSnapshotRequest(payload);
	if (!request)
		return;
	std::uint32_t const guid = player->GetGUID().GetCounter();
	auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	std::uint64_t const previous = _uiLastRequestMs[guid];
	if (!ui::RequestAllowed(previous, static_cast<std::uint64_t>(now)))
		return;
	_uiLastRequestMs[guid] = static_cast<std::uint64_t>(now);
	PublishUiState(player, request->Nonce, true);
}

std::string NativeHuntsManager::BuildStatus(Player const *player,
										 bool includeCoordinates) const {
	if (!player)
		return "[Native Hunts] A logged-in player is required.";
	std::ostringstream out;
	out << "[Native Hunts] resources=" << (_resources.Ready ? "ready" : "unavailable")
		<< " (" << _resources.Reason << ")";
	auto const *runtime = GetRuntime(player);
	if (!runtime)
		return out.str() + " | no active Hunt";
	auto const &hunt = runtime->Aggregate;
	out << " | state=" << StateName(hunt.State)
		<< " | huntmaster=" << hunt.Identity.HuntmasterName
		<< " | prey=" << hunt.Identity.PreyName
		<< " | tier=" << static_cast<std::uint32_t>(hunt.Identity.Tier)
		<< " | zone=" << hunt.Identity.ZoneName << " (" << runtime->ZoneId << ")"
		<< " | tracking=" << static_cast<std::uint32_t>(hunt.Progress) << "%"
		<< " | ambushes=" << static_cast<std::uint32_t>(runtime->AmbushesCompleted)
		<< "/" << static_cast<std::uint32_t>(_config.AmbushCount)
		<< (runtime->AmbushPending ? " pending" : "")
		<< " | revision=" << hunt.Revision;
	if (!hunt.FinalLocationKey.empty()) {
		out << " | final_site=" << hunt.FinalLocationKey;
		if (includeCoordinates)
			if (auto const *site = FindFinalSite(hunt.FinalLocationKey))
				out << " | map=" << site->MapId << " xyz=" << site->X << ","
					<< site->Y << "," << site->Z << " o=" << site->Orientation;
	}
	return out.str();
}

void NativeHuntsManager::OnLogin(Player *player) {
	if (!player)
		return;
	auto const it = _runtimes.find(player->GetGUID().GetCounter());
	ReconcileHuntAura(player,
		it == _runtimes.end() ? nullptr : &it->second.Aggregate);
}

void NativeHuntsManager::OnLogout(Player *player) {
	if (!player)
		return;
	std::uint32_t const guid = player->GetGUID().GetCounter();
	_uiLastRequestMs.erase(guid);
	_uiSequences.erase(guid);
	_uiLastProjection.erase(guid);
	_uiPending.erase(guid);
	auto it = _runtimes.find(guid);
	if (it == _runtimes.end())
		return;
	auto uncertain = _uncertainTurnIns.find(guid);
	if (uncertain != _uncertainTurnIns.end()) {
		auto const assignment = AssignmentExists(guid);
		if (assignment.has_value() && !*assignment) {
			_uncertainTurnIns.erase(uncertain);
			HuntDomain::Execute(it->second.Aggregate, TurnInHunt{});
			ReconcileHuntAura(player, nullptr);
			RemoveRuntimeObjects(player, it->second);
			_runtimes.erase(it);
			ReconcileRelicPools();
			return;
		}
		// Do not let a normal logout save make an unconfirmed live reward durable
		// alongside a retained assignment. If the database cannot be read, favor
		// removing the provisional item over creating a repeatable reward.
		auto const provisional = uncertain->second;
		RemoveProvisionalRewards(player, _resources, provisional);
		if (assignment.has_value()) {
			auto cleanup = CharacterDatabase.BeginTransaction();
			player->SaveInventoryAndGoldToDB(cleanup);
			CommitCharacterTransactionAndWait(cleanup);
		} else
			LOG_ERROR("module.native_hunts",
				"Removed provisional Native Hunt rewards for character {} on "
				"logout because the commit result remained unreadable.",
				guid);
		_uncertainTurnIns.erase(uncertain);
	}
	if (!it->second.CrystalGuid.IsEmpty())
		if (GameObject *crystal = ObjectAccessor::GetGameObject(
				*player, it->second.CrystalGuid))
			crystal->Delete();
	it->second.CrystalGuid.Clear();
	if (!it->second.ReturnRiftGuid.IsEmpty())
		if (GameObject *rift = ObjectAccessor::GetGameObject(
				*player, it->second.ReturnRiftGuid))
			rift->Delete();
	it->second.ReturnRiftGuid.Clear();
	if (!it->second.AmbushGuid.IsEmpty())
		if (Creature *ambush = ObjectAccessor::GetCreature(
				*player, it->second.AmbushGuid))
			ambush->DespawnOrUnsummon();
	it->second.AmbushGuid.Clear();
	if (it->second.Aggregate.State == HuntState::PreyActive) {
		if (!it->second.PreyGuid.IsEmpty())
			if (Creature *prey = ObjectAccessor::GetCreature(
					*player, it->second.PreyGuid))
				prey->DespawnOrUnsummon();
		it->second.PreyGuid.Clear();
		HuntDomain::Execute(it->second.Aggregate, RecoverAfterRestart{});
		SaveAssignment(it->second);
		ReconcileHuntAura(player, &it->second.Aggregate);
	}
}
} // namespace native_hunts
