#include "HuntCatalog.h"

#include <algorithm>

namespace native_hunts {
std::vector<HuntmasterDefinition> const &Huntmasters() {
	static std::vector<HuntmasterDefinition> const values = {
		{"corvin", "huntmaster-corvin", "huntmaster-corvin-spawn", "Huntmaster Corvin", "Stormwind City"},
		{"brannoc", "huntmaster-brannoc", "huntmaster-brannoc-spawn", "Huntmaster Brannoc", "Ironforge"},
		{"shalara", "huntmaster-shalara", "huntmaster-shalara-spawn", "Huntmistress Shalara", "Darnassus"},
		{"veylan", "huntmaster-veylan", "huntmaster-veylan-spawn", "Huntmaster Veylan", "The Exodar"},
		{"gorrak", "huntmaster-gorrak", "huntmaster-gorrak-spawn", "Huntmaster Gorrak", "Orgrimmar"},
		{"tahu", "huntmaster-tahu", "huntmaster-tahu-spawn", "Huntmaster Tahu", "Thunder Bluff"},
		{"morcant", "huntmaster-morcant", "huntmaster-morcant-spawn", "Huntmaster Morcant", "Undercity"},
		{"vaelith", "huntmaster-vaelith", "huntmaster-vaelith-spawn", "Huntmistress Vaelith", "Silvermoon City"},
		{"raleth", "huntmaster-raleth", "huntmaster-raleth-spawn", "Huntmaster Raleth", "Shattrath City"},
		{"varyn", "huntmaster-varyn", "huntmaster-varyn-spawn", "Huntmaster Varyn", "Dalaran"},
	};
	return values;
}

std::vector<PreyDefinition> const &StandardPrey() {
	static std::vector<PreyDefinition> const values = {
		{"ashfang", "prey-ashfang", "Ashfang", 6.0f},
		{"silkmaw", "prey-silkmaw", "Silkmaw", 6.0f},
		{"gorehide", "prey-gorehide", "Gorehide", 6.0f},
		{"whiteclaw", "prey-whiteclaw", "Whiteclaw", 6.0f},
		{"tidefang", "prey-tidefang", "Tidefang", 6.0f},
		{"stonegut", "prey-stonegut", "Stonegut", 6.0f},
		{"sootfang", "prey-sootfang", "Sootfang", 6.0f},
		{"nightfang", "prey-nightfang", "Nightfang", 6.0f},
		{"shadowclaw", "prey-shadowclaw", "Shadowclaw", 6.0f},
		{"dreadwing", "prey-dreadwing", "Dreadwing", 6.0f},
		{"venomtail", "prey-venomtail", "Venomtail", 6.0f},
		{"stormcoil", "prey-stormcoil", "Stormcoil", 6.0f},
		{"mirejaw", "prey-mirejaw", "Mirejaw", 6.0f},
		{"razortalon", "prey-razortalon", "Razortalon", 6.0f},
		{"cliffhowl", "prey-cliffhowl", "Cliffhowl", 6.0f},
		{"grimmaw", "prey-grimmaw", "Grimmaw", 6.0f},
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
	return Find(StandardPrey(), [&](auto const &value) { return value.Key == key; });
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
} // namespace native_hunts
