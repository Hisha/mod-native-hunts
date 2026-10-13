#include "NativeHuntsManager.h"

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "GameObject.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuras.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
native_hunts::NativeHuntsConfig Config;

Player *GetCommandPlayer(ChatHandler *handler) {
	return handler && handler->GetSession()
		? handler->GetSession()->GetPlayer()
		: nullptr;
}

enum GossipAction : std::uint32_t {
	ActionRequest = GOSSIP_ACTION_INFO_DEF + 1,
	ActionRequestElite,
	ActionStatus,
	ActionTurnIn,
	ActionAbandon,
	ActionSealStore = GOSSIP_ACTION_INFO_DEF + 20,
	ActionSealSpecBase = GOSSIP_ACTION_INFO_DEF + 100,
	ActionSealTierBase = GOSSIP_ACTION_INFO_DEF + 110,
	ActionSealSlotBase = GOSSIP_ACTION_INFO_DEF + 120,
	ActionSealItemBase = GOSSIP_ACTION_INFO_DEF + 200,
	ActionSealBuyConfirm = GOSSIP_ACTION_INFO_DEF + 250,
	ActionSealCancel = GOSSIP_ACTION_INFO_DEF + 251,
	ActionGuardHuntmaster = GOSSIP_ACTION_INFO_DEF + 700
};

struct SealSpecChoice {
	std::uint32_t Spec = 0;
	char const *Name = "";
};

struct SealStoreContext {
	std::uint32_t Spec = 0;
	std::uint8_t Tier = 0;
	native_hunts::SealStoreSlot Slot = native_hunts::SealStoreSlot::Weapon;
	std::vector<std::uint32_t> ItemIds;
	std::uint32_t PendingItemId = 0;
};

std::unordered_map<std::uint32_t, SealStoreContext> SealStoreContexts;

std::vector<SealSpecChoice> GetSealSpecs(Player const *player) {
	if (!player)
		return {};
	switch (player->getClass()) {
	case CLASS_WARRIOR: return {{TALENT_TREE_WARRIOR_ARMS, "Arms"}, {TALENT_TREE_WARRIOR_FURY, "Fury"}, {TALENT_TREE_WARRIOR_PROTECTION, "Protection"}};
	case CLASS_PALADIN: return {{TALENT_TREE_PALADIN_HOLY, "Holy"}, {TALENT_TREE_PALADIN_PROTECTION, "Protection"}, {TALENT_TREE_PALADIN_RETRIBUTION, "Retribution"}};
	case CLASS_HUNTER: return {{TALENT_TREE_HUNTER_BEAST_MASTERY, "Beast Mastery"}, {TALENT_TREE_HUNTER_MARKSMANSHIP, "Marksmanship"}, {TALENT_TREE_HUNTER_SURVIVAL, "Survival"}};
	case CLASS_ROGUE: return {{TALENT_TREE_ROGUE_ASSASSINATION, "Assassination"}, {TALENT_TREE_ROGUE_COMBAT, "Combat"}, {TALENT_TREE_ROGUE_SUBTLETY, "Subtlety"}};
	case CLASS_PRIEST: return {{TALENT_TREE_PRIEST_DISCIPLINE, "Discipline"}, {TALENT_TREE_PRIEST_HOLY, "Holy"}, {TALENT_TREE_PRIEST_SHADOW, "Shadow"}};
	case CLASS_DEATH_KNIGHT: return {{TALENT_TREE_DEATH_KNIGHT_BLOOD, "Blood"}, {TALENT_TREE_DEATH_KNIGHT_FROST, "Frost"}, {TALENT_TREE_DEATH_KNIGHT_UNHOLY, "Unholy"}};
	case CLASS_SHAMAN: return {{TALENT_TREE_SHAMAN_ELEMENTAL, "Elemental"}, {TALENT_TREE_SHAMAN_ENHANCEMENT, "Enhancement"}, {TALENT_TREE_SHAMAN_RESTORATION, "Restoration"}};
	case CLASS_MAGE: return {{TALENT_TREE_MAGE_ARCANE, "Arcane"}, {TALENT_TREE_MAGE_FIRE, "Fire"}, {TALENT_TREE_MAGE_FROST, "Frost"}};
	case CLASS_WARLOCK: return {{TALENT_TREE_WARLOCK_AFFLICTION, "Affliction"}, {TALENT_TREE_WARLOCK_DEMONOLOGY, "Demonology"}, {TALENT_TREE_WARLOCK_DESTRUCTION, "Destruction"}};
	case CLASS_DRUID: return {{TALENT_TREE_DRUID_BALANCE, "Balance"}, {TALENT_TREE_DRUID_FERAL_COMBAT, "Feral"}, {TALENT_TREE_DRUID_RESTORATION, "Restoration"}};
	default: return {};
	}
}

