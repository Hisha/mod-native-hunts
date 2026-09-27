#ifndef MOD_NATIVE_HUNTS_CATALOG_H
#define MOD_NATIVE_HUNTS_CATALOG_H

#include "HuntDomain.h"

#include <cstdint>
#include <string>
#include <vector>

namespace native_hunts {
enum class HuntRegion : std::uint8_t {
	EasternKingdoms,
	Kalimdor,
	Outland,
	Northrend
};

enum class HuntSearchScope : std::uint8_t { Local, Continent, World };

struct HuntmasterDefinition {
	char const *Key;
	char const *Symbol;
	char const *SpawnSymbol;
	char const *Name;
	char const *City;
	HuntRegion Region;
};

struct PreyDefinition {
	char const *Key;
	char const *Symbol;
	char const *Name;
	PreyTier Tier;
	std::uint8_t MinLevel;
	std::uint8_t MaxLevel;
	float AmbushHealthMultiplier;
	float FinalHealthMultiplier;
	float RewardMultiplier;
	float HealthModifier;
	float ArmorModifier;
	float DamageModifier;
	bool Ranged;
	float PreferredRange;
};

struct PreyAbilityDefinition {
	char const *PreyKey;
	std::uint32_t SpellId;
	bool SelfTarget;
	std::uint32_t InitialMinMs;
	std::uint32_t InitialMaxMs;
	std::uint32_t CooldownMinMs;
	std::uint32_t CooldownMaxMs;
	std::uint8_t ChancePercent;
	std::uint8_t EncounterMask = 3;
	std::uint8_t MinHunterLevel = 1;
	std::uint8_t MaxHunterLevel = 80;
	std::uint8_t HealthBelowPercent = 0;
	std::uint8_t VictimHealthBelowPercent = 0;
	bool RequireMelee = false;
	bool OncePerEncounter = false;
	bool RequireAuraMissing = false;
};

struct GuardLocatorSeed {
	std::uint32_t CreatureEntry;
	char const *HuntmasterKey;
};

struct FinalSiteDefinition {
	char const *Key;
	char const *ZoneKey;
	char const *ZoneName;
	HuntRegion Region;
	std::uint32_t ZoneId;
	std::uint32_t MapId;
	std::uint8_t MinLevel;
	std::uint8_t MaxLevel;
	float X;
	float Y;
	float Z;
	float Orientation;
};

std::vector<HuntmasterDefinition> const &Huntmasters();
std::vector<PreyDefinition> const &StandardPrey();
std::vector<PreyDefinition> const &ElitePrey();
std::vector<PreyAbilityDefinition> const &StandardPreyAbilities();
std::vector<PreyAbilityDefinition> const &ElitePreyAbilities();
std::vector<GuardLocatorSeed> const &GuardLocatorSeeds();
std::vector<FinalSiteDefinition> const &KnownFinalSites();

HuntmasterDefinition const *FindHuntmaster(std::string const &key);
PreyDefinition const *FindPrey(std::string const &key);
FinalSiteDefinition const *FindFinalSite(std::string const &key);
FinalSiteDefinition const *FindZone(std::string const &zoneKey);
std::vector<FinalSiteDefinition const *>
FinalSitesForZone(std::string const &zoneKey);
bool IsZoneEligibleForScope(HuntSearchScope scope,
							 HuntmasterDefinition const &huntmaster,
							 FinalSiteDefinition const &zone);
std::vector<FinalSiteDefinition const *> EligibleZonesForAssignment(
	std::uint8_t level, HuntSearchScope scope,
	HuntmasterDefinition const &huntmaster);
} // namespace native_hunts

#endif
