#include "NativeHuntsManager.h"

#include "HuntCatalog.h"
#include "HuntGameplay.h"
#include "api/ContentResourceApiV1.h"

#include "Creature.h"
#include "CreatureAI.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Formulas.h"
#include "GameObject.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
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

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace native_hunts {
namespace {
constexpr char Package[] = "mod-native-hunts";

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
} // namespace

NativeHuntsManager &NativeHuntsManager::Instance() {
	static NativeHuntsManager instance;
	return instance;
}

void NativeHuntsManager::Configure(NativeHuntsConfig config) {
	_config = std::move(config);
}

bool NativeHuntsManager::IsEnabled() const {
	return _config.Enabled && _resources.Ready;
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
	if (!resolve("prey-trail-crystal", "gameobject-template.id",
				 _resources.TrailCrystalEntry) ||
		!resolve("return-rift", "gameobject-template.id",
				 _resources.ReturnRiftEntry) ||
		!resolve("huntmaster-seal", "item.id", _resources.SealItemEntry))
		return false;

	auto const *crystal =
		sObjectMgr->GetGameObjectTemplate(_resources.TrailCrystalEntry);
	auto const *rift = sObjectMgr->GetGameObjectTemplate(_resources.ReturnRiftEntry);
	auto const scriptedGoober = [](GameObjectTemplate const *value) {
		return value && value->type == GAMEOBJECT_TYPE_GOOBER &&
			value->goober.lockId == 0 && value->goober.questId == 0 &&
			value->goober.eventId == 0 && value->goober.autoCloseTime == 0 &&
			value->goober.customAnim == 0 && value->goober.consumable == 0 &&
			value->goober.cooldown == 0 && value->goober.pageId == 0 &&
			value->goober.spellId == 0 && value->goober.linkedTrapId == 0 &&
			value->goober.gossipID == 0;
	};
	if (!scriptedGoober(crystal) || !scriptedGoober(rift) ||
		!sObjectMgr->GetItemTemplate(_resources.SealItemEntry)) {
		_resources.Reason =
			"managed scripted goobers/items are missing, stale, or carry default "
			"lock/quest/spell behavior; rebuild content and restart worldserver";
		return false;
	}
	for (auto const &[symbol, entry] : _resources.CreatureEntries)
		if (!sObjectMgr->GetCreatureTemplate(entry)) {
			_resources.Reason = symbol +
				": managed creature is applied but not loaded; restart worldserver";
			return false;
		}

	_resources.Ready = true;
	_resources.Reason = "validated ACTIVE/APPLIED Native Hunts resources";
	return true;
}

void NativeHuntsManager::Initialize() {
	_runtimes.clear();
	_uncertainTurnIns.clear();
	if (!_config.Enabled) {
		_resources.Reason = "Native Hunts is disabled by configuration";
		return;
	}
	ResolveManagedResources();
	LoadAssignments();
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

void NativeHuntsManager::Shutdown() {
	for (auto &[guid, runtime] : _runtimes)
		RemoveRuntimeObjects(ConnectedPlayer(guid), runtime);
	_runtimes.clear();
	_uncertainTurnIns.clear();
}

void NativeHuntsManager::LoadAssignments() {
	QueryResult result = CharacterDatabase.Query(
		"SELECT `character_guid`,`huntmaster_key`,`huntmaster_entry`,"
		"`huntmaster_spawn_guid`,`prey_key`,`zone_key`,`zone_id`,`map_id`,"
		"`state`,`tracking_progress`,`ambushes_completed`,`ambush_pending`,"
		"`final_site_key`,`revision` "
		"FROM `native_hunt_assignment`");
	if (!result)
		return;
	do {
		Field *field = result->Fetch();
		HuntRuntime runtime;
		runtime.CharacterGuid = field[0].Get<std::uint32_t>();
		std::string const huntmasterKey = field[1].Get<std::string>();
		runtime.HuntmasterEntry = field[2].Get<std::uint32_t>();
		runtime.HuntmasterSpawn = field[3].Get<std::uint32_t>();
		std::string const preyKey = field[4].Get<std::string>();
		std::string const zoneKey = field[5].Get<std::string>();
		runtime.ZoneId = field[6].Get<std::uint32_t>();
		runtime.MapId = field[7].Get<std::uint32_t>();
		runtime.Aggregate.State = static_cast<HuntState>(field[8].Get<std::uint8_t>());
		runtime.Aggregate.Progress = field[9].Get<std::uint8_t>();
		runtime.AmbushesCompleted = field[10].Get<std::uint8_t>();
		runtime.AmbushPending = field[11].Get<std::uint8_t>() != 0;
		runtime.Aggregate.FinalLocationKey = field[12].Get<std::string>();
		runtime.Aggregate.Revision = field[13].Get<std::uint64_t>();

		auto const *huntmaster = FindHuntmaster(huntmasterKey);
		auto const *prey = FindPrey(preyKey);
		auto const *zone = FindZone(zoneKey);
		if (!huntmaster || !prey || !zone)
			continue;
		runtime.Aggregate.Identity =
			{huntmaster->Key, huntmaster->Name, huntmaster->City, prey->Key,
			 prey->Name, PreyTier::Standard, zone->ZoneKey, zone->ZoneName};
		if (!runtime.Aggregate.FinalLocationKey.empty()) {
			auto const *site = FindFinalSite(runtime.Aggregate.FinalLocationKey);
			if (!site)
				continue;
			runtime.Aggregate.FinalLocationName = site->ZoneName;
		}
		if (!HuntDomain::IsValid(runtime.Aggregate))
			continue;
		if (runtime.Aggregate.State == HuntState::PreyActive) {
			HuntDomain::Execute(runtime.Aggregate, RecoverAfterRestart{});
			SaveAssignment(runtime);
		}
		_runtimes[runtime.CharacterGuid] = std::move(runtime);
	} while (result->NextRow());
}

void NativeHuntsManager::SaveAssignment(HuntRuntime const &runtime) {
	auto const &aggregate = runtime.Aggregate;
	CharacterDatabase.DirectExecute(
		"INSERT INTO `native_hunt_assignment` "
		"(`character_guid`,`huntmaster_key`,`huntmaster_entry`,"
		"`huntmaster_spawn_guid`,`prey_key`,`tier`,`zone_key`,`zone_id`,"
		"`map_id`,`state`,`tracking_progress`,`ambushes_completed`,"
		"`ambush_pending`,`final_site_key`,`revision`) "
		"VALUES ({},'{}',{},{},'{}',{},'{}',{},{},{},{},{},{},'{}',{}) "
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
		"`revision`=VALUES(`revision`),`updated_at`=CURRENT_TIMESTAMP",
		runtime.CharacterGuid, aggregate.Identity.HuntmasterKey,
		runtime.HuntmasterEntry, runtime.HuntmasterSpawn,
		aggregate.Identity.PreyKey,
		static_cast<std::uint32_t>(aggregate.Identity.Tier),
		aggregate.Identity.ZoneKey, runtime.ZoneId, runtime.MapId,
		static_cast<std::uint32_t>(aggregate.State),
		static_cast<std::uint32_t>(aggregate.Progress),
		static_cast<std::uint32_t>(runtime.AmbushesCompleted),
		static_cast<std::uint32_t>(runtime.AmbushPending),
		aggregate.FinalLocationKey, aggregate.Revision);
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
}

void NativeHuntsManager::DeleteAssignment(HuntRuntime &runtime) {
	Player *player = ConnectedPlayer(runtime.CharacterGuid);
	RemoveRuntimeObjects(player, runtime);
	CharacterDatabase.DirectExecute(
		"DELETE FROM `native_hunt_assignment` WHERE `character_guid`={}",
		runtime.CharacterGuid);
}

bool NativeHuntsManager::IsHuntmaster(std::uint32_t entry) const {
	for (auto const &huntmaster : Huntmasters()) {
		auto const it = _resources.CreatureEntries.find(huntmaster.Symbol);
		if (it != _resources.CreatureEntries.end() && it->second == entry)
			return true;
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
	if (!player || !giver || !IsHuntmaster(giver->GetEntry())) {
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

	HuntmasterDefinition const *huntmaster = nullptr;
	for (auto const &candidate : Huntmasters())
		if (_resources.CreatureEntries.at(candidate.Symbol) == giver->GetEntry()) {
			huntmaster = &candidate;
			break;
		}
	if (!huntmaster) {
		message = "The Huntmaster's managed identity cannot be resolved.";
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
	_runtimes.emplace(guid, runtime);
	SaveAssignment(_runtimes.at(guid));
	message = "Your quarry is " + std::string(prey.Name) + ". Travel to " +
			  site->ZoneName + " and hunt suitable creatures to find its trail.";
	return true;
}

bool NativeHuntsManager::Abandon(Player *player, std::string &message) {
	if (!player) {
		message = "A player is required.";
		return false;
	}
	auto it = _runtimes.find(player->GetGUID().GetCounter());
	if (it == _runtimes.end()) {
		message = "You do not have an active Hunt.";
		return false;
	}
	HuntDomain::Execute(it->second.Aggregate, AbandonHunt{});
	DeleteAssignment(it->second);
	_runtimes.erase(it);
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
	float const multiplier = std::max(1.0f,
		(finalEncounter ? definition->FinalHealthMultiplier *
			FinalLevelScale(player->GetLevel())
						: _config.AmbushHealthMultiplier *
							  AmbushLevelScale(player->GetLevel())));
	std::uint64_t const scaled = static_cast<std::uint64_t>(
		multiplier * static_cast<float>(player->GetMaxHealth()));
	std::uint32_t const health = static_cast<std::uint32_t>(
		std::min<std::uint64_t>(std::numeric_limits<std::uint32_t>::max(),
								 scaled));
	prey->SetMaxHealth(health);
	prey->SetFullHealth();
	std::vector<PreyAbilityDefinition const *> abilities;
	for (auto const &ability : StandardPreyAbilities())
		if (runtime.Aggregate.Identity.PreyKey == ability.PreyKey)
			abilities.push_back(&ability);
	runtime.AbilityOneTimer = abilities.empty()
		? 0
		: urand(abilities[0]->InitialMinMs, abilities[0]->InitialMaxMs);
	runtime.AbilityTwoTimer = abilities.size() < 2
		? 0
		: urand(abilities[1]->InitialMinMs, abilities[1]->InitialMaxMs);
	prey->AI()->AttackStart(player);
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
	message = runtime.Aggregate.Identity.PreyName +
			  " emerges for the final confrontation!";
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

		std::uint8_t const oldProgress = runtime.Aggregate.Progress;
		std::uint8_t amount = static_cast<std::uint8_t>(urand(
			_config.TrackingProgressMin, _config.TrackingProgressMax));
		HuntDomain::Execute(runtime.Aggregate, AdvanceTracking{amount});
		if (runtime.Aggregate.Progress == 100) {
			auto const sites =
				FinalSitesForZone(runtime.Aggregate.Identity.ZoneKey);
			if (!sites.empty()) {
				auto const *site = sites[urand(
					0, static_cast<std::uint32_t>(sites.size() - 1))];
				if (HuntDomain::Execute(runtime.Aggregate,
						RevealFinal{site->Key, site->ZoneName}).Status ==
					TransitionStatus::Applied)
					SendFinalLocationFeedback(hunter, runtime, true);
			}
		} else if (ShouldStartAmbush(oldProgress, runtime.Aggregate.Progress,
				runtime.AmbushesCompleted, _config.AmbushCount,
				runtime.AmbushPending)) {
			runtime.AmbushPending = true;
			std::string ambushMessage;
			if (SpawnAmbush(hunter, runtime, ambushMessage))
				ChatHandler(hunter->GetSession()).PSendSysMessage(
					"|cffff8000[Native Hunts]|r {}", ambushMessage);
		}
		SaveAssignment(runtime);
	}
}

void NativeHuntsManager::UpdatePreyAbilities(Player *player,
										  HuntRuntime &runtime,
										  Creature *prey,
										  std::uint32_t elapsedMs) {
	if (!player || !prey || !prey->IsAlive() || !prey->IsInCombat())
		return;
	std::vector<PreyAbilityDefinition const *> abilities;
	for (auto const &ability : StandardPreyAbilities())
		if (runtime.Aggregate.Identity.PreyKey == ability.PreyKey)
			abilities.push_back(&ability);
	if (abilities.empty())
		return;
	auto tick = [&](PreyAbilityDefinition const &ability, std::uint32_t &timer) {
		if (timer > elapsedMs) {
			timer -= elapsedMs;
			return;
		}
		if (urand(1, 100) <= ability.ChancePercent)
			prey->CastSpell(ability.SelfTarget ? static_cast<Unit *>(prey)
											  : static_cast<Unit *>(player),
							   ability.SpellId, true);
		timer = urand(ability.CooldownMinMs, ability.CooldownMaxMs);
	};
	tick(*abilities[0], runtime.AbilityOneTimer);
	if (abilities.size() > 1)
		tick(*abilities[1], runtime.AbilityTwoTimer);
}

void NativeHuntsManager::Update(std::uint32_t elapsedMs) {
	if (!IsEnabled())
		return;
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
				SaveAssignment(runtime);
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
	if (!IsEnabled() || !player || !giver) {
		message = "Native Hunt rewards are unavailable.";
		return false;
	}
	auto it = _runtimes.find(player->GetGUID().GetCounter());
	if (it == _runtimes.end()) {
		message = "You have no Hunt to turn in.";
		return false;
	}
	HuntRuntime &runtime = it->second;
	std::uint32_t const guid = runtime.CharacterGuid;
	auto uncertain = _uncertainTurnIns.find(guid);
	if (uncertain != _uncertainTurnIns.end()) {
		auto const assignment = AssignmentExists(guid);
		if (!assignment.has_value()) {
			message = "This Hunt has an unresolved database commit. No additional "
					  "Seal will be issued while the database result is unavailable.";
			return false;
		}
		if (!*assignment) {
			_uncertainTurnIns.erase(uncertain);
			HuntDomain::Execute(runtime.Aggregate, TurnInHunt{});
			RemoveRuntimeObjects(player, runtime);
			_runtimes.erase(it);
			message = "The earlier Hunt completion is now confirmed; no duplicate "
					  "Seal was issued.";
			return true;
		}
		player->DestroyItemCount(_resources.SealItemEntry, uncertain->second, true);
		auto cleanup = CharacterDatabase.BeginTransaction();
		player->SaveInventoryAndGoldToDB(cleanup);
		CommitCharacterTransactionAndWait(cleanup);
		_uncertainTurnIns.erase(uncertain);
		message = "The earlier completion rolled back. Its provisional Seal was "
				  "removed and the Hunt remains ready to turn in.";
		return false;
	}
	ItemPosCountVec destination;
	InventoryResult inventory = player->CanStoreNewItem(
		NULL_BAG, NULL_SLOT, destination, _resources.SealItemEntry,
		_config.RewardSeals);
	TurnInContext context{runtime.Aggregate.State,
		runtime.HuntmasterEntry == giver->GetEntry() &&
			(!runtime.HuntmasterSpawn || runtime.HuntmasterSpawn == giver->GetSpawnId()),
		_resources.SealItemEntry != 0, inventory == EQUIP_ERR_OK};
	auto const decision = CanTurnIn(context);
	if (decision == GameplayDecision::InventoryFull) {
		message = "Make room in your inventory before claiming the Huntmaster's Seal.";
		return false;
	}
	if (decision != GameplayDecision::Allowed) {
		message = "Only the issuing Huntmaster can complete a finished Hunt.";
		return false;
	}
	if (!CharacterDatabase.Query(
			"SELECT 1 FROM `native_hunt_assignment` WHERE `character_guid`={} "
			"AND `state`={} AND `revision`={}",
			guid, static_cast<std::uint32_t>(HuntState::ReadyToTurnIn),
			runtime.Aggregate.Revision)) {
		message = "The persisted Hunt is not ready for completion. No Seal was issued.";
		return false;
	}
	if (!player->StoreNewItem(destination, _resources.SealItemEntry, true)) {
		message = "The Seal could not be created; your Hunt remains ready to turn in.";
		return false;
	}

	// StoreNewItem changes the live inventory first. Persist that inventory,
	// completion statistics, and assignment deletion in one character-database
	// transaction so their durable state either advances together or not at all.
	auto transaction = CharacterDatabase.BeginTransaction();
	transaction->Append(
		"INSERT IGNORE INTO `native_hunt_stats` (`character_guid`,"
		"`lifetime_completed`) VALUES (" + std::to_string(guid) + ",0)");
	// Deliberately collide with the statistics primary key if the exact ready
	// assignment no longer exists. That aborts the whole transaction, including
	// inventory persistence, instead of allowing a stale concurrent completion.
	transaction->Append(
		"INSERT INTO `native_hunt_stats` (`character_guid`,"
		"`lifetime_completed`) SELECT " + std::to_string(guid) +
		",0 WHERE NOT EXISTS (SELECT 1 FROM `native_hunt_assignment` WHERE "
		"`character_guid`=" + std::to_string(guid) + " AND `state`=" +
		std::to_string(static_cast<std::uint32_t>(HuntState::ReadyToTurnIn)) +
		" AND `revision`=" + std::to_string(runtime.Aggregate.Revision) + ")");
	player->SaveInventoryAndGoldToDB(transaction);
	transaction->Append(
		"INSERT INTO `native_hunt_stats` (`character_guid`,`lifetime_completed`,"
		"`last_completed_at`) VALUES (" + std::to_string(guid) +
		",1,CURRENT_TIMESTAMP) "
		"ON DUPLICATE KEY UPDATE `lifetime_completed`=`lifetime_completed`+1,"
		"`last_completed_at`=CURRENT_TIMESTAMP");
	transaction->Append(
		"DELETE FROM `native_hunt_assignment` WHERE `character_guid`=" +
		std::to_string(guid) + " AND `state`=" +
		std::to_string(static_cast<std::uint32_t>(HuntState::ReadyToTurnIn)) +
		" AND `revision`=" + std::to_string(runtime.Aggregate.Revision));
	bool committed = CommitCharacterTransactionAndWait(transaction);
	if (!committed) {
		// A completed future returning false is normally a rollback. Confirm the
		// assignment is still present before undoing the provisional live item;
		// if the read is unavailable, block repeat claims until its durable state
		// can be inspected instead of guessing and creating a duplicate or loss.
		auto const assignment = AssignmentExists(guid);
		if (assignment.has_value() && !*assignment)
			committed = true;
		else if (assignment.has_value()) {
			player->DestroyItemCount(_resources.SealItemEntry,
				_config.RewardSeals, true);
			auto cleanup = CharacterDatabase.BeginTransaction();
			player->SaveInventoryAndGoldToDB(cleanup);
			CommitCharacterTransactionAndWait(cleanup);
			message = "Hunt completion could not be committed. The provisional Seal "
					  "was removed and your Hunt remains ready to turn in.";
			return false;
		} else {
			_uncertainTurnIns[guid] = _config.RewardSeals;
			LOG_ERROR("module.native_hunts",
				"Native Hunt turn-in for character {} has an uncertain database "
				"commit; repeat rewards are blocked until the result is readable.",
				guid);
			message = "The database commit result could not be confirmed. No additional "
					  "Seal can be claimed until the database result is readable.";
			return false;
		}
	}
	HuntDomain::Execute(runtime.Aggregate, TurnInHunt{});
	RemoveRuntimeObjects(player, runtime);
	_runtimes.erase(it);
	message = "Hunt complete. You receive the physical Huntmaster's Seal.";
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

void NativeHuntsManager::OnLogout(Player *player) {
	if (!player)
		return;
	std::uint32_t const guid = player->GetGUID().GetCounter();
	auto it = _runtimes.find(guid);
	if (it == _runtimes.end())
		return;
	auto uncertain = _uncertainTurnIns.find(guid);
	if (uncertain != _uncertainTurnIns.end()) {
		auto const assignment = AssignmentExists(guid);
		if (assignment.has_value() && !*assignment) {
			_uncertainTurnIns.erase(uncertain);
			HuntDomain::Execute(it->second.Aggregate, TurnInHunt{});
			RemoveRuntimeObjects(player, it->second);
			_runtimes.erase(it);
			return;
		}
		// Do not let a normal logout save make an unconfirmed live reward durable
		// alongside a retained assignment. If the database cannot be read, favor
		// removing the provisional item over creating a repeatable reward.
		player->DestroyItemCount(_resources.SealItemEntry, uncertain->second, true);
		if (assignment.has_value()) {
			auto cleanup = CharacterDatabase.BeginTransaction();
			player->SaveInventoryAndGoldToDB(cleanup);
			CommitCharacterTransactionAndWait(cleanup);
		} else
			LOG_ERROR("module.native_hunts",
				"Removed the provisional Native Hunt Seal for character {} on "
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
	}
}
} // namespace native_hunts
