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

struct HuntSnapshot {
	HuntState State = HuntState::Idle;
	std::uint64_t Revision = 0;
	std::string HuntmasterName;
	std::string CityName;
	std::string PreyName;
	PreyTier Tier = PreyTier::None;
	std::string ZoneName;
	std::uint8_t Progress = 0;
	bool FinalLocationVisible = false;
	std::string FinalLocationName;
	NativeContentState SealState = NativeContentState::Unavailable;
	std::uint32_t PhysicalSealBalance = 0;
	std::string NativeContentReason;
	std::uint32_t LifetimeCompletions = 0;
	std::string StatusReason;
};

HuntSnapshot BuildHuntSnapshot(HuntAggregate const &aggregate,
							   NativeSealStatus const &seals,
							   std::uint32_t lifetimeCompletions,
							   std::string idleReason = {});
} // namespace native_hunts

#endif
