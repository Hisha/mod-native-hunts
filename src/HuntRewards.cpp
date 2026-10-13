#include "HuntRewards.h"

#include <cmath>

namespace native_hunts {
RewardQuality StandardRewardQuality(std::uint32_t completedToday,
	std::uint32_t roll) {
	if (completedToday == 0)
		return roll <= 10 ? RewardQuality::Epic :
			(roll <= 200 ? RewardQuality::Rare : RewardQuality::Uncommon);
	if (completedToday == 1)
		return roll <= 5 ? RewardQuality::Epic :
			(roll <= 120 ? RewardQuality::Rare : RewardQuality::Uncommon);
	if (completedToday == 2)
		return roll <= 2 ? RewardQuality::Epic :
			(roll <= 60 ? RewardQuality::Rare : RewardQuality::Uncommon);
	return roll <= 20 ? RewardQuality::Rare : RewardQuality::Uncommon;
}

std::uint32_t HuntXpReward(std::uint32_t nextLevelXp, bool atLevelCap,
	float xpMultiplier, float preyMultiplier, float eliteMultiplier) {
	if (atLevelCap || xpMultiplier <= 0.0f)
		return 0;
	return static_cast<std::uint32_t>(std::round(nextLevelXp * 0.08f *
		xpMultiplier * preyMultiplier * eliteMultiplier));
}

std::uint32_t HuntMoneyReward(std::uint32_t level, float preyMultiplier,
	float eliteMultiplier) {
	return static_cast<std::uint32_t>(std::round(20.0f * level * level *
		preyMultiplier * eliteMultiplier));
}

bool EliteUnlocked(std::uint32_t completed, std::uint32_t required) {
	return completed >= required;
}
bool EliteAvailable(std::uint32_t acceptedToday, std::uint32_t limit) {
	return acceptedToday < limit;
}
std::uint32_t EliteSealReward(bool elite, std::uint32_t level,
	std::uint32_t minimumLevel, std::uint32_t perCompletion,
	bool noUpgradeAtEndgame, std::uint32_t noUpgradeBonus) {
	if (!elite || level < minimumLevel)
		return 0;
	return perCompletion + (noUpgradeAtEndgame ? noUpgradeBonus : 0);
}

RewardDeliveryPlan PlanRewardDelivery(bool hasEquipment, bool hasSeals,
		std::uint32_t equipmentResult, std::uint32_t sealResult,
		std::uint32_t combinedResult) {
	RewardDeliveryPlan plan;
	if (hasEquipment && equipmentResult == 0) {
		plan.EquipmentToInventory = true;
		plan.SealsToInventory = hasSeals && combinedResult == 0;
	} else
		plan.SealsToInventory = hasSeals && sealResult == 0;
	return plan;
}

std::int32_t SelectBelowThresholdSlot(std::uint32_t candidateEntry,
		std::uint32_t threshold, EquippedRewardSlot const *slots,
		std::size_t slotCount) {
	if (!slots || !slotCount || !threshold)
		return -1;
	for (std::size_t index = 0; index < slotCount; ++index)
		if (slots[index].ItemEntry == candidateEntry)
			return -1;

	std::int32_t selected = -1;
	for (std::size_t index = 0; index < slotCount; ++index) {
		if (!slots[index].Eligible || slots[index].ItemLevel >= threshold)
			continue;
		if (selected < 0 || slots[index].ItemLevel <
				slots[static_cast<std::size_t>(selected)].ItemLevel)
			selected = static_cast<std::int32_t>(index);
	}
	return selected;
}

InventoryFailureKind ClassifyInventoryResult(std::uint32_t result) {
	if (result == 0)
		return InventoryFailureKind::None;
	if (result == 4 || result == 50 || result == 53 || result == 56 ||
		result == 62)
		return InventoryFailureKind::BagSpace;
	if (result == 17 || result == 67 || result == 71 || result == 75 ||
		result == 76 || result == 84 || result == 85 || result == 89)
		return InventoryFailureKind::UniqueOrLimit;
	if (result == 10 || result == 11 || result == 20 || result == 23 ||
		result == 54)
		return InventoryFailureKind::ItemValidation;
	return InventoryFailureKind::Storage;
}

RewardCommitResolution ResolveRewardCommit(bool commitAcknowledged,
		AssignmentPersistence assignment) {
	if (commitAcknowledged || assignment == AssignmentPersistence::Missing)
		return RewardCommitResolution::Complete;
	if (assignment == AssignmentPersistence::Present)
		return RewardCommitResolution::RollBack;
	return RewardCommitResolution::AwaitRecovery;
}
} // namespace native_hunts
