#include "HuntUiProtocol.h"

#include <array>
#include <charconv>
#include <limits>

namespace native_hunts::ui {
namespace {
bool ParseUint(std::string_view value, std::uint32_t &out) {
	if (value.empty() || value.size() > 10)
		return false;
	auto const result = std::from_chars(value.data(), value.data() + value.size(), out);
	return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

char Hex(unsigned value) { return "0123456789ABCDEF"[value & 15]; }

char StateCode(HuntState state) {
	switch (state) {
	case HuntState::Idle: return 'I';
	case HuntState::Tracking: return 'T';
	case HuntState::FinalRevealed: return 'F';
	case HuntState::PreyActive: return 'P';
	case HuntState::ReadyToTurnIn: return 'R';
	}
	return 'I';
}

char TierCode(PreyTier tier) {
	return tier == PreyTier::Elite ? 'E' : tier == PreyTier::Standard ? 'S' : 'N';
}

std::vector<std::string> Frame(std::string const &kind,
	std::uint32_t sequence, std::uint32_t nonce, std::string const &body) {
	std::string direct = "1\t" + kind + "\t" + std::to_string(sequence) + "\t" +
		std::to_string(nonce) + "\t" + body;
	if (direct.size() <= MaxPayloadBytes)
		return {std::move(direct)};
	if (body.size() > MaxEncodedRecordBytes)
		return {};
	std::string const base = "1\tF\t" + std::to_string(sequence) + "\t" +
		std::to_string(nonce) + "\t" + kind + "\t";
	// Reserve room for two one-digit indices plus separators. MaxFragments is 8.
	if (base.size() + 6 >= MaxPayloadBytes)
		return {};
	std::size_t const chunkSize = MaxPayloadBytes - base.size() - 6;
	std::size_t const count = (body.size() + chunkSize - 1) / chunkSize;
	if (!count || count > MaxFragments)
		return {};
	std::vector<std::string> result;
	result.reserve(count);
	for (std::size_t index = 0; index < count; ++index) {
		std::string message = base + std::to_string(index + 1) + "\t" +
			std::to_string(count) + "\t" + body.substr(index * chunkSize, chunkSize);
		if (message.size() > MaxPayloadBytes)
			return {};
		result.push_back(std::move(message));
	}
	return result;
}
} // namespace

std::optional<SnapshotRequest> ParseSnapshotRequest(std::string_view payload) {
	if (payload.size() > MaxPayloadBytes)
		return std::nullopt;
	std::array<std::string_view, 3> fields;
	std::size_t start = 0;
	for (std::size_t index = 0; index < fields.size(); ++index) {
		std::size_t const end = payload.find('\t', start);
		if ((end == std::string_view::npos) != (index + 1 == fields.size()))
			return std::nullopt;
		fields[index] = payload.substr(start, end == std::string_view::npos
			? payload.size() - start : end - start);
		start = end == std::string_view::npos ? payload.size() : end + 1;
	}
	std::uint32_t version = 0;
	std::uint32_t nonce = 0;
	if (!ParseUint(fields[0], version) || version != Version || fields[1] != "Q" ||
		!ParseUint(fields[2], nonce) || nonce == 0)
		return std::nullopt;
	return SnapshotRequest{nonce};
}

std::string Escape(std::string_view value) {
	std::string result;
	result.reserve(value.size());
	for (unsigned char byte : value) {
		bool const safe = (byte >= 'a' && byte <= 'z') ||
			(byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
			byte == ' ' || byte == '-' || byte == '_' || byte == '.' || byte == '\'';
		if (safe)
			result.push_back(static_cast<char>(byte));
		else {
			result.push_back('%');
			result.push_back(Hex(byte >> 4));
			result.push_back(Hex(byte));
		}
	}
	return result;
}

std::vector<std::string> SerializeSnapshot(HuntSnapshot const &snapshot,
	std::uint32_t sequence, std::uint32_t nonce) {
	std::string assignment = std::to_string(snapshot.Revision) + "\t" +
		(snapshot.ContentAvailable ? "1" : "0") + "\t" +
		(snapshot.Active ? "1" : "0") + "\t" + StateCode(snapshot.State) + "\t" +
		TierCode(snapshot.Tier) + "\t" + std::to_string(snapshot.Progress) + "\t" +
		(snapshot.FinalLocationVisible ? "1" : "0") + "\t" +
		(snapshot.ReadyToTurnIn ? "1" : "0") + "\t" + Escape(snapshot.HuntmasterName) + "\t" +
		Escape(snapshot.CityName) + "\t" + Escape(snapshot.PreyName) + "\t" +
		Escape(snapshot.ZoneName) + "\t" + Escape(snapshot.FinalLocationName) + "\t" +
		Escape(!snapshot.ContentAvailable ? snapshot.NativeContentReason : snapshot.StatusReason);
	std::string progression = std::to_string(snapshot.Progression.StandardCompleted) + "\t" +
		std::to_string(snapshot.Progression.EliteCompleted) + "\t" +
		(snapshot.Progression.EliteUnlocked ? "1" : "0") + "\t" +
		std::to_string(snapshot.Progression.EliteAcceptedToday) + "\t" +
		std::to_string(snapshot.Progression.EliteDailyLimit) + "\t" +
		(snapshot.Progression.EliteAvailableToday ? "1" : "0") + "\t" +
		(snapshot.SealState == NativeContentState::Available ? "A" : "U") + "\t" +
		std::to_string(snapshot.PhysicalSealBalance);
	auto messages = Frame("A", sequence, nonce, assignment);
	auto stats = Frame("P", sequence, nonce, progression);
	if (messages.empty() || stats.empty())
		return {};
	messages.insert(messages.end(), stats.begin(), stats.end());
	return messages;
}

bool RequestAllowed(std::uint64_t previousMilliseconds,
	std::uint64_t nowMilliseconds, std::uint64_t intervalMilliseconds) {
	return previousMilliseconds == 0 || nowMilliseconds < previousMilliseconds ||
		nowMilliseconds - previousMilliseconds >= intervalMilliseconds;
}
} // namespace native_hunts::ui
