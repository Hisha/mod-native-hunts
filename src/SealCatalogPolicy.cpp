#include "SealCatalogPolicy.h"

namespace native_hunts {

bool MatchesSealCatalogRole(SealCatalogRole role,
		SealCatalogTraits const &traits) {
	switch (role) {
	case SealCatalogRole::StrengthMelee:
		return traits.Strength || traits.AttackPower;
	case SealCatalogRole::AgilityMelee:
		return traits.Agility || traits.AttackPower;
	case SealCatalogRole::HunterRanged:
		return traits.Agility || traits.AttackPower ||
			traits.RangedAttackPower;
	case SealCatalogRole::SpellDamage:
		return traits.SpellPower || traits.Intellect;
	case SealCatalogRole::Healer:
		return traits.SpellPower || traits.Intellect ||
			traits.ManaRegeneration || traits.Spirit;
	case SealCatalogRole::Tank:
		// Stamina alone is universal at endgame and cannot identify tank gear.
		return traits.Defense || traits.Dodge || traits.Parry ||
			traits.BlockRating || traits.BlockValue;
	case SealCatalogRole::Generic:
		return true;
	}
	return false;
}

} // namespace native_hunts
