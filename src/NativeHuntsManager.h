#ifndef MOD_NATIVE_HUNTS_MANAGER_H
#define MOD_NATIVE_HUNTS_MANAGER_H

#include "HuntDomain.h"
#include "HuntCatalog.h"
#include "HuntSnapshot.h"
#include "RelicPoolPolicy.h"

#include "ObjectGuid.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Creature;
class GameObject;
class Map;
class Player;

namespace native_hunts {
enum class SealStoreSlot : std::uint8_t {
	Weapon = 0,
	Head,
	Neck,
	Shoulder,
	Back,
	Chest,
	Wrist,
	Hands,
	Waist,
	Legs,
	Feet,
	Ring,
	Trinket,
	OffHand,
	Relic
};

struct SealStoreItem {
	std::uint32_t ItemId = 0;
	std::string Name;
	std::uint32_t ItemLevel = 0;
	float Score = 0.0f;
};

struct SealStoreCatalogDiagnostics {
	std::size_t TotalCandidates = 0;
	std::size_t RejectedByRolePolicy = 0;
	std::size_t AcceptedByRolePolicy = 0;
	std::size_t RemainingAfterEligibility = 0;
};

struct NativeHuntsConfig {
	bool Enabled = true;
	bool Debug = false;
	std::uint32_t MinimumLevel = 10;
	float XpMultiplier = 0.75f;
	std::uint32_t TrackingProgressMin = 3;
	std::uint32_t TrackingProgressMax = 7;
	float GroupCreditRadius = 100.0f;
	HuntSearchScope SearchScope = HuntSearchScope::Local;
	std::uint32_t RelicsCountPerZone = DefaultRelicsPerZone;
	std::uint32_t RelicGameObjectEntry = 990000;
	std::uint32_t RelicProgressTarget = 10;
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
	std::array<std::uint32_t, 4> SealStoreTierCost{{5, 10, 20, 30}};
	std::array<std::uint32_t, 4> SealStoreTierMinItemLevel{{213, 226, 245, 264}};
	std::array<std::uint32_t, 4> SealStoreTierMaxItemLevel{{219, 232, 251, 264}};
	bool ReturnRiftEnabled = true;
	std::uint32_t ReturnRiftDurationSeconds = 120;
	float ReturnRiftArrivalDistance = 3.0f;
};

struct ManagedResources {
	bool Ready = false;
	std::string Reason;
	bool SealStoreReady = false;
	std::string SealStoreReason;
	std::unordered_map<std::string, std::uint32_t> CreatureEntries;
	std::unordered_map<std::string, std::uint32_t> CreatureSpawns;
	std::uint32_t TrailCrystalEntry = 0;
	std::uint32_t ReturnRiftEntry = 0;
	std::uint32_t SealItemEntry = 0;
	std::array<std::uint32_t, 4> SealStoreExtendedCosts{};
	std::uint32_t StandardHuntAuraSpell = 0;
	std::uint32_t EliteHuntAuraSpell = 0;
};

struct HuntRuntime {
	std::uint32_t CharacterGuid = 0;
	std::string HuntInstanceId;
	HuntAggregate Aggregate;
	std::uint32_t HuntmasterEntry = 0;
	std::uint32_t HuntmasterSpawn = 0;
	std::uint32_t ZoneId = 0;
	std::uint32_t MapId = 0;
	std::string RelicPoolId;
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
	std::uint32_t MailId = 0;
	std::vector<ObjectGuid> MailItemGuids;
};

struct RelicPosition {
	std::uint32_t Index = 0;
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
	float Orientation = 0.0f;
	ObjectGuid Guid;
};

struct RelicPool {
	std::string Id;
	RelicPoolScope Scope;
	std::uint32_t RequiredCount = 0;
	std::vector<RelicPosition> Positions;
	bool SpawnShortfallReported = false;
};

class NativeHuntsManager {
public:
	static NativeHuntsManager &Instance();

	void Configure(NativeHuntsConfig config);
	void Initialize();
	void Update(std::uint32_t elapsedMs);
	void Shutdown();

