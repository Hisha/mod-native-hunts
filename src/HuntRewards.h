#ifndef MOD_NATIVE_HUNTS_REWARDS_H
#define MOD_NATIVE_HUNTS_REWARDS_H

#include <cstdint>

namespace native_hunts {
enum class RewardQuality : std::uint8_t { Uncommon = 2, Rare = 3, Epic = 4 };

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
} // namespace native_hunts

#endif
