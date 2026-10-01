#include "NativeHuntsManager.h"

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "GameObject.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuras.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>

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
	ActionStats,
	ActionGuardHuntmaster
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
		case ActionRequestElite: sNativeHunts.RequestEliteHunt(player, creature, message); break;
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
