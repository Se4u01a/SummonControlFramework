#include "Core/PassThrough.h"

#include <algorithm>

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

	std::vector<EntityID> RowsToRefilter(const std::vector<PassThroughRow>& a_previous, const CollisionSettings& a_previousSettings,
		const std::vector<PassThroughRow>& a_current, const CollisionSettings& a_currentSettings)
	{
		const bool settingsChanged = a_previousSettings.enableCollisionBypass != a_currentSettings.enableCollisionBypass ||
		                             a_previousSettings.enableSummonToSummonBypass != a_currentSettings.enableSummonToSummonBypass;

		const auto find = [](const std::vector<PassThroughRow>& a_rows, EntityID a_id) -> const PassThroughRow* {
			for (const auto& row : a_rows) {
				if (row.id == a_id) {
					return &row;
				}
			}
			return nullptr;
		};

		std::vector<EntityID> result;
		const auto add = [&](EntityID a_id) {
			if (a_id != InvalidEntityID && std::find(result.begin(), result.end(), a_id) == result.end()) {
				result.push_back(a_id);
			}
		};

		for (const auto& row : a_current) {
			const auto* before = find(a_previous, row.id);
			if (settingsChanged || (row.systemGroup != 0 && (!before || before->systemGroup != row.systemGroup))) {
				add(row.id);
			}
		}
		for (const auto& row : a_previous) {
			if (settingsChanged || !find(a_current, row.id)) {
				add(row.id);
			}
		}
		return result;
	}
}
