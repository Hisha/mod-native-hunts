#ifndef MOD_NATIVE_HUNTS_RELIC_POOL_POLICY_H
#define MOD_NATIVE_HUNTS_RELIC_POOL_POLICY_H

#include <cstddef>
#include <cstdint>
#include <functional>

namespace native_hunts {

constexpr std::uint32_t DefaultRelicsPerZone = 60;
constexpr std::uint32_t MaximumRelicsPerZone = 200;
constexpr std::uint32_t RelicPlacementAttemptsPerObject = 100;

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
};

RelicCountPolicy NormalizeRelicsPerZone(std::int64_t configured);
std::uint32_t RelicPlacementAttemptLimit(std::uint32_t count);
bool ShouldRetireRelicPool(std::size_t associatedHunts);
RelicPlacementRun RunRelicPlacementAttempts(std::uint32_t count,
	std::function<bool()> const &tryCandidate);

} // namespace native_hunts

#endif