char const *GetSealSlotName(native_hunts::SealStoreSlot slot) {
	switch (slot) {
	case native_hunts::SealStoreSlot::Weapon: return "Weapons";
	case native_hunts::SealStoreSlot::Head: return "Head";
	case native_hunts::SealStoreSlot::Neck: return "Neck";
	case native_hunts::SealStoreSlot::Shoulder: return "Shoulders";
	case native_hunts::SealStoreSlot::Back: return "Back";
	case native_hunts::SealStoreSlot::Chest: return "Chest";
	case native_hunts::SealStoreSlot::Wrist: return "Wrists";
	case native_hunts::SealStoreSlot::Hands: return "Hands";
	case native_hunts::SealStoreSlot::Waist: return "Waist";
	case native_hunts::SealStoreSlot::Legs: return "Legs";
	case native_hunts::SealStoreSlot::Feet: return "Feet";
	case native_hunts::SealStoreSlot::Ring: return "Rings";
	case native_hunts::SealStoreSlot::Trinket: return "Trinkets";
	case native_hunts::SealStoreSlot::OffHand: return "Off-hand / Shields";
	case native_hunts::SealStoreSlot::Relic: return "Ranged / Relics";
	}
	return "Equipment";
}

std::string StateText(native_hunts::HuntRuntime const &runtime) {
	auto const &hunt = runtime.Aggregate;
	std::string text = "Quarry: " + hunt.Identity.PreyName + " | Zone: " +
					   hunt.Identity.ZoneName + " | Tracking: " +
					   std::to_string(hunt.Progress) + "%";
	if (hunt.State == native_hunts::HuntState::FinalRevealed)
		text += " | Final trail revealed near " + hunt.FinalLocationName;
	else if (hunt.State == native_hunts::HuntState::PreyActive)
		text += " | Final prey active";
	else if (hunt.State == native_hunts::HuntState::ReadyToTurnIn)
		text += " | Ready to turn in";
	return text;
}

class NativeHuntsWorldScript final : public WorldScript {
public:
	NativeHuntsWorldScript() :
		WorldScript("NativeHuntsWorldScript",
					{WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP,
					 WORLDHOOK_ON_UPDATE}) {}

