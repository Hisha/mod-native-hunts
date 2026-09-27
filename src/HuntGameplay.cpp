#include "HuntGameplay.h"

namespace native_hunts {
GameplayDecision CanAdvanceTracking(TrackingKillContext const &context) {
	if (!context.HunterIsKillerOrGrouped || !context.SameMap ||
		!context.InAssignedZone || !context.WithinCreditRadius ||
		context.IsGrey || context.IsHuntPrey)
		return GameplayDecision::IneligibleKill;
	return GameplayDecision::Allowed;
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
