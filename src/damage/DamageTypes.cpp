#include "damage/DamageTypes.h"

namespace Damage
{
	const char* ToString(DamageKind a_kind)
	{
		switch (a_kind) {
		case DamageKind::kFire:
			return "fire";
		case DamageKind::kFrost:
			return "frost";
		case DamageKind::kShock:
			return "shock";
		case DamageKind::kPoison:
			return "poison";
		case DamageKind::kMagic:
			return "magic";
		case DamageKind::kPhysical:
		default:
			return "physical";
		}
	}
}
