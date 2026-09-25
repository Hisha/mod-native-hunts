#include "HuntDomain.h"

#include <algorithm>
#include <utility>

namespace native_hunts {
namespace {
TransitionResult Applied() {
	return {TransitionStatus::Applied, TransitionError::None};
}
TransitionResult NoChange() {
	return {TransitionStatus::NoChange, TransitionError::None};
}
TransitionResult Rejected(TransitionError error) {
	return {TransitionStatus::Rejected, error};
}

bool HasRequiredIdentity(HuntIdentity const &identity) {
	return !identity.HuntmasterKey.empty() &&
		   !identity.HuntmasterName.empty() && !identity.CityName.empty() &&
		   !identity.PreyKey.empty() && !identity.PreyName.empty() &&
		   identity.Tier != PreyTier::None && !identity.ZoneKey.empty() &&
		   !identity.ZoneName.empty();
}

void AdvanceRevision(HuntAggregate &aggregate) { ++aggregate.Revision; }

void ClearAssignment(HuntAggregate &aggregate) {
	aggregate.State = HuntState::Idle;
	aggregate.Identity = {};
	aggregate.Progress = 0;
	aggregate.FinalLocationKey.clear();
	aggregate.FinalLocationName.clear();
}

TransitionResult Apply(HuntAggregate &aggregate, AcceptHunt const &command) {
	if (!HasRequiredIdentity(command.Identity))
		return Rejected(TransitionError::InvalidIdentity);
	if (aggregate.State != HuntState::Idle) {
		if (aggregate.State == HuntState::Tracking && aggregate.Progress == 0 &&
			aggregate.Identity == command.Identity)
			return NoChange();
		return Rejected(TransitionError::InvalidState);
	}
	aggregate.Identity = command.Identity;
	aggregate.Progress = 0;
	aggregate.FinalLocationKey.clear();
	aggregate.FinalLocationName.clear();
	aggregate.State = HuntState::Tracking;
	AdvanceRevision(aggregate);
	return Applied();
}

TransitionResult Apply(HuntAggregate &aggregate,
					   AdvanceTracking const &command) {
	if (aggregate.State != HuntState::Tracking)
		return Rejected(TransitionError::InvalidState);
	if (command.Amount == 0)
		return Rejected(TransitionError::InvalidCommand);
	if (aggregate.Progress == 100)
		return NoChange();
	aggregate.Progress = static_cast<std::uint8_t>(std::min<std::uint16_t>(
		100, static_cast<std::uint16_t>(aggregate.Progress) + command.Amount));
	AdvanceRevision(aggregate);
	return Applied();
}

TransitionResult Apply(HuntAggregate &aggregate, RevealFinal const &command) {
	if (aggregate.State == HuntState::FinalRevealed &&
		aggregate.FinalLocationKey == command.LocationKey &&
		aggregate.FinalLocationName == command.LocationName)
		return NoChange();
	if (aggregate.State != HuntState::Tracking)
		return Rejected(TransitionError::InvalidState);
	if (aggregate.Progress != 100)
		return Rejected(TransitionError::TrackingIncomplete);
	if (command.LocationKey.empty() || command.LocationName.empty())
		return Rejected(TransitionError::InvalidIdentity);
	aggregate.FinalLocationKey = command.LocationKey;
	aggregate.FinalLocationName = command.LocationName;
	aggregate.State = HuntState::FinalRevealed;
	AdvanceRevision(aggregate);
	return Applied();
}

TransitionResult Apply(HuntAggregate &aggregate, ActivatePrey const &) {
	if (aggregate.State == HuntState::PreyActive)
		return NoChange();
	if (aggregate.State != HuntState::FinalRevealed)
		return Rejected(TransitionError::InvalidState);
	aggregate.State = HuntState::PreyActive;
	AdvanceRevision(aggregate);
	return Applied();
}

TransitionResult Apply(HuntAggregate &aggregate, MarkPreyKilled const &) {
	if (aggregate.State == HuntState::ReadyToTurnIn)
		return NoChange();
	if (aggregate.State != HuntState::PreyActive)
		return Rejected(TransitionError::InvalidState);
	aggregate.State = HuntState::ReadyToTurnIn;
	AdvanceRevision(aggregate);
	return Applied();
}

TransitionResult Apply(HuntAggregate &aggregate, TurnInHunt const &) {
	if (aggregate.State != HuntState::ReadyToTurnIn)
		return Rejected(TransitionError::InvalidState);
	ClearAssignment(aggregate);
	AdvanceRevision(aggregate);
	return Applied();
}

TransitionResult Apply(HuntAggregate &aggregate, AbandonHunt const &) {
	if (aggregate.State == HuntState::Idle)
		return NoChange();
	ClearAssignment(aggregate);
	AdvanceRevision(aggregate);
	return Applied();
}

TransitionResult Apply(HuntAggregate &aggregate, RecoverAfterRestart const &) {
	if (aggregate.State != HuntState::PreyActive)
		return NoChange();
	aggregate.State = HuntState::FinalRevealed;
	AdvanceRevision(aggregate);
	return Applied();
}
} // namespace

bool HuntIdentity::operator==(HuntIdentity const &other) const {
	return HuntmasterKey == other.HuntmasterKey &&
		   HuntmasterName == other.HuntmasterName &&
		   CityName == other.CityName && PreyKey == other.PreyKey &&
		   PreyName == other.PreyName && Tier == other.Tier &&
		   ZoneKey == other.ZoneKey && ZoneName == other.ZoneName;
}

TransitionResult HuntDomain::Execute(HuntAggregate &aggregate,
									 HuntCommand const &command) {
	if (!IsValid(aggregate))
		return Rejected(TransitionError::InvalidState);
	HuntAggregate next = aggregate;
	TransitionResult const result = std::visit(
		[&next](auto const &concrete) { return Apply(next, concrete); },
		command);
	if (result.Status == TransitionStatus::Applied) {
		if (!IsValid(next))
			return Rejected(TransitionError::InvalidState);
		aggregate = std::move(next);
	}
	return result;
}

bool HuntDomain::IsValid(HuntAggregate const &aggregate) {
	if (aggregate.Progress > 100)
		return false;
	if (aggregate.State == HuntState::Idle)
		return aggregate.Identity == HuntIdentity{} &&
			   aggregate.Progress == 0 && aggregate.FinalLocationKey.empty() &&
			   aggregate.FinalLocationName.empty();
	if (!HasRequiredIdentity(aggregate.Identity))
		return false;
	if (aggregate.State == HuntState::Tracking)
		return aggregate.FinalLocationKey.empty() &&
			   aggregate.FinalLocationName.empty();
	return aggregate.Progress == 100 && !aggregate.FinalLocationKey.empty() &&
		   !aggregate.FinalLocationName.empty();
}

bool HuntDomain::IsActive(HuntAggregate const &aggregate) {
	return aggregate.State != HuntState::Idle;
}
} // namespace native_hunts
