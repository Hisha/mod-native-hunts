#include "HuntCatalog.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace native_hunts {
std::vector<HuntmasterDefinition> const &Huntmasters() {
	static std::vector<HuntmasterDefinition> const values = {
		{"corvin", "huntmaster-corvin", "huntmaster-corvin-spawn", "Huntmaster Corvin", "Stormwind City", HuntRegion::EasternKingdoms},
		{"brannoc", "huntmaster-brannoc", "huntmaster-brannoc-spawn", "Huntmaster Brannoc", "Ironforge", HuntRegion::EasternKingdoms},
		{"shalara", "huntmaster-shalara", "huntmaster-shalara-spawn", "Huntmistress Shalara", "Darnassus", HuntRegion::Kalimdor},
		{"veylan", "huntmaster-veylan", "huntmaster-veylan-spawn", "Huntmaster Veylan", "The Exodar", HuntRegion::Kalimdor},
		{"gorrak", "huntmaster-gorrak", "huntmaster-gorrak-spawn", "Huntmaster Gorrak", "Orgrimmar", HuntRegion::Kalimdor},
		{"tahu", "huntmaster-tahu", "huntmaster-tahu-spawn", "Huntmaster Tahu", "Thunder Bluff", HuntRegion::Kalimdor},
		{"morcant", "huntmaster-morcant", "huntmaster-morcant-spawn", "Huntmaster Morcant", "Undercity", HuntRegion::EasternKingdoms},
		{"vaelith", "huntmaster-vaelith", "huntmaster-vaelith-spawn", "Huntmistress Vaelith", "Silvermoon City", HuntRegion::EasternKingdoms},
		{"raleth", "huntmaster-raleth", "huntmaster-raleth-spawn", "Huntmaster Raleth", "Shattrath City", HuntRegion::Outland},
		{"varyn", "huntmaster-varyn", "huntmaster-varyn-spawn", "Huntmaster Varyn", "Dalaran", HuntRegion::Northrend},
	};
	return values;
}

