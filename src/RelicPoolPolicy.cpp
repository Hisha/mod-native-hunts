#include "RelicPoolPolicy.h"

#include <algorithm>
#include <limits>

namespace native_hunts {

RelicCountPolicy NormalizeRelicsPerZone(std::int64_t configured) {
	if (configured < 0)
		return {0, true};
	if (configured > MaximumRelicsPerZone)
		return {MaximumRelicsPerZone, true};
	return {static_cast<std::uint32_t>(configured), false};
}

std::uint32_t RelicPlacementAttemptLimit(std::uint32_t count) {
	if (count == 0)
		return 0;
	std::uint64_t const attempts = static_cast<std::uint64_t>(count) *
		RelicPlacementAttemptsPerObject;
	return static_cast<std::uint32_t>(std::min<std::uint64_t>(attempts,
		std::numeric_limits<std::uint32_t>::max()));
}

bool ShouldRetireRelicPool(std::size_t associatedHunts) {
	return associatedHunts == 0;
}

bool ShouldCancelRelicGeneration(bool relicsEnabled,
		std::size_t associatedHunts) {
	return !relicsEnabled || associatedHunts == 0;
}

RelicGenerationRequest DecideRelicGenerationRequest(bool poolExists,
		bool generationPending, bool retryScheduled, bool retryDue) {
	if (poolExists)
		return RelicGenerationRequest::UseExisting;
	if (generationPending)
		return RelicGenerationRequest::JoinPending;
	if (retryScheduled && !retryDue)
		return RelicGenerationRequest::WaitForRetry;
	return RelicGenerationRequest::Start;
}

RelicPersistenceAction DecideRelicPersistenceAction(
		RelicPersistenceObservation observation, bool generationComplete) {
	switch (observation) {
	case RelicPersistenceObservation::Complete:
		return RelicPersistenceAction::RecoverPersisted;
	case RelicPersistenceObservation::Invalid:
		return RelicPersistenceAction::ReplaceInvalid;
	case RelicPersistenceObservation::Missing:
		return generationComplete ? RelicPersistenceAction::CommitGenerated
			: RelicPersistenceAction::Wait;
	case RelicPersistenceObservation::Unavailable:
		return RelicPersistenceAction::Wait;
	}
	return RelicPersistenceAction::Wait;
}

RelicPlacementRun AdvanceRelicPlacementAttempts(std::uint32_t count,
		std::uint32_t accepted, std::uint32_t attempts, std::uint32_t budget,
		std::function<bool()> const &tryCandidate) {
	RelicPlacementRun result{accepted, attempts, accepted == count, false};
	std::uint32_t const limit = RelicPlacementAttemptLimit(count);
	std::uint32_t processed = 0;
	while (processed < budget && result.Attempts < limit &&
			result.Accepted < count) {
		++processed;
		++result.Attempts;
		if (tryCandidate && tryCandidate())
			++result.Accepted;
	}
	result.Complete = result.Accepted == count;
	result.Exhausted = !result.Complete && result.Attempts >= limit;
	return result;
}

RelicPlacementRun RunRelicPlacementAttempts(std::uint32_t count,
		std::function<bool()> const &tryCandidate) {
	return AdvanceRelicPlacementAttempts(count, 0, 0,
		RelicPlacementAttemptLimit(count), tryCandidate);
}

} // namespace native_hunts
