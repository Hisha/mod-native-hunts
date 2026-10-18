#include "HuntGameplay.h"

namespace native_hunts {
bool MatchesManagedHuntmaster(std::uint32_t creatureEntry,
		std::uint32_t creatureSpawn, std::uint32_t managedEntry,
		std::uint32_t managedSpawn) {
	return (managedEntry && creatureEntry == managedEntry) ||
		(managedSpawn && creatureSpawn == managedSpawn);
}

bool MatchesIssuingHuntmaster(std::uint32_t creatureEntry,
		std::uint32_t creatureSpawn, std::uint32_t issuingEntry,
		std::uint32_t issuingSpawn) {
	return issuingEntry && creatureEntry == issuingEntry &&
		(!issuingSpawn || creatureSpawn == issuingSpawn);
}

bool CanUseIssuingHuntmaster(HuntState state, std::uint32_t creatureEntry,
		std::uint32_t creatureSpawn, std::uint32_t issuingEntry,
		std::uint32_t issuingSpawn) {
	return state != HuntState::Idle && MatchesIssuingHuntmaster(creatureEntry,
		creatureSpawn, issuingEntry, issuingSpawn);
}

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

TrackingMilestoneAction NextTrackingMilestone(std::uint8_t progress,
		std::uint8_t ambushesCompleted, std::uint8_t ambushCount,
		bool ambushPending) {
	if (ambushPending)
		return TrackingMilestoneAction::None;
	if (ambushesCompleted < ambushCount &&
		progress >= NextAmbushThreshold(ambushesCompleted, ambushCount))
		return TrackingMilestoneAction::StartAmbush;
	if (progress == 100 && ambushesCompleted >= ambushCount)
		return TrackingMilestoneAction::RevealFinal;
	return TrackingMilestoneAction::None;
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

HuntAuraDecision DecideHuntAura(HuntState state, PreyTier tier,
		std::uint32_t standardSpell, std::uint32_t eliteSpell,
		bool hasStandard, bool hasElite) {
	bool const active = state != HuntState::Idle;
	bool const wantsStandard = active && tier == PreyTier::Standard;
	bool const wantsElite = active && tier == PreyTier::Elite;
	return {
		standardSpell && hasStandard && !wantsStandard,
		eliteSpell && hasElite && !wantsElite,
		wantsStandard && standardSpell && !hasStandard ? standardSpell
			: wantsElite && eliteSpell && !hasElite ? eliteSpell : 0
	};
}

TransitionResult ApplyPreySpawnOutcome(HuntAggregate &aggregate,
									   bool spawnSucceeded) {
	if (!spawnSucceeded)
		return {TransitionStatus::NoChange, TransitionError::None};
	return HuntDomain::Execute(aggregate, ActivatePrey{});
}
} // namespace native_hunts
