#include "Config.h"
#include "Log.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <cstdint>

namespace {
struct FoundationConfig {
	bool Enabled = true;
	bool Debug = false;
	std::uint32_t MinimumLevel = 10;
	std::uint32_t TrackingProgressMin = 3;
	std::uint32_t TrackingProgressMax = 7;
	std::uint32_t RewardSeals = 1;
	bool ReturnRiftEnabled = true;
	std::uint32_t ReturnRiftDurationSeconds = 120;
};

FoundationConfig Config;

class NativeHuntsWorldScript final : public WorldScript {
public:
	NativeHuntsWorldScript() :
		WorldScript("NativeHuntsWorldScript",
					{WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP}) {}

	void OnAfterConfigLoad(bool /*reload*/) override {
		Config.Enabled =
			sConfigMgr->GetOption<bool>("NativeHunts.Enable", true);
		Config.Debug = sConfigMgr->GetOption<bool>("NativeHunts.Debug", false);
		Config.MinimumLevel =
			std::max<std::uint32_t>(1, sConfigMgr->GetOption<std::uint32_t>(
										   "NativeHunts.MinimumLevel", 10));

		std::uint32_t const progressA = std::min<std::uint32_t>(
			100, sConfigMgr->GetOption<std::uint32_t>(
					 "NativeHunts.Tracking.ProgressMin", 3));
		std::uint32_t const progressB = std::min<std::uint32_t>(
			100, sConfigMgr->GetOption<std::uint32_t>(
					 "NativeHunts.Tracking.ProgressMax", 7));
		Config.TrackingProgressMin = std::min(progressA, progressB);
		Config.TrackingProgressMax = std::max(progressA, progressB);
		Config.RewardSeals =
			sConfigMgr->GetOption<std::uint32_t>("NativeHunts.Reward.Seals", 1);
		Config.ReturnRiftEnabled =
			sConfigMgr->GetOption<bool>("NativeHunts.ReturnRift.Enable", true);
		Config.ReturnRiftDurationSeconds = std::max<std::uint32_t>(
			1, sConfigMgr->GetOption<std::uint32_t>(
				   "NativeHunts.ReturnRift.DurationSeconds", 120));
	}

	void OnStartup() override {
		if (!Config.Enabled) {
			AC_LOG_INFO("module.native_hunts",
						"mod-native-hunts foundation is disabled.");
			return;
		}

		AC_LOG_INFO("module.native_hunts",
					"mod-native-hunts Milestone 1 foundation loaded. Gameplay "
					"remains unavailable until managed native content and "
					"persistence are implemented.");

		if (Config.Debug) {
			AC_LOG_INFO(
				"module.native_hunts",
				"Foundation defaults: minimumLevel={}, tracking={}..{}, "
				"rewardSeals={}, returnRift={}, returnRiftSeconds={}",
				Config.MinimumLevel, Config.TrackingProgressMin,
				Config.TrackingProgressMax, Config.RewardSeals,
				Config.ReturnRiftEnabled, Config.ReturnRiftDurationSeconds);
		}
	}
};
} // namespace

void AddNativeHuntsModuleScripts() { new NativeHuntsWorldScript(); }