	void OnAfterConfigLoad(bool /*reload*/) override {
		Config.Enabled = sConfigMgr->GetOption<bool>("NativeHunts.Enable", true);
		Config.Debug = sConfigMgr->GetOption<bool>("NativeHunts.Debug", false);
		Config.MinimumLevel = std::max<std::uint32_t>(
			1, sConfigMgr->GetOption<std::uint32_t>(
				   "NativeHunts.MinimumLevel", 10));
		Config.XpMultiplier = std::max(0.0f, sConfigMgr->GetOption<float>(
			"NativeHunts.XPMultiplier", 0.75f));
		std::uint32_t const a = std::clamp<std::uint32_t>(
			sConfigMgr->GetOption<std::uint32_t>(
				"NativeHunts.Tracking.ProgressMin", 3),
			1, 100);
		std::uint32_t const b = std::clamp<std::uint32_t>(
			sConfigMgr->GetOption<std::uint32_t>(
				"NativeHunts.Tracking.ProgressMax", 7),
			1, 100);
		Config.TrackingProgressMin = std::min(a, b);
		Config.TrackingProgressMax = std::max(a, b);
		Config.GroupCreditRadius = std::clamp(
			sConfigMgr->GetOption<float>("NativeHunts.GroupCreditRadius", 100.0f),
			1.0f, 200.0f);
		std::string scope = sConfigMgr->GetOption<std::string>(
			"NativeHunts.AssignmentScope", "Local");
		std::transform(scope.begin(), scope.end(), scope.begin(),
			[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
		Config.SearchScope = scope == "world"
			? native_hunts::HuntSearchScope::World
			: scope == "continent" ? native_hunts::HuntSearchScope::Continent
								 : native_hunts::HuntSearchScope::Local;
		Config.AmbushCount = static_cast<std::uint8_t>(std::clamp<std::uint32_t>(
			sConfigMgr->GetOption<std::uint32_t>("NativeHunts.Ambush.Count", 2),
			0, 5));
		Config.AmbushHealthMultiplier = sConfigMgr->GetOption<float>(
			"NativeHunts.Ambush.HealthMultiplier", 4.0f);
		if (!std::isfinite(Config.AmbushHealthMultiplier))
			Config.AmbushHealthMultiplier = 4.0f;
		Config.AmbushHealthMultiplier = std::clamp(
			Config.AmbushHealthMultiplier, 1.0f, 20.0f);
		Config.AmbushEscapeHealthPercent = sConfigMgr->GetOption<float>(
			"NativeHunts.Ambush.EscapeHealthPercent", 50.0f);
		if (!std::isfinite(Config.AmbushEscapeHealthPercent))
			Config.AmbushEscapeHealthPercent = 50.0f;
		Config.AmbushEscapeHealthPercent = std::clamp(
			Config.AmbushEscapeHealthPercent, 1.0f, 99.0f);
		Config.EliteRequiredStandardCompletions = sConfigMgr->GetOption<std::uint32_t>("NativeHunts.Elite.RequiredStandardCompletions", 10);
		Config.EliteDailyLimit = sConfigMgr->GetOption<std::uint32_t>("NativeHunts.Elite.DailyLimit", 1);
		Config.EliteHealthMultiplier = std::max(0.1f, sConfigMgr->GetOption<float>("NativeHunts.Elite.HealthMultiplier", 1.0f));
		Config.EliteDamageMultiplier = std::max(0.1f, sConfigMgr->GetOption<float>("NativeHunts.Elite.DamageMultiplier", 1.0f));
		Config.EliteArmorMultiplier = std::max(0.1f, sConfigMgr->GetOption<float>("NativeHunts.Elite.ArmorMultiplier", 1.0f));
		Config.EliteXpMultiplier = std::max(0.0f, sConfigMgr->GetOption<float>("NativeHunts.Elite.XPMultiplier", 1.0f));
		Config.EliteGoldMultiplier = std::max(0.0f, sConfigMgr->GetOption<float>("NativeHunts.Elite.GoldMultiplier", 1.0f));
		Config.EliteSealMinimumLevel = sConfigMgr->GetOption<std::uint32_t>("NativeHunts.Elite.SealMinimumLevel", 80);
		Config.EliteSealsPerCompletion = sConfigMgr->GetOption<std::uint32_t>("NativeHunts.Elite.SealsPerCompletion", 1);
		Config.EliteEndgameRewardLevel = sConfigMgr->GetOption<std::uint32_t>("NativeHunts.Elite.EndgameRewardLevel", 80);
		Config.EliteEndgameRewardMinItemLevel = sConfigMgr->GetOption<std::uint32_t>("NativeHunts.Elite.EndgameRewardMinItemLevel", 200);
		Config.EliteEndgameRewardMaxItemLevel = sConfigMgr->GetOption<std::uint32_t>("NativeHunts.Elite.EndgameRewardMaxItemLevel", 200);
		Config.EliteRewardRequireUpgrade = sConfigMgr->GetOption<bool>("NativeHunts.Elite.RewardRequireUpgrade", true);
		Config.EliteRewardUpgradePoolPct = std::clamp(sConfigMgr->GetOption<float>("NativeHunts.Elite.RewardUpgradePoolPct", .70f), 0.0f, 1.0f);
		Config.EliteNoUpgradeBonusSeals = sConfigMgr->GetOption<std::uint32_t>("NativeHunts.Elite.NoUpgradeBonusSeals", 1);
		std::array<std::uint32_t, 4> const defaultCosts{{5, 10, 20, 30}};
		std::array<std::uint32_t, 4> const defaultMins{{213, 226, 245, 264}};
		std::array<std::uint32_t, 4> const defaultMaxes{{219, 232, 251, 264}};
		for (std::size_t index = 0; index < 4; ++index) {
			std::string const prefix = "NativeHunts.SealStore.Tier" +
				std::to_string(index + 1);
			Config.SealStoreTierCost[index] = sConfigMgr->GetOption<std::uint32_t>(
				prefix + ".Cost", defaultCosts[index]);
			std::uint32_t const minimum = sConfigMgr->GetOption<std::uint32_t>(
				prefix + ".MinItemLevel", defaultMins[index]);
			std::uint32_t const maximum = sConfigMgr->GetOption<std::uint32_t>(
				prefix + ".MaxItemLevel", defaultMaxes[index]);
			Config.SealStoreTierMinItemLevel[index] = std::min(minimum, maximum);
			Config.SealStoreTierMaxItemLevel[index] = std::max(minimum, maximum);
		}
		Config.ReturnRiftEnabled = sConfigMgr->GetOption<bool>(
			"NativeHunts.ReturnRift.Enable", true);
		Config.ReturnRiftDurationSeconds = std::clamp<std::uint32_t>(
			sConfigMgr->GetOption<std::uint32_t>(
				"NativeHunts.ReturnRift.DurationSeconds", 120),
			1, 120);
		Config.ReturnRiftArrivalDistance = std::clamp(
			sConfigMgr->GetOption<float>(
				"NativeHunts.ReturnRift.ArrivalDistance", 3.0f),
			1.0f, 10.0f);
		sNativeHunts.Configure(Config);
	}

	void OnStartup() override { sNativeHunts.Initialize(); }
	void OnUpdate(std::uint32_t elapsedMs) override {
		sNativeHunts.Update(elapsedMs);
	}
};

class NativeHuntmasterScript final : public CreatureScript {
public:
	NativeHuntmasterScript() :
		CreatureScript("mod_native_hunts_huntmaster") {}

	bool OnGossipHello(Player *player, Creature *creature) override {
		if (!player)
			return true;
		if (!sNativeHunts.IsEnabled() ||
			!sNativeHunts.CanOpenHuntmasterGossip(player, creature)) {
			CloseGossipMenuFor(player);
			return true;
		}
		SealStoreContexts.erase(player->GetGUID().GetCounter());
		ShowMenu(player, creature);
		return true;
	}

	bool OnGossipSelect(Player *player, Creature *creature,
						std::uint32_t sender, std::uint32_t action) override {
		if (!player)
			return true;
		if (!sNativeHunts.IsEnabled() ||
			!sNativeHunts.CanOpenHuntmasterGossip(player, creature)) {
			CloseGossipMenuFor(player);
			return true;
		}
		if (sender != GOSSIP_SENDER_MAIN) {
			SealStoreContexts.erase(player->GetGUID().GetCounter());
			CloseGossipMenuFor(player);
			return true;
		}
		std::uint32_t const guid = player->GetGUID().GetCounter();
		if (action == ActionSealStore) {
			SealStoreContexts[guid] = {};
			ShowSealSpecMenu(player, creature);
			return true;
		}
		if (action >= ActionSealSpecBase && action < ActionSealSpecBase + 3) {
			auto specs = GetSealSpecs(player);
			std::uint32_t const index = action - ActionSealSpecBase;
			if (index < specs.size()) {
				SealStoreContexts[guid].Spec = specs[index].Spec;
				ShowSealTierMenu(player, creature);
				return true;
			}
		}
		if (action >= ActionSealTierBase && action < ActionSealTierBase + 4) {
			auto context = SealStoreContexts.find(guid);
			if (context != SealStoreContexts.end()) {
				context->second.Tier = static_cast<std::uint8_t>(
					action - ActionSealTierBase + 1);
				ShowSealSlotMenu(player, creature);
				return true;
			}
		}
		if (action >= ActionSealSlotBase &&
			action <= ActionSealSlotBase + static_cast<std::uint32_t>(
				native_hunts::SealStoreSlot::Relic)) {
			auto context = SealStoreContexts.find(guid);
			if (context != SealStoreContexts.end() && context->second.Tier) {
				context->second.Slot = static_cast<native_hunts::SealStoreSlot>(
					action - ActionSealSlotBase);
				ShowSealItemMenu(player, creature);
				return true;
			}
		}
		if (action >= ActionSealItemBase && action < ActionSealItemBase + 20) {
			auto context = SealStoreContexts.find(guid);
			std::uint32_t const index = action - ActionSealItemBase;
			if (context != SealStoreContexts.end() &&
				index < context->second.ItemIds.size()) {
				context->second.PendingItemId = context->second.ItemIds[index];
				ShowSealConfirmation(player, creature);
				return true;
			}
		}
		if (action == ActionSealBuyConfirm) {
			auto context = SealStoreContexts.find(guid);
			SealStoreContext purchase;
			bool const valid = context != SealStoreContexts.end() &&
				context->second.PendingItemId;
			if (valid)
				purchase = context->second;
			// Consume the one-shot server selection before executing the purchase.
			// Replayed/duplicate confirmation packets therefore fail closed.
			SealStoreContexts.erase(guid);
			std::string message;
			if (valid)
				sNativeHunts.PurchaseSealStoreItem(player, purchase.Spec,
					purchase.Tier, purchase.PendingItemId, message);
			else
				message = "That Huntmaster reward selection expired. Please choose it again.";
			ChatHandler(player->GetSession()).PSendSysMessage(
				"|cff33ccff[Native Hunts]|r {}", message);
			CloseGossipMenuFor(player);
			return true;
		}
		if (action == ActionSealCancel) {
			SealStoreContexts.erase(guid);
			ShowMenu(player, creature);
			return true;
		}
		std::string message;
		switch (action) {
		case ActionRequest: sNativeHunts.RequestHunt(player, creature, message); break;
		case ActionRequestElite: sNativeHunts.RequestEliteHunt(player, creature, message); break;
		case ActionTurnIn: sNativeHunts.TurnIn(player, creature, message); break;
		case ActionAbandon: sNativeHunts.Abandon(player, message); break;
		case ActionStatus:
			if (auto const *runtime = sNativeHunts.GetRuntime(player))
				message = StateText(*runtime);
			else
				message = "You have no active Hunt.";
			break;
		default:
			SealStoreContexts.erase(guid);
			CloseGossipMenuFor(player);
			return true;
		}
		ChatHandler(player->GetSession()).PSendSysMessage(
			"|cff33ccff[Native Hunts]|r {}", message);
		CloseGossipMenuFor(player);
		return true;
	}

private:
	static void ShowMenu(Player *player, Creature *creature) {
		ClearGossipMenuFor(player);
		auto const *runtime = sNativeHunts.GetRuntime(player);

		if (!runtime) {
			AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I seek dangerous prey.",
				GOSSIP_SENDER_MAIN, ActionRequest);

			if (sNativeHunts.IsEliteUnlocked(player) &&
				sNativeHunts.IsEliteAvailableToday(player))
				AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I seek an Elite Hunt.",
					GOSSIP_SENDER_MAIN, ActionRequestElite);
		} else if (runtime->Aggregate.State ==
			native_hunts::HuntState::ReadyToTurnIn) {
			AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I have slain my quarry.",
				GOSSIP_SENDER_MAIN, ActionTurnIn);
		} else {
			AddGossipItemFor(player, GOSSIP_ICON_CHAT,
				"Tell me about my current Hunt.", GOSSIP_SENDER_MAIN, ActionStatus);
			AddGossipItemFor(player, GOSSIP_ICON_CHAT,
				"I wish to abandon this Hunt.", GOSSIP_SENDER_MAIN, ActionAbandon);
		}
		if (sNativeHunts.IsSealStoreAvailable(player)) {
			std::uint32_t const seals = sNativeHunts.GetSealBalance(player);
			std::ostringstream label;
			label << "Browse Huntmaster's Seal rewards. (" << seals << " Seal"
				<< (seals == 1 ? "" : "s") << ")";
			AddGossipItemFor(player, GOSSIP_ICON_VENDOR, label.str(),
				GOSSIP_SENDER_MAIN, ActionSealStore);
		}
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

