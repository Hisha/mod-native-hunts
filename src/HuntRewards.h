#ifndef MOD_NATIVE_HUNTS_REWARDS_H
#define MOD_NATIVE_HUNTS_REWARDS_H

#include <cstdint>
#include <cstddef>

namespace native_hunts {
enum class RewardQuality : std::uint8_t { Uncommon = 2, Rare = 3, Epic = 4 };

struct RewardDeliveryPlan {
	bool EquipmentToInventory = false;
	bool SealsToInventory = false;
};

struct EquippedRewardSlot {
	std::uint32_t ItemEntry = 0;
	std::uint32_t ItemLevel = 0;
	bool Eligible = true;
};

enum class InventoryFailureKind : std::uint8_t {
	None,
	BagSpace,
	UniqueOrLimit,
	ItemValidation,
	Storage
};

enum class AssignmentPersistence : std::uint8_t {
	Missing,
	Present,
	Unknown
};

enum class RewardCommitResolution : std::uint8_t {
	Complete,
	RollBack,
	AwaitRecovery
};

RewardQuality StandardRewardQuality(std::uint32_t completedToday,
	std::uint32_t rollOutOf1000);
std::uint32_t HuntXpReward(std::uint32_t nextLevelXp, bool atLevelCap,
	float xpMultiplier, float preyMultiplier, float eliteMultiplier = 1.0f);
std::uint32_t HuntMoneyReward(std::uint32_t level, float preyMultiplier,
	float eliteMultiplier = 1.0f);
bool EliteUnlocked(std::uint32_t standardCompletions,
	std::uint32_t requiredCompletions);
bool EliteAvailable(std::uint32_t acceptedToday, std::uint32_t dailyLimit);
std::uint32_t EliteSealReward(bool elite, std::uint32_t level,
	std::uint32_t minimumLevel, std::uint32_t perCompletion,
	bool noUpgradeAtEndgame, std::uint32_t noUpgradeBonus);
RewardDeliveryPlan PlanRewardDelivery(bool hasEquipment, bool hasSeals,
	std::uint32_t equipmentResult, std::uint32_t sealResult,
	std::uint32_t combinedResult);
std::int32_t SelectBelowThresholdSlot(std::uint32_t candidateEntry,
	std::uint32_t threshold, EquippedRewardSlot const *slots,
	std::size_t slotCount);
InventoryFailureKind ClassifyInventoryResult(std::uint32_t result);
RewardCommitResolution ResolveRewardCommit(bool commitAcknowledged,
	AssignmentPersistence assignment);
} // namespace native_hunts

#endif
