#include "HuntGameplay.h"

namespace native_hunts {
GameplayDecision CanAdvanceTracking(TrackingKillContext const &context) {
	if (!context.HunterIsKillerOrGrouped || !context.SameMap ||
		!context.InAssignedZone || !context.WithinCreditRadius ||
		context.IsGrey || context.IsHuntPrey || context.AmbushPending)
		return GameplayDecision::IneligibleKill;
	return GameplayDecision::Allowed;
}

std::uint8_t NextAmbushThreshold(std::uint8_t completed,
								 std::uint8_t count) {
	if (count == 0 || completed >= count)
		return 100;
	return static_cast<std::uint8_t>(
		(100u * (static_cast<std::uint32_t>(completed) + 1u)) /
		(static_cast<std::uint32_t>(count) + 1u));
}

bool ShouldStartAmbush(std::uint8_t oldProgress, std::uint8_t newProgress,
					   std::uint8_t completed, std::uint8_t count,
					   bool pending) {
	if (pending || newProgress >= 100 || completed >= count)
		return false;
	std::uint8_t const threshold = NextAmbushThreshold(completed, count);
	return oldProgress < threshold && newProgress >= threshold;
}

GameplayDecision CanActivateCrystal(CrystalUseContext const &context) {
	if (context.State != HuntState::FinalRevealed)
		return GameplayDecision::WrongState;
	if (!context.CorrectPlayer)
		return GameplayDecision::WrongOwner;
	if (!context.CorrectObject || !context.CorrectTemplate)
		return GameplayDecision::WrongResource;
	if (!context.SameMapAndZone || !context.NearPersistedSite)
		return GameplayDecision::WrongLocation;
	return GameplayDecision::Allowed;
}

GameplayDecision CanCompleteFinalKill(FinalKillContext const &context) {
	if (context.State != HuntState::PreyActive)
		return GameplayDecision::WrongState;
	if (!context.CorrectPlayer)
		return GameplayDecision::WrongOwner;
	if (!context.CorrectCreature || !context.CorrectTemplate)
		return GameplayDecision::WrongResource;
	return GameplayDecision::Allowed;
}

GameplayDecision CanUseReturnRift(ReturnRiftUseContext const &context) {
	if (context.State != HuntState::ReadyToTurnIn)
		return GameplayDecision::WrongState;
	if (!context.CorrectPlayer)
		return GameplayDecision::WrongOwner;
	if (!context.CorrectObject || !context.CorrectTemplate)
		return GameplayDecision::WrongResource;
	if (!context.SameMapAndPhase || !context.WithinInteractionDistance ||
		!context.NotExpired)
		return GameplayDecision::WrongLocation;
	return GameplayDecision::Allowed;
}

GameplayDecision CanTurnIn(TurnInContext const &context) {
	if (context.State != HuntState::ReadyToTurnIn)
		return GameplayDecision::WrongState;
	if (!context.CorrectHuntmaster)
		return GameplayDecision::WrongOwner;
	if (!context.SealResourceAvailable)
		return GameplayDecision::WrongResource;
	if (!context.HasInventorySpace)
		return GameplayDecision::InventoryFull;
	return GameplayDecision::Allowed;
}

TransitionResult ApplyPreySpawnOutcome(HuntAggregate &aggregate,
									   bool spawnSucceeded) {
	if (!spawnSucceeded)
		return {TransitionStatus::NoChange, TransitionError::None};
	return HuntDomain::Execute(aggregate, ActivatePrey{});
}
} // namespace native_hunts