	static void ShowSealSpecMenu(Player *player, Creature *creature) {
		ClearGossipMenuFor(player);
		auto specs = GetSealSpecs(player);
		for (std::uint32_t index = 0; index < specs.size() && index < 3; ++index)
			AddGossipItemFor(player, GOSSIP_ICON_CHAT,
				std::string("Shop for ") + specs[index].Name + " gear.",
				GOSSIP_SENDER_MAIN, ActionSealSpecBase + index);
		AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Never mind.",
			GOSSIP_SENDER_MAIN, ActionSealCancel);
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

	static void ShowSealTierMenu(Player *player, Creature *creature) {
		ClearGossipMenuFor(player);
		for (std::uint8_t tier = 1; tier <= 4; ++tier) {
			std::uint32_t const cost = sNativeHunts.GetSealStoreTierCost(tier);
			if (!cost)
				continue;
			std::ostringstream label;
			label << "Tier " << static_cast<std::uint32_t>(tier) << " - ilvl "
				<< sNativeHunts.GetSealStoreTierMinItemLevel(tier);
			if (sNativeHunts.GetSealStoreTierMaxItemLevel(tier) !=
				sNativeHunts.GetSealStoreTierMinItemLevel(tier))
				label << "-" << sNativeHunts.GetSealStoreTierMaxItemLevel(tier);
			label << " - " << cost << " Seals";
			AddGossipItemFor(player, GOSSIP_ICON_CHAT, label.str(),
				GOSSIP_SENDER_MAIN, ActionSealTierBase + tier - 1);
		}
		AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Never mind.",
			GOSSIP_SENDER_MAIN, ActionSealCancel);
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

