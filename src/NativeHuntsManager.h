#ifndef MOD_NATIVE_HUNTS_MANAGER_H
#define MOD_NATIVE_HUNTS_MANAGER_H

#include "HuntDomain.h"
#include "HuntCatalog.h"

#include "ObjectGuid.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Creature;
class GameObject;
class Player;

namespace native_hunts {
struct NativeHuntsConfig {
	bool Enabled = true;
	bool Debug = false;
	std::uint32_t MinimumLevel = 10;
	float XpMultiplier = 0.75f;
	std::uint32_t TrackingProgressMin = 3;
	std::uint32_t TrackingProgressMax = 7;
	float GroupCreditRadius = 100.0f;
	HuntSearchScope SearchScope = HuntSearchScope::Local;
	std::uint8_t AmbushCount = 2;
	float AmbushHealthMultiplier = 4.0f;
	float AmbushEscapeHealthPercent = 50.0f;
	std::uint32_t EliteRequiredStandardCompletions = 10;
	std::uint32_t EliteDailyLimit = 1;
	float EliteHealthMultiplier = 1.0f;
	float EliteDamageMultiplier = 1.0f;
	float EliteArmorMultiplier = 1.0f;
	float EliteXpMultiplier = 1.0f;
	float EliteGoldMultiplier = 1.0f;
	std::uint32_t EliteSealMinimumLevel = 80;
	std::uint32_t EliteSealsPerCompletion = 1;
	std::uint32_t EliteEndgameRewardLevel = 80;
	std::uint32_t EliteEndgameRewardMinItemLevel = 200;
	std::uint32_t EliteEndgameRewardMaxItemLevel = 200;
	bool EliteRewardRequireUpgrade = true;
	float EliteRewardUpgradePoolPct = 0.70f;
	std::uint32_t EliteNoUpgradeBonusSeals = 1;
	bool ReturnRiftEnabled = true;
	std::uint32_t ReturnRiftDurationSeconds = 120;
	float ReturnRiftArrivalDistance = 3.0f;
};

struct ManagedResources {
	bool Ready = false;
	std::string Reason;
	std::unordered_map<std::string, std::uint32_t> CreatureEntries;
	std::unordered_map<std::string, std::uint32_t> CreatureSpawns;
	std::uint32_t TrailCrystalEntry = 0;
	std::uint32_t ReturnRiftEntry = 0;
	std::uint32_t SealItemEntry = 0;
};

struct HuntRuntime {
	std::uint32_t CharacterGuid = 0;
	HuntAggregate Aggregate;
	std::uint32_t HuntmasterEntry = 0;
	std::uint32_t HuntmasterSpawn = 0;
	std::uint32_t ZoneId = 0;
	std::uint32_t MapId = 0;
	ObjectGuid CrystalGuid;
	ObjectGuid AmbushGuid;
	ObjectGuid PreyGuid;
	ObjectGuid CompanionGuid;
	ObjectGuid ReturnRiftGuid;
	std::uint32_t ReturnRiftMap = 0;
	std::uint32_t ReturnRiftInstance = 0;
	std::chrono::steady_clock::time_point ReturnRiftExpires;
	std::uint32_t AbilityOneTimer = 0;
	std::uint32_t AbilityTwoTimer = 0;
	std::vector<std::uint32_t> AbilityTimers;
	std::vector<bool> AbilityUsed;
	std::uint8_t AmbushesCompleted = 0;
	bool AmbushPending = false;
	bool FinalRevealNotified = false;
	bool DruidBearPhase = false;
	std::uint8_t FearDrStage = 0;
	std::uint32_t FearDrResetMs = 0;
	std::uint32_t RogueReopenMs = 0;
};

struct ProvisionalTurnIn {
	std::uint32_t SealCount = 0;
	std::uint32_t EquipmentEntry = 0;
	std::uint32_t Money = 0;
	ObjectGuid EquipmentGuid;
};

class NativeHuntsManager {
public:
	static NativeHuntsManager &Instance();

	void Configure(NativeHuntsConfig config);
	void Initialize();
	void Update(std::uint32_t elapsedMs);
	void Shutdown();

	bool IsEnabled() const;
	bool IsHuntmaster(std::uint32_t entry) const;
	bool IsElitePrey(std::uint32_t entry) const;
	bool IsTrailCrystal(std::uint32_t entry) const;
	bool IsReturnRift(std::uint32_t entry) const;
	ManagedResources const &Resources() const;

	HuntRuntime const *GetRuntime(Player const *player) const;
	bool RequestHunt(Player *player, Creature *huntmaster, std::string &message);
	bool RequestEliteHunt(Player *player, Creature *huntmaster,
		std::string &message);
	bool TurnIn(Player *player, Creature *huntmaster, std::string &message);
	bool Abandon(Player *player, std::string &message);
	bool UseCrystal(Player *player, GameObject *object, std::string &message);
	bool UseReturnRift(Player *player, GameObject *object, std::string &message);
	void OnCreatureKill(Player *killer, Creature *killed);
	void OnLogout(Player *player);
	std::uint32_t LifetimeCompletions(Player const *player) const;
	bool IsEliteUnlocked(Player const *player) const;
	bool IsEliteAvailableToday(Player const *player) const;
	bool IsGuardLocator(std::uint32_t entry) const;
	bool SendHuntmasterLocation(Player *player, std::uint32_t guardEntry,
		std::string &message) const;
	std::string BuildStatus(Player const *player,
						bool includeCoordinates) const;

private:
	NativeHuntsManager() = default;
	bool ResolveManagedResources();
	void ResolveGuardLocators();
	void LoadAssignments();
	void SaveAssignment(HuntRuntime const &runtime);
	void DeleteAssignment(HuntRuntime &runtime);
	void RemoveRuntimeObjects(Player *player, HuntRuntime &runtime);
	bool EnsureCrystal(Player *player, HuntRuntime &runtime);
	bool SpawnFinalPrey(Player *player, HuntRuntime &runtime,
						std::string &message);
	bool SpawnAmbush(Player *player, HuntRuntime &runtime,
					 std::string &message);
	void SendFinalLocationFeedback(Player *player, HuntRuntime &runtime,
								  bool includeMessage);
	void InitializePreyCombat(Player *player, HuntRuntime &runtime,
							 Creature *prey, bool finalEncounter);
	void CreateReturnRift(Player *player, Creature *prey, HuntRuntime &runtime);
	void UpdatePreyAbilities(Player *player, HuntRuntime &runtime,
							 Creature *prey, std::uint32_t elapsedMs);

	NativeHuntsConfig _config;
	ManagedResources _resources;
	std::unordered_map<std::uint32_t, HuntRuntime> _runtimes;
	std::unordered_map<std::uint32_t, std::string> _guardLocators;
	std::unordered_map<std::uint32_t, ProvisionalTurnIn> _uncertainTurnIns;
	std::uint32_t _updateAccumulator = 0;
	std::uint32_t _poiAccumulator = 0;
};
} // namespace native_hunts

#define sNativeHunts native_hunts::NativeHuntsManager::Instance()

#endif
