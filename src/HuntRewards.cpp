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
} // namespace native_hunts