	static void ShowSealSlotMenu(Player *player, Creature *creature) {
		ClearGossipMenuFor(player);
		for (std::uint8_t raw = static_cast<std::uint8_t>(
				native_hunts::SealStoreSlot::Weapon);
			raw <= static_cast<std::uint8_t>(native_hunts::SealStoreSlot::Relic);
			++raw) {
			auto const slot = static_cast<native_hunts::SealStoreSlot>(raw);
			AddGossipItemFor(player, GOSSIP_ICON_CHAT, GetSealSlotName(slot),
				GOSSIP_SENDER_MAIN, ActionSealSlotBase + raw);
		}
		AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Never mind.",
			GOSSIP_SENDER_MAIN, ActionSealCancel);
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

	static void ShowSealItemMenu(Player *player, Creature *creature) {
		ClearGossipMenuFor(player);
		auto &context = SealStoreContexts[player->GetGUID().GetCounter()];
		context.ItemIds.clear();
		context.PendingItemId = 0;
		auto items = sNativeHunts.BuildSealStoreItems(player, context.Spec,
			context.Tier, context.Slot);
		std::uint32_t const cost =
			sNativeHunts.GetSealStoreTierCost(context.Tier);
		for (std::uint32_t index = 0; index < items.size() && index < 20; ++index) {
			context.ItemIds.push_back(items[index].ItemId);
			std::ostringstream label;
			label << items[index].Name << " (ilvl " << items[index].ItemLevel
				<< ") - " << cost << " Seals";
			AddGossipItemFor(player, GOSSIP_ICON_CHAT, label.str(),
				GOSSIP_SENDER_MAIN, ActionSealItemBase + index);
		}
		if (items.empty())
			AddGossipItemFor(player, GOSSIP_ICON_CHAT,
				"No suitable rewards are available in this category.",
				GOSSIP_SENDER_MAIN, ActionSealCancel);
		AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Never mind.",
			GOSSIP_SENDER_MAIN, ActionSealCancel);
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

