#ifndef MOD_NATIVE_HUNTS_MANAGER_H
#define MOD_NATIVE_HUNTS_MANAGER_H

#include "HuntDomain.h"

#include "ObjectGuid.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

class Creature;
class GameObject;
class Player;

namespace native_hunts {
struct NativeHuntsConfig {
	bool Enabled = true;
	bool Debug = false;
	std::uint32_t MinimumLevel = 10;
	std::uint32_t TrackingProgressMin = 3;
	std::uint32_t TrackingProgressMax = 7;
	float GroupCreditRadius = 100.0f;
	std::uint32_t RewardSeals = 1;
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
	ObjectGuid PreyGuid;
	ObjectGuid ReturnRiftGuid;
	std::uint32_t ReturnRiftMap = 0;
	std::uint32_t ReturnRiftInstance = 0;
	std::chrono::steady_clock::time_point ReturnRiftExpires;
	std::uint32_t AbilityOneTimer = 0;
	std::uint32_t AbilityTwoTimer = 0;
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
	bool IsTrailCrystal(std::uint32_t entry) const;
	bool IsReturnRift(std::uint32_t entry) const;
	ManagedResources const &Resources() const;

	HuntRuntime const *GetRuntime(Player const *player) const;
	bool RequestHunt(Player *player, Creature *huntmaster, std::string &message);
	bool TurnIn(Player *player, Creature *huntmaster, std::string &message);
	bool Abandon(Player *player, std::string &message);
	bool UseCrystal(Player *player, GameObject *object, std::string &message);
	bool UseReturnRift(Player *player, GameObject *object, std::string &message);
	void OnCreatureKill(Player *killer, Creature *killed);
	void OnLogout(Player *player);
	std::uint32_t LifetimeCompletions(Player const *player) const;

private:
	NativeHuntsManager() = default;
	bool ResolveManagedResources();
	void LoadAssignments();
	void SaveAssignment(HuntRuntime const &runtime);
	void DeleteAssignment(HuntRuntime &runtime);
	void RemoveRuntimeObjects(Player *player, HuntRuntime &runtime);
	bool EnsureCrystal(Player *player, HuntRuntime &runtime);
	bool SpawnFinalPrey(Player *player, HuntRuntime &runtime,
						std::string &message);
	void CreateReturnRift(Player *player, Creature *prey, HuntRuntime &runtime);
	void UpdatePreyAbilities(Player *player, HuntRuntime &runtime,
							 Creature *prey, std::uint32_t elapsedMs);

	NativeHuntsConfig _config;
	ManagedResources _resources;
	std::unordered_map<std::uint32_t, HuntRuntime> _runtimes;
	std::unordered_map<std::uint32_t, std::uint32_t> _uncertainTurnIns;
	std::uint32_t _updateAccumulator = 0;
};
} // namespace native_hunts

#define sNativeHunts native_hunts::NativeHuntsManager::Instance()

#endif
