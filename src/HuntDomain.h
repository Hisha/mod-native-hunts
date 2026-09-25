#ifndef MOD_NATIVE_HUNTS_DOMAIN_H
#define MOD_NATIVE_HUNTS_DOMAIN_H

#include <cstdint>
#include <string>
#include <variant>

namespace native_hunts {
enum class HuntState : std::uint8_t {
	Idle,
	Tracking,
	FinalRevealed,
	PreyActive,
	ReadyToTurnIn
};

enum class PreyTier : std::uint8_t { None, Standard, Elite };

struct HuntIdentity {
	std::string HuntmasterKey;
	std::string HuntmasterName;
	std::string CityName;
	std::string PreyKey;
	std::string PreyName;
	PreyTier Tier = PreyTier::None;
	std::string ZoneKey;
	std::string ZoneName;

	bool operator==(HuntIdentity const &other) const;
};

struct HuntAggregate {
	HuntState State = HuntState::Idle;
	std::uint64_t Revision = 0;
	HuntIdentity Identity;
	std::uint8_t Progress = 0;
	std::string FinalLocationKey;
	std::string FinalLocationName;
};

struct AcceptHunt {
	HuntIdentity Identity;
};
struct AdvanceTracking {
	std::uint8_t Amount = 0;
};
struct RevealFinal {
	std::string LocationKey;
	std::string LocationName;
};
struct ActivatePrey {};
struct MarkPreyKilled {};
struct TurnInHunt {};
struct AbandonHunt {};
struct RecoverAfterRestart {};

using HuntCommand =
	std::variant<AcceptHunt, AdvanceTracking, RevealFinal, ActivatePrey,
				 MarkPreyKilled, TurnInHunt, AbandonHunt, RecoverAfterRestart>;

enum class TransitionStatus : std::uint8_t { Applied, NoChange, Rejected };
enum class TransitionError : std::uint8_t {
	None,
	InvalidState,
	InvalidCommand,
	InvalidIdentity,
	TrackingIncomplete
};

struct TransitionResult {
	TransitionStatus Status = TransitionStatus::Rejected;
	TransitionError Error = TransitionError::InvalidCommand;
};

class HuntDomain {
public:
	static TransitionResult Execute(HuntAggregate &aggregate,
									HuntCommand const &command);
	static bool IsValid(HuntAggregate const &aggregate);
	static bool IsActive(HuntAggregate const &aggregate);
};
} // namespace native_hunts

#endif