	static void ShowSealConfirmation(Player *player, Creature *creature) {
		ClearGossipMenuFor(player);
		auto const &context = SealStoreContexts[player->GetGUID().GetCounter()];
		ItemTemplate const *item = sObjectMgr->GetItemTemplate(context.PendingItemId);
		std::uint32_t const cost =
			sNativeHunts.GetSealStoreTierCost(context.Tier);
		std::ostringstream label;
		label << "Purchase " << (item ? item->Name1 : "this item") << " for "
			<< cost << " Seals. (Balance: " << sNativeHunts.GetSealBalance(player)
			<< ")";
		AddGossipItemFor(player, GOSSIP_ICON_CHAT, label.str(),
			GOSSIP_SENDER_MAIN, ActionSealBuyConfirm);
		AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Cancel purchase.",
			GOSSIP_SENDER_MAIN, ActionSealCancel);
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}
};

class NativeTrailCrystalScript final : public GameObjectScript {
public:
	NativeTrailCrystalScript() :
		GameObjectScript("mod_native_hunts_trail_crystal") {}
	bool OnGossipHello(Player *player, GameObject *object) override {
		std::string message;
		sNativeHunts.UseCrystal(player, object, message);
		if (!message.empty())
			ChatHandler(player->GetSession()).PSendSysMessage(
				"|cff33ccff[Native Hunts]|r {}", message);
		return true;
	}
};

