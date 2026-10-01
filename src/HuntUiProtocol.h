#ifndef MOD_NATIVE_HUNTS_UI_PROTOCOL_H
#define MOD_NATIVE_HUNTS_UI_PROTOCOL_H

#include "HuntSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace native_hunts::ui {
inline constexpr char Prefix[] = "NHUNTS";
inline constexpr std::uint32_t Version = 1;
inline constexpr std::size_t MaxWireBytes = 255;
inline constexpr std::size_t MaxPayloadBytes = MaxWireBytes - sizeof(Prefix);
inline constexpr std::size_t MaxFragments = 8;
inline constexpr std::size_t MaxEncodedRecordBytes = 1024;

struct SnapshotRequest { std::uint32_t Nonce = 0; };

std::optional<SnapshotRequest> ParseSnapshotRequest(std::string_view payload);
std::string Escape(std::string_view value);
std::vector<std::string> SerializeSnapshot(HuntSnapshot const &snapshot,
	std::uint32_t sequence, std::uint32_t nonce);
bool RequestAllowed(std::uint64_t previousMilliseconds,
	std::uint64_t nowMilliseconds, std::uint64_t intervalMilliseconds = 1000);
} // namespace native_hunts::ui

#endif
