#include "NativeHuntsManager.h"

#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "GameObject.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace {
native_hunts::NativeHuntsConfig Config;

enum GossipAction : std::uint32_t {
	ActionRequest = GOSSIP_ACTION_INFO_DEF + 1,
	ActionStatus,
	ActionTurnIn,
	ActionAbandon,
	ActionStats
};

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
		Config.RewardSeals = std::clamp<std::uint32_t>(
			sConfigMgr->GetOption<std::uint32_t>(
				"NativeHunts.Reward.Seals", 1),
			1, 200);
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
		ShowMenu(player, creature);
		return true;
	}

	bool OnGossipSelect(Player *player, Creature *creature,
						std::uint32_t /*sender*/, std::uint32_t action) override {
		std::string message;
		switch (action) {
		case ActionRequest: sNativeHunts.RequestHunt(player, creature, message); break;
		case ActionTurnIn: sNativeHunts.TurnIn(player, creature, message); break;
		case ActionAbandon: sNativeHunts.Abandon(player, message); break;
		case ActionStats:
			message = "Lifetime completed Hunts: " +
					  std::to_string(sNativeHunts.LifetimeCompletions(player));
			break;
		case ActionStatus:
		default:
			if (auto const *runtime = sNativeHunts.GetRuntime(player))
				message = StateText(*runtime);
			else
				message = "You have no active Hunt.";
			break;
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
		if (!runtime)
			AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I seek dangerous prey.",
				GOSSIP_SENDER_MAIN, ActionRequest);
		else if (runtime->Aggregate.State == native_hunts::HuntState::ReadyToTurnIn)
			AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I have slain my quarry.",
				GOSSIP_SENDER_MAIN, ActionTurnIn);
		else {
			AddGossipItemFor(player, GOSSIP_ICON_CHAT,
				"Tell me about my current Hunt.", GOSSIP_SENDER_MAIN, ActionStatus);
			AddGossipItemFor(player, GOSSIP_ICON_CHAT,
				"I wish to abandon this Hunt.", GOSSIP_SENDER_MAIN, ActionAbandon);
		}
		AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Show me my hunting record.",
			GOSSIP_SENDER_MAIN, ActionStats);
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
	void OnPlayerCreatureKill(Player *killer, Creature *killed) override {
		sNativeHunts.OnCreatureKill(killer, killed);
	}
	void OnPlayerCreatureKilledByPet(Player *owner, Creature *killed) override {
		sNativeHunts.OnCreatureKill(owner, killed);
	}
	void OnPlayerBeforeLogout(Player *player) override {
		sNativeHunts.OnLogout(player);
	}
};
} // namespace

void AddNativeHuntsModuleScripts() {
	new NativeHuntsWorldScript();
	new NativeHuntmasterScript();
	new NativeTrailCrystalScript();
	new NativeReturnRiftScript();
	new NativeHuntsPlayerScript();
}