class NativeReturnRiftScript final : public GameObjectScript {
public:
	NativeReturnRiftScript() :
		GameObjectScript("mod_native_hunts_return_rift") {}
	bool OnGossipHello(Player *player, GameObject *object) override {
		std::string message;
		sNativeHunts.UseReturnRift(player, object, message);
		if (!message.empty())
			ChatHandler(player->GetSession()).PSendSysMessage(
				"|cff33ccff[Native Hunts]|r {}", message);
		return true;
	}
};

class NativeHuntsPlayerScript final : public PlayerScript {
public:
	NativeHuntsPlayerScript() : PlayerScript("NativeHuntsPlayerScript") {}
	void OnPlayerBeforeSendChatMessage(Player *player, std::uint32_t &type,
			std::uint32_t &language, std::string &message) override {
		if (!player || language != LANG_ADDON || type != CHAT_MSG_WHISPER)
			return;
		static std::string const prefix = "NHUNTS\t";
		if (message.compare(0, prefix.size(), prefix) != 0)
			return;
		sNativeHunts.HandleUiAddonMessage(player,
			message.substr(prefix.size()));
		message.clear();
	}
	void OnPlayerLogin(Player *player) override {
		sNativeHunts.OnLogin(player);
	}
	void OnPlayerCreatureKill(Player *killer, Creature *killed) override {
		sNativeHunts.OnCreatureKill(killer, killed);
	}
	void OnPlayerCreatureKilledByPet(Player *owner, Creature *killed) override {
		sNativeHunts.OnCreatureKill(owner, killed);
	}
	void OnPlayerBeforeLogout(Player *player) override {
		if (player)
			SealStoreContexts.erase(player->GetGUID().GetCounter());
		sNativeHunts.OnLogout(player);
	}
};

class NativeHuntGuardLocatorScript final : public AllCreatureScript {
public:
	NativeHuntGuardLocatorScript() :
		AllCreatureScript("NativeHuntGuardLocatorScript") {}
	bool CanCreatureGossipHello(Player *player, Creature *creature) override {
		if (!sNativeHunts.IsEnabled() || !player || !creature ||
			!sNativeHunts.IsGuardLocator(creature->GetEntry()))
			return false;
		player->PrepareGossipMenu(creature, creature->GetGossipMenuId(), true);
		AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Where is the Huntmaster?",
			GOSSIP_SENDER_MAIN, ActionGuardHuntmaster);
		player->SendPreparedGossip(creature);
		return true;
	}
	bool CanCreatureGossipSelect(Player *player, Creature *creature,
			std::uint32_t, std::uint32_t action) override {
		if (action != ActionGuardHuntmaster || !player || !creature ||
			!sNativeHunts.IsGuardLocator(creature->GetEntry()))
			return false;
		std::string message;
		sNativeHunts.SendHuntmasterLocation(player, creature->GetEntry(), message);
		ChatHandler(player->GetSession()).PSendSysMessage(
			"|cff33ccff[Native Hunts]|r {}", message);
		CloseGossipMenuFor(player);
		return true;
	}
};

