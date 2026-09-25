#include "HuntDomain.h"
#include "HuntSnapshot.h"

#include <cstdlib>
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
} // namespace

int main(int argc, char **argv) {
	std::string const selection = argc > 1 ? argv[1] : "all";
	if (selection == "all" || selection == "transitions")
		TestLegalTransitions();
	if (selection == "all" || selection == "invalid")
		TestInvalidAndDuplicateEvents();
	if (selection == "all" || selection == "abandonment")
		TestAbandonment();
	if (selection == "all" || selection == "recovery")
		TestRecovery();
	if (selection == "all" || selection == "snapshots")
		TestSnapshots();
	if (Failures != 0)
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
