#pragma once

namespace native_hunts {

enum class SealCatalogRole {
	Generic,
	StrengthMelee,
	AgilityMelee,
	HunterRanged,
	SpellDamage,
	Healer,
	Tank
};

struct SealCatalogTraits {
	// Relic benefits in 3.3.5 are commonly encoded as equip spells instead of
	// explicit item-template stats. Inventory/subclass compatibility is checked
	// separately before a relic can enter a class/spec catalog.
	bool Relic = false;
	bool Strength = false;
	bool Agility = false;
	bool AttackPower = false;
	bool RangedAttackPower = false;
	bool SpellPower = false;
	bool Intellect = false;
	bool ManaRegeneration = false;
	bool Spirit = false;
	bool Defense = false;
	bool Dodge = false;
	bool Parry = false;
	bool BlockRating = false;
	bool BlockValue = false;
};

// A Seal catalog is an eligibility boundary, not merely a ranking hint. Apart
// from class-restricted relics, only explicit item-template stats are used;
// proc/spell text is deliberately not guessed at, so other unclassifiable
// items stay out of every specialized list.
bool MatchesSealCatalogRole(SealCatalogRole role,
	SealCatalogTraits const &traits);

} // namespace native_hunts