	bool IsEnabled() const;
	bool IsHuntmaster(Creature const *creature) const;
	bool CanOpenHuntmasterGossip(Player const *player,
		Creature const *creature) const;
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
	bool CollectRelic(Player *player, GameObject *object,
		std::string &message);
	bool UseCrystal(Player *player, GameObject *object, std::string &message);
	bool UseReturnRift(Player *player, GameObject *object, std::string &message);
	void OnCreatureKill(Player *killer, Creature *killed);
	void OnLogin(Player *player);
	void OnLogout(Player *player);
	void HandleUiAddonMessage(Player *player, std::string const &payload);
	std::uint32_t LifetimeCompletions(Player const *player) const;
	bool IsEliteUnlocked(Player const *player) const;
	bool IsEliteAvailableToday(Player const *player) const;
	bool IsSealStoreAvailable(Player const *player) const;
	bool AreSealStoreExtendedCostsReady() const;
	std::uint32_t GetSealBalance(Player const *player) const;
	std::uint32_t GetSealStoreTierCost(std::uint8_t tier) const;
	std::uint32_t GetSealStoreTierMinItemLevel(std::uint8_t tier) const;
	std::uint32_t GetSealStoreTierMaxItemLevel(std::uint8_t tier) const;
	std::uint32_t GetSealStoreExtendedCost(std::uint8_t tier) const;
	std::string const &GetSealStoreUnavailableReason() const;
	std::vector<SealStoreItem> BuildSealStoreItems(Player *player,
		std::uint32_t spec, std::uint8_t tier, SealStoreSlot slot,
		SealStoreCatalogDiagnostics *diagnostics = nullptr) const;
	bool PurchaseSealStoreItem(Player *player, std::uint32_t spec,
		std::uint8_t tier, std::uint32_t itemId, std::string &message);
	bool IsGuardLocator(std::uint32_t entry) const;
	bool SendHuntmasterLocation(Player *player, std::uint32_t guardEntry,
		std::string &message) const;
	std::string BuildStatus(Player const *player,
						bool includeCoordinates) const;

private:
	NativeHuntsManager() = default;
	HuntmasterDefinition const *ResolveHuntmaster(
		Creature const *creature) const;
	bool ResolveManagedResources();
	void ResolveGuardLocators();
	void LoadAssignments();
	bool CreateAssignment(HuntRuntime const &runtime);
	void SaveAssignment(HuntRuntime const &runtime);
	void DeleteAssignment(HuntRuntime &runtime);
	void InitializeRelicPools();
	void LoadRelicPools();
	void ReconcileRelicPools();
	void RemoveRelicPoolObjects(RelicPool &pool);
	void RemoveAllRelicPoolObjects();
	void RetireRelicPool(std::string const &poolId);
	bool AssociateRelicPool(Player *player, HuntRuntime &runtime);
	bool CreateRelicPool(RelicPoolScope const &scope, HuntRuntime &runtime);
	bool GenerateRelicPositions(RelicPool &pool, Map *map);
	void SpawnRelicPool(RelicPool &pool);
	RelicPool *FindRelicPool(GameObject const *object,
		std::uint32_t *relicIndex = nullptr);
	void ReconcileHuntAura(Player *player, HuntAggregate const *assignment);
	void RemoveRuntimeObjects(Player *player, HuntRuntime &runtime);
	bool EnsureCrystal(Player *player, HuntRuntime &runtime);
	bool SpawnFinalPrey(Player *player, HuntRuntime &runtime,
						std::string &message);
	bool SpawnAmbush(Player *player, HuntRuntime &runtime,
					 std::string &message);
	bool AwardTrackingProgress(Player *player, HuntRuntime &runtime,
		std::uint8_t amount);
	bool ReconcileTrackingMilestones(Player *player, HuntRuntime &runtime);
	void SendFinalLocationFeedback(Player *player, HuntRuntime &runtime,
								  bool includeMessage);
	void InitializePreyCombat(Player *player, HuntRuntime &runtime,
							 Creature *prey, bool finalEncounter);
	void CreateReturnRift(Player *player, Creature *prey, HuntRuntime &runtime);
	void UpdatePreyAbilities(Player *player, HuntRuntime &runtime,
							 Creature *prey, std::uint32_t elapsedMs);
	HuntProgressionSnapshot ReadProgression(Player const *player) const;
	HuntSnapshot BuildUiSnapshot(Player const *player) const;
	void QueueUiState(Player *player);
	void PublishUiState(Player *player, std::uint32_t nonce = 0,
		bool force = false);
	bool IsSealStoreItemEligible(Player *player, std::uint32_t spec,
		std::uint8_t tier, SealStoreSlot slot, std::uint32_t itemId,
		SealStoreCatalogDiagnostics *diagnostics = nullptr) const;

	NativeHuntsConfig _config;
	ManagedResources _resources;
	bool _relicPoolsReady = false;
	bool _relicDisableCleanupComplete = false;
	bool _initialized = false;
	std::unordered_map<std::string, RelicPool> _relicPools;
	std::unordered_map<RelicPoolScope, std::chrono::steady_clock::time_point,
		RelicPoolScopeHash> _relicPoolRetryAfter;
	std::unordered_map<std::uint32_t, HuntRuntime> _runtimes;
	std::unordered_map<std::uint32_t, std::string> _guardLocators;
	std::unordered_map<std::uint32_t, ProvisionalTurnIn> _uncertainTurnIns;
	std::unordered_map<std::uint32_t, std::uint64_t> _uiLastRequestMs;
	std::unordered_map<std::uint32_t, std::uint32_t> _uiSequences;
	std::unordered_map<std::uint32_t, std::string> _uiLastProjection;
	std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> _uiPending;
	std::uint32_t _updateAccumulator = 0;
	std::uint32_t _poiAccumulator = 0;
};
} // namespace native_hunts

#define sNativeHunts native_hunts::NativeHuntsManager::Instance()

#endif