std::vector<PreyDefinition> const &StandardPrey() {
	static std::vector<PreyDefinition> const values = {
		{"ashfang", "prey-ashfang", "Ashfang", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"silkmaw", "prey-silkmaw", "Silkmaw", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"gorehide", "prey-gorehide", "Gorehide", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"whiteclaw", "prey-whiteclaw", "Whiteclaw", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"tidefang", "prey-tidefang", "Tidefang", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"stonegut", "prey-stonegut", "Stonegut", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"sootfang", "prey-sootfang", "Sootfang", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"nightfang", "prey-nightfang", "Nightfang", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"shadowclaw", "prey-shadowclaw", "Shadowclaw", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"dreadwing", "prey-dreadwing", "Dreadwing", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"venomtail", "prey-venomtail", "Venomtail", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"stormcoil", "prey-stormcoil", "Stormcoil", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"mirejaw", "prey-mirejaw", "Mirejaw", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"razortalon", "prey-razortalon", "Razortalon", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"cliffhowl", "prey-cliffhowl", "Cliffhowl", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
		{"grimmaw", "prey-grimmaw", "Grimmaw", PreyTier::Standard, 10, 80, 4.0f, 6.0f, 1.0f, 1, 1, 1, false, 0},
	};
	return values;
}

std::vector<PreyDefinition> const &ElitePrey() {
	static std::vector<PreyDefinition> const values = {
		{"oathbreaker", "elite-oathbreaker", "The Oathbreaker", PreyTier::Elite, 10, 80, 5.0f, 8.0f, 2.5f, 1.0f, 1.20f, 1.12f, false, 0},
		{"winterborn", "elite-winterborn", "The Winterborn", PreyTier::Elite, 10, 80, 4.5f, 7.25f, 2.5f, .90f, .85f, 1.08f, true, 22},
		{"headsman", "elite-headsman", "The Headsman", PreyTier::Elite, 10, 80, 5.25f, 8.5f, 2.5f, 1.10f, 1.20f, 1.15f, false, 0},
		{"veiled-knife", "elite-veiled-knife", "The Veiled Knife", PreyTier::Elite, 10, 80, 4.75f, 7.75f, 2.5f, .95f, .90f, 1.16f, false, 0},
		{"ashen-pact", "elite-ashen-pact", "The Ashen Pact", PreyTier::Elite, 10, 80, 4.75f, 7.75f, 2.5f, 1.0f, .90f, 1.10f, true, 24},
		{"wildclaw", "elite-wildclaw", "The Wildclaw", PreyTier::Elite, 10, 80, 5.0f, 8.0f, 2.5f, 1.05f, 1.05f, 1.12f, false, 0},
		{"stormcaller", "elite-stormcaller", "The Stormcaller", PreyTier::Elite, 10, 80, 5.0f, 8.0f, 2.5f, 1.05f, 1.08f, 1.13f, false, 0},
		{"dusk-confessor", "elite-dusk-confessor", "The Dusk Confessor", PreyTier::Elite, 10, 80, 4.75f, 7.75f, 2.5f, .95f, .90f, 1.10f, true, 20},
		{"gravebound", "elite-gravebound", "The Gravebound", PreyTier::Elite, 10, 80, 5.25f, 8.5f, 2.5f, 1.10f, 1.18f, 1.14f, false, 0},
		{"farstrider", "elite-farstrider", "The Farstrider", PreyTier::Elite, 10, 80, 4.75f, 7.75f, 2.5f, 1.0f, .95f, 1.12f, true, 24},
	};
	return values;
}

std::vector<PreyAbilityDefinition> const &StandardPreyAbilities() {
	static std::vector<PreyAbilityDefinition> const values = {
		{"ashfang",13443,false,4000,7000,14000,19000,80},{"ashfang",19615,true,9000,13000,24000,32000,70},
		{"silkmaw",4167,false,5000,8000,18000,24000,100},{"silkmaw",18197,false,2500,5000,12000,17000,80},
		{"gorehide",25999,false,500,1500,18000,24000,100},{"gorehide",3391,true,8000,12000,22000,30000,60},
		{"whiteclaw",5164,false,5000,8000,18000,24000,75},{"whiteclaw",15971,true,7000,11000,24000,32000,75},
		{"tidefang",3391,true,5000,9000,16000,22000,75},{"tidefang",11428,false,9000,13000,24000,32000,60},
		{"stonegut",5164,false,5000,8000,18000,24000,70},{"stonegut",3391,true,7000,11000,18000,26000,70},
		{"sootfang",15971,true,4000,7000,22000,30000,80},{"sootfang",19615,true,9000,13000,24000,32000,70},
		{"nightfang",13443,false,3500,6500,13000,18000,85},{"nightfang",19615,true,7000,11000,22000,30000,70},
		{"shadowclaw",13443,false,3000,6000,12000,17000,90},{"shadowclaw",5164,false,8000,12000,24000,32000,60},
		{"dreadwing",8281,true,5000,9000,22000,30000,70},{"dreadwing",14100,true,10000,15000,30000,40000,55},
		{"venomtail",18197,false,2500,5000,12000,17000,85},{"venomtail",5164,false,8000,12000,22000,30000,60},
		{"stormcoil",3391,true,5000,8000,16000,22000,80},{"stormcoil",19615,true,9000,13000,24000,32000,65},
		{"mirejaw",5164,false,4500,7500,18000,24000,75},{"mirejaw",13443,false,7000,11000,16000,22000,75},
		{"razortalon",13443,false,3000,6000,12000,17000,90},{"razortalon",3391,true,6000,9000,16000,22000,80},
		{"cliffhowl",13443,false,3500,6500,13000,18000,85},{"cliffhowl",14100,true,9000,14000,30000,40000,50},
		{"grimmaw",5164,false,4500,7500,18000,24000,75},{"grimmaw",8599,true,9000,13000,26000,34000,65},
	};
	return values;
}

std::vector<PreyAbilityDefinition> const &ElitePreyAbilities() {
	static std::vector<PreyAbilityDefinition> const values = {
		{"oathbreaker",20375,true,0,500,30000,30000,100,3,10,80,0,0,false,false,true},
		{"oathbreaker",20271,false,2500,4500,8000,11000,100,3,10,80,0,0,false,false,false},
		{"oathbreaker",35395,false,1500,3000,6000,8000,100,3,20,80,0,0,true,false,false},
		{"oathbreaker",48819,true,5000,8000,12000,16000,85,2,30,80,0,0,true,false,false},
		{"oathbreaker",10308,false,7000,11000,28000,35000,70,2,30,80,0,0,true,false,false},
		{"oathbreaker",53385,false,4000,6500,10000,13000,100,2,40,80,0,0,true,false,false},
		{"oathbreaker",31884,true,0,0,60000,60000,100,2,60,80,40,0,false,true,false},
		{"winterborn",42842,false,1000,2500,2500,4000,100,3,10,80,0,0,false,false,false},
		{"winterborn",42917,false,3500,6000,18000,24000,100,3,10,80,0,0,false,false,false},
		{"winterborn",1953,true,1500,3000,15000,15000,100,3,20,80,0,0,false,false,false},
		{"winterborn",43039,true,500,1500,24000,32000,100,3,20,80,0,0,false,false,true},
		{"winterborn",42931,false,6000,9000,10000,14000,75,2,30,80,0,0,false,false,false},
		{"winterborn",45438,true,0,0,60000,60000,100,2,50,80,25,0,false,true,false},
		{"headsman",11578,false,0,1200,14000,18000,100,3,10,80,0,0,false,false,false},
		{"headsman",25212,false,1000,2200,8000,10000,100,3,10,80,0,0,true,false,true},
		{"headsman",47465,false,1800,3200,12000,15000,100,3,10,80,0,0,true,false,true},
		{"headsman",47486,false,2500,4000,6000,8000,100,3,20,80,0,0,true,false,false},
		{"headsman",1680,true,4500,6500,9000,12000,100,3,20,80,0,0,true,false,false},
		{"headsman",5246,false,8000,12000,28000,36000,70,2,30,80,0,0,true,false,false},
		{"headsman",47471,false,0,0,5000,7000,100,2,40,80,0,20,true,false,false},
		{"veiled-knife",48638,false,900,1800,3500,5000,100,3,10,80,0,0,true,false,false},
		{"veiled-knife",48672,false,1800,3200,11000,14000,100,3,20,80,0,0,true,false,true},
		{"veiled-knife",1776,false,4500,6500,18000,24000,80,3,20,80,0,0,true,false,false},
		{"veiled-knife",26669,true,0,0,45000,45000,100,3,30,80,35,0,false,true,false},
		{"veiled-knife",8643,false,6500,9000,22000,28000,80,2,40,80,0,0,true,false,false},
		{"veiled-knife",48668,false,3500,5500,7000,9000,100,2,40,80,0,30,true,false,false},
		{"veiled-knife",26889,true,12000,16000,30000,38000,100,3,30,80,0,0,false,false,false},
		{"veiled-knife",57970,false,1200,2200,9000,12000,100,3,20,80,0,0,true,false,false},
		{"veiled-knife",3409,false,3000,4500,15000,19000,85,3,20,80,0,0,true,false,true},
		{"ashen-pact",47809,false,800,1800,3000,4500,100,3,10,80,0,0,false,false,false},
		{"ashen-pact",47813,false,1200,2400,15000,18000,100,3,10,80,0,0,false,false,true},
		{"ashen-pact",47864,false,2200,3800,18000,22000,100,3,20,80,0,0,false,false,true},
		{"ashen-pact",6215,false,7500,10000,24000,30000,100,3,20,80,0,0,false,false,false},
		{"ashen-pact",47811,false,3500,5500,12000,16000,90,2,30,80,0,0,false,false,true},
		{"ashen-pact",47857,false,6500,9000,14000,18000,85,2,40,80,45,0,false,false,false},
		{"ashen-pact",47860,false,0,0,28000,34000,100,2,50,80,30,0,false,true,false},
		{"wildclaw",49803,false,0,700,30000,30000,100,3,20,80,0,0,true,true,false},
		{"wildclaw",48574,false,1000,1800,9000,12000,100,3,10,80,0,0,true,false,true},
		{"wildclaw",48566,false,1800,2800,4500,6500,100,3,20,80,0,0,true,false,false},
		{"wildclaw",8983,false,0,800,18000,24000,90,3,20,80,45,0,true,false,false},
		{"wildclaw",48564,false,900,1600,5000,7000,100,3,20,80,45,0,true,false,false},
		{"wildclaw",22842,true,0,500,30000,30000,100,2,30,80,28,0,false,true,false},
		{"stormcaller",49281,true,0,500,30000,36000,100,3,10,80,0,0,false,false,true},
		{"stormcaller",17364,false,1000,1800,7000,9000,100,3,20,80,0,0,true,false,false},
		{"stormcaller",49233,false,1800,2800,12000,15000,100,3,20,80,0,0,false,false,true},
		{"stormcaller",49231,false,3500,5000,7000,10000,85,3,20,80,0,0,false,false,false},
		{"stormcaller",2484,true,2500,4000,30000,36000,100,3,20,80,0,0,false,false,false},
		{"stormcaller",58734,true,6500,8500,30000,36000,100,2,30,80,0,0,false,false,false},
		{"stormcaller",51533,true,9000,12000,60000,60000,100,2,40,80,40,0,false,true,false},
		{"dusk-confessor",48125,false,800,1600,16000,19000,100,3,10,80,0,0,false,false,true},
		{"dusk-confessor",48160,false,1800,3000,15000,18000,100,3,20,80,0,0,false,false,true},
		{"dusk-confessor",48156,false,2600,3800,3500,5000,100,3,20,80,0,0,false,false,false},
		{"dusk-confessor",48127,false,4200,6000,7000,9500,100,3,20,80,0,0,false,false,false},
		{"dusk-confessor",10890,true,7000,10000,24000,30000,100,3,30,80,0,0,false,false,false},
		{"dusk-confessor",47585,true,0,0,60000,60000,100,2,60,80,24,0,false,true,false},
		{"gravebound",49909,false,700,1400,12000,15000,100,3,10,80,0,0,false,false,true},
		{"gravebound",49921,false,1500,2400,12000,15000,100,3,10,80,0,0,true,false,true},
		{"gravebound",49938,false,3500,5000,18000,24000,100,3,20,80,0,0,false,false,false},
		{"gravebound",49576,false,6500,9000,22000,28000,100,3,20,80,0,0,false,false,false},
		{"gravebound",45524,false,2500,4000,12000,16000,100,3,20,80,0,0,false,false,true},
		{"gravebound",49924,false,5000,7000,8000,11000,100,3,30,80,55,0,true,false,false},
		{"gravebound",47528,false,3000,4500,10000,14000,100,3,30,80,0,0,false,false,false},
		{"gravebound",46584,true,8000,11000,60000,60000,100,2,55,80,35,0,false,true,false},
		{"farstrider",883,true,0,500,60000,60000,100,3,10,80,0,0,false,true,false},
		{"farstrider",53338,false,700,1300,30000,36000,100,3,10,80,0,0,false,false,true},
		{"farstrider",49001,false,1200,2200,15000,18000,100,3,10,80,0,0,false,false,true},
		{"farstrider",49052,false,1800,2800,3000,4500,100,3,20,80,0,0,false,false,false},
		{"farstrider",49050,false,3500,5000,9000,12000,100,3,30,80,0,0,false,false,false},
		{"farstrider",5116,false,4500,6500,12000,16000,90,3,20,80,0,0,false,false,true},
		{"farstrider",781,true,6500,9000,18000,24000,100,3,20,80,0,0,false,false,false},
		{"farstrider",49067,true,7000,10000,24000,30000,90,2,40,80,0,0,false,false,false},
		{"farstrider",19263,true,0,0,60000,60000,100,2,50,80,25,0,false,true,false},
	};
	return values;
}

std::vector<GuardLocatorSeed> const &GuardLocatorSeeds() {
	static std::vector<GuardLocatorSeed> const values = {
		{68,"corvin"},{1976,"corvin"},{5595,"brannoc"},{4262,"shalara"},
		{16733,"veylan"},{20674,"veylan"},{3296,"gorrak"},{3084,"tahu"},
		{5624,"morcant"},{36213,"morcant"},{16222,"vaelith"},
		{19687,"raleth"},{30659,"varyn"},{32691,"varyn"},
	};
	return values;
}

std::vector<FinalSiteDefinition> const &KnownFinalSites() {
	static std::vector<FinalSiteDefinition> const values = {
#include "HuntFinalSites.inc"
	};
	return values;
}

template <typename T, typename Predicate>
T const *Find(std::vector<T> const &values, Predicate predicate) {
	auto const it = std::find_if(values.begin(), values.end(), predicate);
	return it == values.end() ? nullptr : &*it;
}

HuntmasterDefinition const *FindHuntmaster(std::string const &key) {
	return Find(Huntmasters(), [&](auto const &value) { return value.Key == key; });
}
PreyDefinition const *FindPrey(std::string const &key) {
	if (auto const *standard = Find(StandardPrey(),
			[&](auto const &value) { return value.Key == key; }))
		return standard;
	return Find(ElitePrey(), [&](auto const &value) { return value.Key == key; });
}
FinalSiteDefinition const *FindFinalSite(std::string const &key) {
	return Find(KnownFinalSites(), [&](auto const &value) { return value.Key == key; });
}
FinalSiteDefinition const *FindZone(std::string const &zoneKey) {
	return Find(KnownFinalSites(), [&](auto const &value) { return value.ZoneKey == zoneKey; });
}
std::vector<FinalSiteDefinition const *>
FinalSitesForZone(std::string const &zoneKey) {
	std::vector<FinalSiteDefinition const *> matches;
	for (auto const &site : KnownFinalSites())
		if (site.ZoneKey == zoneKey)
			matches.push_back(&site);
	return matches;
}

bool IsZoneEligibleForScope(HuntSearchScope scope,
							 HuntmasterDefinition const &huntmaster,
							 FinalSiteDefinition const &zone) {
	if (scope == HuntSearchScope::World)
		return true;
	if (scope == HuntSearchScope::Continent)
		return huntmaster.Region == zone.Region;
	static std::unordered_map<std::string, std::vector<std::uint32_t>> const local = {
		{"corvin", {12, 40, 44, 10, 33, 41}},
		{"brannoc", {1, 38, 11, 3, 51, 46}},
		{"shalara", {141, 148, 331, 361, 618}},
		{"veylan", {3524, 3525, 148, 331}},
		{"gorrak", {14, 17, 406, 16, 15}},
		{"tahu", {215, 17, 400, 406, 405, 357}},
		{"morcant", {85, 130, 267, 36, 28, 139}},
		{"vaelith", {3430, 3433, 47, 139, 4080}},
		{"raleth", {3483, 3521, 3519, 3518, 3522, 3523, 3520}},
		{"varyn", {3537, 495, 65, 394, 66, 3711, 2817, 67, 210}},
	};
	auto const found = local.find(huntmaster.Key);
	return found != local.end() &&
		std::find(found->second.begin(), found->second.end(), zone.ZoneId) !=
			found->second.end();
}

std::vector<FinalSiteDefinition const *> EligibleZonesForAssignment(
		std::uint8_t level, HuntSearchScope scope,
		HuntmasterDefinition const &huntmaster) {
	std::vector<FinalSiteDefinition const *> eligible;
	std::unordered_set<std::string> seen;
	for (auto const &site : KnownFinalSites())
		if (level >= site.MinLevel && level <= site.MaxLevel &&
			IsZoneEligibleForScope(scope, huntmaster, site) &&
			seen.emplace(site.ZoneKey).second)
			eligible.push_back(&site);
	return eligible;
}
} // namespace native_hunts
