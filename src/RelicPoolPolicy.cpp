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

RelicPlacementRun RunRelicPlacementAttempts(std::uint32_t count,
		std::function<bool()> const &tryCandidate) {
	RelicPlacementRun result;
	std::uint32_t const limit = RelicPlacementAttemptLimit(count);
	while (result.Attempts < limit && result.Accepted < count) {
		++result.Attempts;
		if (tryCandidate && tryCandidate())
			++result.Accepted;
	}
	result.Complete = result.Accepted == count;
	return result;
}

} // namespace native_hunts
