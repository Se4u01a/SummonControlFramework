#include "Core/PassThrough.h"

namespace scf::core
{
	bool IsPassThroughLayerPair(std::uint32_t a_layerA, std::uint32_t a_layerB) noexcept
	{
		constexpr std::uint32_t kBiped = 8;
		constexpr std::uint32_t kCharController = 30;
		constexpr std::uint32_t kBipedNoCC = 33;
		const auto isBody = [](std::uint32_t a_layer) {
			return a_layer == kCharController || a_layer == kBiped || a_layer == kBipedNoCC;
		};
		return (a_layerA == kCharController && isBody(a_layerB)) || (a_layerB == kCharController && isBody(a_layerA));
	}

	bool ShouldPassThrough(const PassThroughParty& a_lhs, const PassThroughParty& a_rhs, const CollisionSettings& a_settings) noexcept
	{
		if (!a_settings.enableCollisionBypass) {
			return false;
		}
		if (a_lhs.id == InvalidEntityID || a_rhs.id == InvalidEntityID || a_lhs.id == a_rhs.id) {
			return false;
		}

		if (a_lhs.IsSummon() && a_rhs.IsSummon()) {
			return a_settings.enableSummonToSummonBypass && a_lhs.ownerId == a_rhs.ownerId;
		}
		if (a_lhs.IsSummon()) {
			return a_lhs.ownerId == a_rhs.id;
		}
		if (a_rhs.IsSummon()) {
			return a_rhs.ownerId == a_lhs.id;
		}
		return false;
	}
}
