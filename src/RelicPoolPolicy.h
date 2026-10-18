#ifndef MOD_NATIVE_HUNTS_RELIC_POOL_POLICY_H
#define MOD_NATIVE_HUNTS_RELIC_POOL_POLICY_H

#include <cstddef>
#include <cstdint>
#include <functional>

namespace native_hunts {

constexpr std::uint32_t DefaultRelicsPerZone = 60;
constexpr std::uint32_t MaximumRelicsPerZone = 200;
constexpr std::uint32_t RelicPlacementAttemptsPerObject = 100;
constexpr std::uint32_t RelicPlacementCandidatesPerUpdate = 32;
constexpr std::uint32_t RelicPlacementCandidatesPerJob = 8;

struct RelicCountPolicy {
	std::uint32_t Count = DefaultRelicsPerZone;
	bool Adjusted = false;
};

struct RelicPoolScope {
	std::uint32_t MapId = 0;
	std::uint32_t InstanceId = 0;
	std::uint32_t ZoneId = 0;

	bool operator==(RelicPoolScope const &other) const {
		return MapId == other.MapId && InstanceId == other.InstanceId &&
			ZoneId == other.ZoneId;
	}
};

struct RelicPoolScopeHash {
	std::size_t operator()(RelicPoolScope const &scope) const {
		std::size_t value = std::hash<std::uint32_t>{}(scope.MapId);
		value ^= std::hash<std::uint32_t>{}(scope.InstanceId) +
			0x9e3779b9u + (value << 6u) + (value >> 2u);
		value ^= std::hash<std::uint32_t>{}(scope.ZoneId) +
			0x9e3779b9u + (value << 6u) + (value >> 2u);
		return value;
	}
};

struct RelicPlacementRun {
	std::uint32_t Accepted = 0;
	std::uint32_t Attempts = 0;
	bool Complete = false;
	bool Exhausted = false;
};

enum class RelicGenerationRequest {
	UseExisting,
	JoinPending,
	WaitForRetry,
	Start
};

enum class RelicPersistenceObservation {
	Unavailable,
	Missing,
	Complete,
	Invalid
};

enum class RelicPersistenceAction {
	Wait,
	CommitGenerated,
	RecoverPersisted,
	ReplaceInvalid
};

RelicCountPolicy NormalizeRelicsPerZone(std::int64_t configured);
std::uint32_t RelicPlacementAttemptLimit(std::uint32_t count);
bool ShouldRetireRelicPool(std::size_t associatedHunts);
bool ShouldCancelRelicGeneration(bool relicsEnabled,
	std::size_t associatedHunts);
RelicGenerationRequest DecideRelicGenerationRequest(bool poolExists,
	bool generationPending, bool retryScheduled, bool retryDue);
RelicPersistenceAction DecideRelicPersistenceAction(
	RelicPersistenceObservation observation, bool generationComplete);
RelicPlacementRun AdvanceRelicPlacementAttempts(std::uint32_t count,
	std::uint32_t accepted, std::uint32_t attempts, std::uint32_t budget,
	std::function<bool()> const &tryCandidate);
RelicPlacementRun RunRelicPlacementAttempts(std::uint32_t count,
	std::function<bool()> const &tryCandidate);

} // namespace native_hunts

#endif
