#include "HuntSnapshot.h"

#include <utility>

namespace native_hunts {
NativeSealStatus NativeSealStatus::Available(std::uint32_t physicalBalance) {
	return {NativeContentState::Available, physicalBalance, {}};
}

NativeSealStatus NativeSealStatus::Unavailable(std::string reason) {
	if (reason.empty())
		reason = "Native Hunt content is unavailable.";
	return {NativeContentState::Unavailable, 0, std::move(reason)};
}

HuntSnapshot BuildHuntSnapshot(HuntAggregate const &aggregate,
							   NativeSealStatus const &seals,
							   std::uint32_t lifetimeCompletions,
							   std::string idleReason) {
	HuntSnapshot snapshot;
	snapshot.State = aggregate.State;
	snapshot.Revision = aggregate.Revision;
	snapshot.SealState = seals.State;
	snapshot.PhysicalSealBalance = seals.State == NativeContentState::Available
									   ? seals.PhysicalBalance
									   : 0;
	snapshot.NativeContentReason =
		seals.State == NativeContentState::Unavailable ? seals.UnavailableReason
													   : std::string{};
	snapshot.LifetimeCompletions = lifetimeCompletions;
	if (aggregate.State == HuntState::Idle) {
		snapshot.StatusReason =
			idleReason.empty() ? "No active Hunt." : std::move(idleReason);
		return snapshot;
	}
	snapshot.HuntmasterName = aggregate.Identity.HuntmasterName;
	snapshot.CityName = aggregate.Identity.CityName;
	snapshot.PreyName = aggregate.Identity.PreyName;
	snapshot.Tier = aggregate.Identity.Tier;
	snapshot.ZoneName = aggregate.Identity.ZoneName;
	snapshot.Progress = aggregate.Progress;
	if (aggregate.State == HuntState::FinalRevealed ||
		aggregate.State == HuntState::PreyActive ||
		aggregate.State == HuntState::ReadyToTurnIn) {
		snapshot.FinalLocationVisible = true;
		snapshot.FinalLocationName = aggregate.FinalLocationName;
	}
	return snapshot;
}
} // namespace native_hunts
