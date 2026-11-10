#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace scf::friendly_fire
{
	struct FriendlyFireSettings
	{
		// Master toggle
		bool enabled{ true };

		// Target selection
		bool protectPlayerOwnedSummons{ true };
		bool protectAllyOwnedSummons{ false };

		// Attack sources to filter
		bool filterPlayerAttacks{ true };
		bool filterPlayerAllies{ true };
		bool filterPlayerOwnedSummons{ true };
		bool filterAlliedSummons{ true };

		// Damage protection
		bool blockMeleeDamage{ true };
		bool blockArrowDamage{ true };
		bool blockPhysicalProjectileDamage{ true };
		bool blockMagicProjectileDamage{ true };
		bool blockConcentrationDamage{ true };
		bool blockAreaMagicDamage{ true };
		bool blockShoutDamage{ true };
		bool blockEnchantmentDamage{ true };

		// Attack pass-through
		bool meleePassThrough{ true };
		bool arrowPassThrough{ true };
		bool physicalProjectilePassThrough{ false };
		bool magicProjectilePassThrough{ false };

		// Side-effects suppression
		bool suppressFriendlyAggression{ true };
		bool suppressFriendlyKillmoves{ true };
		bool suppressFriendlyHitReactions{ false };
	};
}