class NativeElitePreyAI final : public ScriptedAI {
public:
	explicit NativeElitePreyAI(Creature *creature):ScriptedAI(creature) {}
	void Reset() override { ClearStunDiminishing(); }
	void SpellHit(Unit *caster, SpellInfo const *spell) override {
		if (!caster || !spell || !caster->GetCharmerOrOwnerPlayerOrPlayerItself() ||
			!(spell->GetAllEffectsMechanicMask() & (1u << MECHANIC_STUN))) return;
		Aura *aura=me->GetAura(spell->Id,caster->GetGUID());
		if (!aura) return;
		if (!_stunResetMs) _stunApplications=0;
		++_stunApplications;
		std::int32_t duration=aura->GetDuration();
		if (_stunApplications==2) duration=std::max<std::int32_t>(1,duration/2);
		else if (_stunApplications>=3) duration=std::max<std::int32_t>(1,duration/4);
		aura->SetDuration(duration);
		_stunResetMs=static_cast<std::uint32_t>(std::max<std::int32_t>(0,duration))+15000u;
		if (_stunApplications>=3 && !_stunImmune) {
			me->ApplySpellImmune(0,IMMUNITY_MECHANIC,MECHANIC_STUN,true);
			_stunImmune=true;
		}
	}
	void UpdateAI(std::uint32_t diff) override {
		if (_stunResetMs) { if (_stunResetMs<=diff) ClearStunDiminishing(); else _stunResetMs-=diff; }
		if (UpdateVictim()) DoMeleeAttackIfReady();
	}
private:
	void ClearStunDiminishing() {
		if (_stunImmune) me->ApplySpellImmune(0,IMMUNITY_MECHANIC,MECHANIC_STUN,false);
		_stunApplications=0; _stunResetMs=0; _stunImmune=false;
	}
	std::uint8_t _stunApplications=0; std::uint32_t _stunResetMs=0; bool _stunImmune=false;
};

class NativeElitePreyScript final : public AllCreatureScript {
public:
	NativeElitePreyScript():AllCreatureScript("NativeElitePreyScript") {}
	CreatureAI *GetCreatureAI(Creature *creature) const override {
		return creature && sNativeHunts.IsEnabled() && sNativeHunts.IsElitePrey(creature->GetEntry())
			? new NativeElitePreyAI(creature) : nullptr;
	}
};

class NativeHuntsCommandScript final : public CommandScript {
public:
	NativeHuntsCommandScript() : CommandScript("NativeHuntsCommandScript") {}

	Acore::ChatCommands::ChatCommandTable GetCommands() const override {
		using namespace Acore::ChatCommands;
		static ChatCommandTable nativeHunts = {
			{"status", HandleStatus, rbac::RBAC_PERM_COMMAND_SERVER_INFO,
			 Console::No},
			{"reset", HandleReset, rbac::RBAC_PERM_COMMAND_SERVER_INFO,
			 Console::No},
		};
		static ChatCommandTable root = {{"nativehunts", nativeHunts}};
		return root;
	}

private:
	static bool HandleStatus(ChatHandler *handler) {
		Player *player = GetCommandPlayer(handler);
		handler->SendSysMessage(sNativeHunts.BuildStatus(player, true));
		return true;
	}

	static bool HandleReset(ChatHandler *handler) {
		Player *player = GetCommandPlayer(handler);
		if (!player)
			return false;
		std::string message;
		sNativeHunts.Abandon(player, message);
		handler->PSendSysMessage("[Native Hunts] {}", message);
		return true;
	}
};
} // namespace

void AddNativeHuntsModuleScripts() {
	new NativeHuntsWorldScript();
	new NativeHuntmasterScript();
	new NativeTrailCrystalScript();
	new NativeReturnRiftScript();
	new NativeHuntsPlayerScript();
	new NativeHuntGuardLocatorScript();
	new NativeElitePreyScript();
	new NativeHuntsCommandScript();
}
