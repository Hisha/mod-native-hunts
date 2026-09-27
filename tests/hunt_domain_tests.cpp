#include "HuntDomain.h"
#include "HuntCatalog.h"
#include "HuntGameplay.h"
#include "HuntSnapshot.h"
#include "HuntRewards.h"

#include <cstdlib>
#include <set>
#include <string>

using namespace native_hunts;

namespace {
int Failures = 0;

void Check(bool condition, std::string const &message) {
	static_cast<void>(message);
	if (condition)
		return;
	++Failures;
}

HuntIdentity ProofIdentity() {
	return {"corvin",	"Huntmaster Corvin", "Stormwind City", "gorehide",
			"Gorehide", PreyTier::Standard,	 "westfall",	   "Westfall"};
}

void ExpectStatus(TransitionResult result, TransitionStatus expected,
				  std::string const &message) {
	Check(result.Status == expected, message);
}

HuntAggregate Accepted() {
	HuntAggregate aggregate;
	ExpectStatus(HuntDomain::Execute(aggregate, AcceptHunt{ProofIdentity()}),
				 TransitionStatus::Applied, "accept proof Hunt");
	return aggregate;
}

HuntAggregate Revealed() {
	HuntAggregate aggregate = Accepted();
	ExpectStatus(HuntDomain::Execute(aggregate, AdvanceTracking{100}),
				 TransitionStatus::Applied, "complete tracking");
	ExpectStatus(HuntDomain::Execute(aggregate, RevealFinal{"westfall-primary",
															"The Dust Plains"}),
				 TransitionStatus::Applied, "reveal final");
	return aggregate;
}

void TestLegalTransitions() {
	HuntAggregate aggregate;
	Check(HuntDomain::IsValid(aggregate), "fresh aggregate is valid");
	Check(!HuntDomain::IsActive(aggregate), "fresh aggregate is idle");
	ExpectStatus(HuntDomain::Execute(aggregate, AcceptHunt{ProofIdentity()}),
				 TransitionStatus::Applied, "Idle -> Tracking");
	Check(aggregate.State == HuntState::Tracking && aggregate.Revision == 1,
		  "accepted state and revision");
	ExpectStatus(HuntDomain::Execute(aggregate, AdvanceTracking{40}),
				 TransitionStatus::Applied, "tracking advances");
	ExpectStatus(HuntDomain::Execute(aggregate, AdvanceTracking{80}),
				 TransitionStatus::Applied, "tracking caps at 100");
	Check(aggregate.Progress == 100, "tracking cap");
	ExpectStatus(HuntDomain::Execute(aggregate, RevealFinal{"westfall-primary",
															"The Dust Plains"}),
				 TransitionStatus::Applied, "Tracking -> FinalRevealed");
	ExpectStatus(HuntDomain::Execute(aggregate, ActivatePrey{}),
				 TransitionStatus::Applied, "FinalRevealed -> PreyActive");
	ExpectStatus(HuntDomain::Execute(aggregate, MarkPreyKilled{}),
				 TransitionStatus::Applied, "PreyActive -> ReadyToTurnIn");
	ExpectStatus(HuntDomain::Execute(aggregate, TurnInHunt{}),
				 TransitionStatus::Applied, "ReadyToTurnIn -> Idle");
	Check(aggregate.State == HuntState::Idle && aggregate.Revision == 7,
		  "turn-in clears at revision seven");
	Check(HuntDomain::IsValid(aggregate), "turned-in aggregate is valid");
}

void TestRewardRules() {
	Check(StandardRewardQuality(0,10)==RewardQuality::Epic &&
		StandardRewardQuality(0,11)==RewardQuality::Rare &&
		StandardRewardQuality(0,201)==RewardQuality::Uncommon, "first daily rarity");
	Check(StandardRewardQuality(1,5)==RewardQuality::Epic &&
		StandardRewardQuality(1,120)==RewardQuality::Rare &&
		StandardRewardQuality(1,121)==RewardQuality::Uncommon, "second daily rarity");
	Check(StandardRewardQuality(2,2)==RewardQuality::Epic &&
		StandardRewardQuality(2,60)==RewardQuality::Rare, "third daily rarity");
	Check(StandardRewardQuality(3,20)==RewardQuality::Rare &&
		StandardRewardQuality(3,21)==RewardQuality::Uncommon, "fourth daily rarity");
	Check(HuntXpReward(10000,false,.75f,1.0f)==600 &&
		HuntXpReward(10000,true,.75f,1.0f)==0, "XP and level cap");
	Check(HuntMoneyReward(80,1.0f)==128000, "gold formula");
	Check(!EliteUnlocked(9,10) && EliteUnlocked(10,10), "elite threshold");
	Check(EliteAvailable(0,1) && !EliteAvailable(1,1), "elite accepted daily limit");
	Check(EliteSealReward(false,80,80,1,true,1)==0 &&
		EliteSealReward(true,79,80,1,true,1)==0 &&
		EliteSealReward(true,80,80,1,true,1)==2, "physical seal eligibility and fallback");
	Check(ElitePrey().size()==10, "complete Elite roster");
	Check(ElitePreyAbilities().size()>=65, "mature Elite ability profiles");
	for (auto const &prey : ElitePrey()) {
		Check(prey.Tier==PreyTier::Elite && prey.RewardMultiplier==2.5f,
			"Elite tier and reward tuning");
		Check(prey.AmbushHealthMultiplier>4.0f && prey.FinalHealthMultiplier>7.0f,
			"Elite health tuning");
	}
	Check(GuardLocatorSeeds().size()==14, "ten-city authoritative guard seeds");
}

void TestInvalidAndDuplicateEvents() {
	HuntAggregate aggregate;
	ExpectStatus(HuntDomain::Execute(aggregate, ActivatePrey{}),
				 TransitionStatus::Rejected, "activation rejected while idle");
	ExpectStatus(HuntDomain::Execute(aggregate, TurnInHunt{}),
				 TransitionStatus::Rejected, "turn-in rejected while idle");
	HuntIdentity invalid = ProofIdentity();
	invalid.PreyKey.clear();
	ExpectStatus(HuntDomain::Execute(aggregate, AcceptHunt{invalid}),
				 TransitionStatus::Rejected, "invalid identity rejected");
	HuntIdentity const identity = ProofIdentity();
	ExpectStatus(HuntDomain::Execute(aggregate, AcceptHunt{identity}),
				 TransitionStatus::Applied, "valid identity accepted");
	std::uint64_t revision = aggregate.Revision;
	ExpectStatus(HuntDomain::Execute(aggregate, AcceptHunt{identity}),
				 TransitionStatus::NoChange, "duplicate accept");
	Check(aggregate.Revision == revision, "duplicate accept keeps revision");
	ExpectStatus(HuntDomain::Execute(aggregate, AdvanceTracking{0}),
				 TransitionStatus::Rejected, "zero progress rejected");
	ExpectStatus(HuntDomain::Execute(aggregate, RevealFinal{"site", "Site"}),
				 TransitionStatus::Rejected, "early reveal rejected");
	ExpectStatus(HuntDomain::Execute(aggregate, AdvanceTracking{100}),
				 TransitionStatus::Applied, "tracking completion");
	revision = aggregate.Revision;
	ExpectStatus(HuntDomain::Execute(aggregate, AdvanceTracking{1}),
				 TransitionStatus::NoChange, "progress at cap");
	Check(aggregate.Revision == revision, "capped progress keeps revision");
	ExpectStatus(HuntDomain::Execute(aggregate, RevealFinal{"site", "Site"}),
				 TransitionStatus::Applied, "reveal accepted");
	revision = aggregate.Revision;
	ExpectStatus(HuntDomain::Execute(aggregate, RevealFinal{"site", "Site"}),
				 TransitionStatus::NoChange, "duplicate reveal");
	Check(aggregate.Revision == revision, "duplicate reveal keeps revision");
	ExpectStatus(HuntDomain::Execute(aggregate, ActivatePrey{}),
				 TransitionStatus::Applied, "activation accepted");
	ExpectStatus(HuntDomain::Execute(aggregate, ActivatePrey{}),
				 TransitionStatus::NoChange, "duplicate activation");
	ExpectStatus(HuntDomain::Execute(aggregate, MarkPreyKilled{}),
				 TransitionStatus::Applied, "kill accepted");
	ExpectStatus(HuntDomain::Execute(aggregate, MarkPreyKilled{}),
				 TransitionStatus::NoChange, "duplicate kill");
	ExpectStatus(HuntDomain::Execute(aggregate, AdvanceTracking{1}),
				 TransitionStatus::Rejected, "late tracking rejected");
}

void TestAbandonment() {
	for (HuntState target : {HuntState::Tracking, HuntState::FinalRevealed,
							 HuntState::PreyActive, HuntState::ReadyToTurnIn}) {
		HuntAggregate aggregate =
			target == HuntState::Tracking ? Accepted() : Revealed();
		if (target == HuntState::PreyActive ||
			target == HuntState::ReadyToTurnIn)
			HuntDomain::Execute(aggregate, ActivatePrey{});
		if (target == HuntState::ReadyToTurnIn)
			HuntDomain::Execute(aggregate, MarkPreyKilled{});
		Check(aggregate.State == target, "abandonment fixture state");
		ExpectStatus(HuntDomain::Execute(aggregate, AbandonHunt{}),
					 TransitionStatus::Applied, "active Hunt abandons");
		Check(aggregate.State == HuntState::Idle &&
				  HuntDomain::IsValid(aggregate),
			  "abandonment returns idle");
	}
	HuntAggregate idle;
	ExpectStatus(HuntDomain::Execute(idle, AbandonHunt{}),
				 TransitionStatus::NoChange, "duplicate abandonment");
}

void TestRecovery() {
	HuntAggregate aggregate = Revealed();
	HuntDomain::Execute(aggregate, ActivatePrey{});
	std::uint64_t const before = aggregate.Revision;
	ExpectStatus(HuntDomain::Execute(aggregate, RecoverAfterRestart{}),
				 TransitionStatus::Applied, "interrupted prey recovers");
	Check(aggregate.State == HuntState::FinalRevealed &&
			  aggregate.Revision == before + 1,
		  "recovery state/revision");
	ExpectStatus(HuntDomain::Execute(aggregate, RecoverAfterRestart{}),
				 TransitionStatus::NoChange, "repeated recovery");
}

void CheckSnapshotState(HuntAggregate const &aggregate, HuntState expected,
						bool finalVisible) {
	HuntSnapshot const snapshot =
		BuildHuntSnapshot(aggregate, NativeSealStatus::Available(9), 12);
	Check(snapshot.State == expected, "snapshot state");
	Check(snapshot.Revision == aggregate.Revision, "snapshot revision");
	Check(snapshot.FinalLocationVisible == finalVisible,
		  "snapshot final visibility");
	Check((!finalVisible && snapshot.FinalLocationName.empty()) ||
			  (finalVisible && snapshot.FinalLocationName == "The Dust Plains"),
		  "snapshot final name redaction");
	Check(snapshot.SealState == NativeContentState::Available &&
			  snapshot.PhysicalSealBalance == 9,
		  "snapshot physical Seal balance");
	Check(snapshot.LifetimeCompletions == 12, "snapshot lifetime completions");
}

void TestSnapshots() {
	HuntAggregate idle;
	HuntSnapshot idleSnapshot = BuildHuntSnapshot(
		idle, NativeSealStatus::Available(3), 4, "Seek a Huntmaster.");
	Check(idleSnapshot.State == HuntState::Idle &&
			  idleSnapshot.StatusReason == "Seek a Huntmaster.",
		  "idle reason");
	Check(idleSnapshot.HuntmasterName.empty() &&
			  !idleSnapshot.FinalLocationVisible,
		  "idle has no assignment");
	HuntAggregate tracking = Accepted();
	HuntDomain::Execute(tracking, AdvanceTracking{55});
	CheckSnapshotState(tracking, HuntState::Tracking, false);
	HuntSnapshot trackingSnapshot =
		BuildHuntSnapshot(tracking, NativeSealStatus::Available(2), 1);
	Check(trackingSnapshot.HuntmasterName == "Huntmaster Corvin" &&
			  trackingSnapshot.CityName == "Stormwind City",
		  "tracking Huntmaster");
	Check(trackingSnapshot.PreyName == "Gorehide" &&
			  trackingSnapshot.Tier == PreyTier::Standard,
		  "tracking prey");
	Check(trackingSnapshot.ZoneName == "Westfall" &&
			  trackingSnapshot.Progress == 55,
		  "tracking zone/progress");
	HuntAggregate revealed = Revealed();
	CheckSnapshotState(revealed, HuntState::FinalRevealed, true);
	HuntDomain::Execute(revealed, ActivatePrey{});
	CheckSnapshotState(revealed, HuntState::PreyActive, true);
	HuntDomain::Execute(revealed, MarkPreyKilled{});
	CheckSnapshotState(revealed, HuntState::ReadyToTurnIn, true);
	HuntSnapshot unavailable =
		BuildHuntSnapshot(tracking,
						  NativeSealStatus::Unavailable(
							  "Managed native content is not ACTIVE/APPLIED."),
						  0);
	Check(unavailable.SealState == NativeContentState::Unavailable &&
			  unavailable.PhysicalSealBalance == 0,
		  "unavailable content has no balance");
	Check(unavailable.NativeContentReason ==
			  "Managed native content is not ACTIVE/APPLIED.",
		  "unavailable reason");
}

void TestTrackingEligibility() {
	TrackingKillContext eligible{true, true, true, true, false, false};
	Check(CanAdvanceTracking(eligible) == GameplayDecision::Allowed,
		  "eligible ordinary kill advances");
	TrackingKillContext grey = eligible;
	grey.IsGrey = true;
	Check(CanAdvanceTracking(grey) == GameplayDecision::IneligibleKill,
		  "grey kill rejected");
	TrackingKillContext remote = eligible;
	remote.WithinCreditRadius = false;
	Check(CanAdvanceTracking(remote) == GameplayDecision::IneligibleKill,
		  "remote group kill rejected");
	TrackingKillContext prey = eligible;
	prey.IsHuntPrey = true;
	Check(CanAdvanceTracking(prey) == GameplayDecision::IneligibleKill,
		  "hunt prey cannot become ordinary progress");
	TrackingKillContext pending = eligible;
	pending.AmbushPending = true;
	Check(CanAdvanceTracking(pending) == GameplayDecision::IneligibleKill,
		  "pending ambush pauses ordinary tracking");
}

void TestAssignmentScopesAndAmbushes() {
	auto const *corvin = FindHuntmaster("corvin");
	auto const *varyn = FindHuntmaster("varyn");
	Check(corvin && varyn, "scope fixtures resolve");
	if (!corvin || !varyn)
		return;
	auto const local = EligibleZonesForAssignment(20, HuntSearchScope::Local,
		*corvin);
	Check(!local.empty(), "Corvin has local level-20 hunting grounds");
	for (auto const *zone : local)
		Check(IsZoneEligibleForScope(HuntSearchScope::Local, *corvin, *zone),
			"Local uses the authored Huntmaster allowlist");
	auto const continent = EligibleZonesForAssignment(
		75, HuntSearchScope::Continent, *varyn);
	Check(!continent.empty(), "Varyn has Northrend continent grounds");
	for (auto const *zone : continent)
		Check(zone->Region == HuntRegion::Northrend,
			"Continent scope does not cross gameplay continents");
	auto const world = EligibleZonesForAssignment(
		80, HuntSearchScope::World, *corvin);
	Check(!world.empty(), "World retains broad max-level assignments");
	Check(EligibleZonesForAssignment(80, HuntSearchScope::Local, *corvin).empty(),
		"no eligible local zone fails instead of widening scope");
	std::set<std::string> uniqueZones;
	for (auto const *zone : world)
		uniqueZones.emplace(zone->ZoneKey);
	Check(uniqueZones.size() == world.size(),
		"assignment candidates contain each zone once regardless of site count");

	Check(NextAmbushThreshold(0, 2) == 33 &&
			  NextAmbushThreshold(1, 2) == 66,
		"two ambushes use established one-third thresholds");
	Check(ShouldStartAmbush(30, 35, 0, 2, false),
		"crossing first threshold starts ambush");
	Check(!ShouldStartAmbush(30, 35, 0, 2, true),
		"pending ambush cannot duplicate");
	Check(!ShouldStartAmbush(95, 100, 1, 2, false),
		"final reveal is not replaced by an ambush");
}

void TestCrystalAndSpawnValidation() {
	CrystalUseContext valid{HuntState::FinalRevealed, true, true, true, true,
						true};
	Check(CanActivateCrystal(valid) == GameplayDecision::Allowed,
		  "owner crystal is usable");
	CrystalUseContext otherPlayer = valid;
	otherPlayer.CorrectPlayer = false;
	Check(CanActivateCrystal(otherPlayer) == GameplayDecision::WrongOwner,
		  "other player crystal rejected");
	CrystalUseContext wrongSite = valid;
	wrongSite.NearPersistedSite = false;
	Check(CanActivateCrystal(wrongSite) == GameplayDecision::WrongLocation,
		  "crystal away from persisted site rejected");
	CrystalUseContext wrongObject = valid;
	wrongObject.CorrectObject = false;
	Check(CanActivateCrystal(wrongObject) == GameplayDecision::WrongResource,
		  "wrong runtime crystal GUID rejected");
	CrystalUseContext wrongTemplate = valid;
	wrongTemplate.CorrectTemplate = false;
	Check(CanActivateCrystal(wrongTemplate) == GameplayDecision::WrongResource,
		  "wrong crystal template rejected");
	CrystalUseContext wrongMap = valid;
	wrongMap.SameMapAndZone = false;
	Check(CanActivateCrystal(wrongMap) == GameplayDecision::WrongLocation,
		  "crystal on wrong map or zone rejected");

	HuntAggregate revealed = Revealed();
	std::uint64_t const revision = revealed.Revision;
	ExpectStatus(ApplyPreySpawnOutcome(revealed, false),
				 TransitionStatus::NoChange, "failed spawn changes no state");
	Check(revealed.State == HuntState::FinalRevealed &&
			  revealed.Revision == revision,
		  "failed spawn remains safely revealed");
	ExpectStatus(ApplyPreySpawnOutcome(revealed, true),
				 TransitionStatus::Applied, "successful spawn activates prey");
}

void TestFinalKillRiftAndTurnInValidation() {
	FinalKillContext finalKill{HuntState::PreyActive, true, true, true};
	Check(CanCompleteFinalKill(finalKill) == GameplayDecision::Allowed,
		  "correct final prey completes");
	FinalKillContext wrongPrey = finalKill;
	wrongPrey.CorrectCreature = false;
	Check(CanCompleteFinalKill(wrongPrey) == GameplayDecision::WrongResource,
		  "incorrect prey does not complete");

	HuntAggregate ready = Revealed();
	ExpectStatus(HuntDomain::Execute(ready, ActivatePrey{}),
				 TransitionStatus::Applied, "activate prey before Rift proof");
	ExpectStatus(HuntDomain::Execute(ready, MarkPreyKilled{}),
				 TransitionStatus::Applied, "reach ReadyToTurnIn for Rift proof");
	std::uint64_t const readyRevision = ready.Revision;
	ReturnRiftUseContext rift{ready.State, true, true, true,
							  true, true, true};
	Check(CanUseReturnRift(rift) == GameplayDecision::Allowed,
		  "owner-bound rift accepted");
	Check(ready.State == HuntState::ReadyToTurnIn &&
			  ready.Revision == readyRevision,
		  "Rift validation does not complete or mutate the Hunt");
	rift.CorrectPlayer = false;
	Check(CanUseReturnRift(rift) == GameplayDecision::WrongOwner,
		  "another player's rift rejected");
	rift.CorrectPlayer = true;
	rift.CorrectObject = false;
	Check(CanUseReturnRift(rift) == GameplayDecision::WrongResource,
		  "wrong runtime Rift GUID rejected");

	TurnInContext turnIn{HuntState::ReadyToTurnIn, true, true, true};
	Check(CanTurnIn(turnIn) == GameplayDecision::Allowed,
		  "valid physical Seal turn-in");
	turnIn.HasInventorySpace = false;
	Check(CanTurnIn(turnIn) == GameplayDecision::InventoryFull,
		  "full inventory refuses turn-in");
	turnIn.HasInventorySpace = true;
	turnIn.SealResourceAvailable = false;
	Check(CanTurnIn(turnIn) == GameplayDecision::WrongResource,
		  "unavailable managed Seal refuses turn-in");
}

void TestAuthoredFinalSiteCatalog() {
	auto const &sites = KnownFinalSites();
	Check(sites.size() == 195, "complete authored final-site count");
	std::size_t easternKingdoms = 0;
	std::size_t kalimdor = 0;
	std::size_t outland = 0;
	std::size_t northrend = 0;
	std::set<std::string> keys;
	std::set<std::string> zones;
	for (auto const &site : sites) {
		keys.emplace(site.Key);
		zones.emplace(site.ZoneKey);
		switch (site.Region) {
		case HuntRegion::EasternKingdoms: ++easternKingdoms; break;
		case HuntRegion::Kalimdor: ++kalimdor; break;
		case HuntRegion::Outland: ++outland; break;
		case HuntRegion::Northrend: ++northrend; break;
		}
		Check(site.MinLevel > 0 && site.MinLevel <= site.MaxLevel,
			  "site inherits valid zone-level applicability");
	}
	Check(keys.size() == sites.size(), "authored site keys are unique");
	Check(easternKingdoms == 84 && kalimdor == 63 && outland == 21 &&
			  northrend == 27,
		  "authored regional site counts");
	for (auto const &zone : zones)
		Check(FinalSitesForZone(zone).size() >= 3,
			  "every authored hunting zone has multiple final sites");
}
} // namespace

int main(int argc, char **argv) {
	std::string const selection = argc > 1 ? argv[1] : "all";
	if (selection == "all" || selection == "transitions")
		TestLegalTransitions();
	if (selection == "all" || selection == "rewards")
		TestRewardRules();
	if (selection == "all" || selection == "invalid")
		TestInvalidAndDuplicateEvents();
	if (selection == "all" || selection == "abandonment")
		TestAbandonment();
	if (selection == "all" || selection == "recovery")
		TestRecovery();
	if (selection == "all" || selection == "snapshots")
		TestSnapshots();
	if (selection == "all" || selection == "tracking")
		TestTrackingEligibility();
	if (selection == "all" || selection == "scope")
		TestAssignmentScopesAndAmbushes();
	if (selection == "all" || selection == "crystal")
		TestCrystalAndSpawnValidation();
	if (selection == "all" || selection == "completion")
		TestFinalKillRiftAndTurnInValidation();
	if (selection == "all" || selection == "sites")
		TestAuthoredFinalSiteCatalog();
	if (Failures != 0)
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
