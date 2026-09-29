#ifndef MOD_NATIVE_HUNTS_GAMEPLAY_H
#define MOD_NATIVE_HUNTS_GAMEPLAY_H

#include "HuntDomain.h"

#include <cstdint>

namespace native_hunts {

enum class GameplayDecision : std::uint8_t {
	Allowed,
	WrongState,
	WrongOwner,
	WrongResource,
	WrongLocation,
	IneligibleKill,
	InventoryFull
};

struct TrackingKillContext {
	bool HunterIsKillerOrGrouped = false;
	bool SameMap = false;
	bool InAssignedZone = false;
	bool WithinCreditRadius = false;
	bool IsGrey = true;
	bool IsHuntPrey = false;
	bool AmbushPending = false;
};

struct CrystalUseContext {
	HuntState State = HuntState::Idle;
	bool CorrectPlayer = false;
	bool CorrectObject = false;
	bool CorrectTemplate = false;
	bool SameMapAndZone = false;
	bool NearPersistedSite = false;
};

struct FinalKillContext {
	HuntState State = HuntState::Idle;
	bool CorrectPlayer = false;
	bool CorrectCreature = false;
	bool CorrectTemplate = false;
};

struct ReturnRiftUseContext {
	HuntState State = HuntState::Idle;
	bool CorrectPlayer = false;
	bool CorrectObject = false;
	bool CorrectTemplate = false;
	bool SameMapAndPhase = false;
	bool WithinInteractionDistance = false;
	bool NotExpired = false;
};

struct TurnInContext {
	HuntState State = HuntState::Idle;
	bool CorrectHuntmaster = false;
	bool SealResourceAvailable = false;
	bool HasInventorySpace = false;
};

struct HuntAuraDecision {
	bool RemoveStandard = false;
	bool RemoveElite = false;
	std::uint32_t AddSpell = 0;
};

GameplayDecision CanAdvanceTracking(TrackingKillContext const &context);
std::uint8_t NextAmbushThreshold(std::uint8_t completed,
								 std::uint8_t count);
bool ShouldStartAmbush(std::uint8_t oldProgress, std::uint8_t newProgress,
					   std::uint8_t completed, std::uint8_t count,
					   bool pending);
GameplayDecision CanActivateCrystal(CrystalUseContext const &context);
GameplayDecision CanCompleteFinalKill(FinalKillContext const &context);
GameplayDecision CanUseReturnRift(ReturnRiftUseContext const &context);
GameplayDecision CanTurnIn(TurnInContext const &context);
HuntAuraDecision DecideHuntAura(HuntState state, PreyTier tier,
	std::uint32_t standardSpell, std::uint32_t eliteSpell,
	bool hasStandard, bool hasElite);

// The state changes only after the caller has successfully created the prey.
TransitionResult ApplyPreySpawnOutcome(HuntAggregate &aggregate,
									   bool spawnSucceeded);

} // namespace native_hunts

#endif
