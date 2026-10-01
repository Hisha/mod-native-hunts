#ifndef MOD_NATIVE_HUNTS_SNAPSHOT_H
#define MOD_NATIVE_HUNTS_SNAPSHOT_H

#include "HuntDomain.h"

#include <cstdint>
#include <string>

namespace native_hunts {
enum class NativeContentState : std::uint8_t { Available, Unavailable };

struct NativeSealStatus {
	NativeContentState State = NativeContentState::Unavailable;
	std::uint32_t PhysicalBalance = 0;
	std::string UnavailableReason;

	static NativeSealStatus Available(std::uint32_t physicalBalance);
	static NativeSealStatus Unavailable(std::string reason);
};

struct HuntProgressionSnapshot {
	std::uint32_t StandardCompleted = 0;
	std::uint32_t EliteCompleted = 0;
	bool EliteUnlocked = false;
	std::uint32_t EliteAcceptedToday = 0;
	std::uint32_t EliteDailyLimit = 0;
	bool EliteAvailableToday = false;
};

struct HuntSnapshot {
	bool ContentAvailable = true;
	bool Active = false;
	HuntState State = HuntState::Idle;
	std::uint64_t Revision = 0;
	std::string HuntmasterName;
	std::string CityName;
	std::string PreyName;
	PreyTier Tier = PreyTier::None;
	std::string ZoneName;
	std::uint8_t Progress = 0;
	bool FinalLocationVisible = false;
	bool ReadyToTurnIn = false;
	std::string FinalLocationName;
	NativeContentState SealState = NativeContentState::Unavailable;
	std::uint32_t PhysicalSealBalance = 0;
	std::string NativeContentReason;
	HuntProgressionSnapshot Progression;
	std::string StatusReason;
};

HuntSnapshot BuildHuntSnapshot(HuntAggregate const &aggregate,
							   NativeSealStatus const &seals,
							   HuntProgressionSnapshot progression,
							   std::string idleReason = {});
} // namespace native_hunts

#endif
