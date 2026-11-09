#pragma once

#include "AI/TargetSelector.h"
#include "Core/EntityID.h"
#include "Core/SummonModel.h"
#include "Core/Types.h"

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <vector>

namespace scf::ai
{
	enum class CombatRole : std::uint8_t
	{
		Melee,
		Ranged,
		Support
	};

	constexpr const char* ToString(CombatRole a_role) noexcept
	{
		switch (a_role) {
		case CombatRole::Melee:   return "Melee";
		case CombatRole::Ranged:  return "Ranged";
		case CombatRole::Support: return "Support";
		default:                  return "Unknown";
		}
	}

	struct SquadMember
	{
		core::SummonModel* model{ nullptr };
		CombatRole role{ CombatRole::Melee };
		core::Vec3 formationOffset{};
		core::EntityID assignedTarget{ core::InvalidEntityID };
	};

	struct SquadConfig
	{
		std::size_t maxMeleeSlotsPerTarget{ 2 };
		std::size_t maxRangedSlotsPerTarget{ 3 };
		float formationSpreadRadius{ 160.0f };
		float maxSquadEngageRadius{ 2000.0f };
	};

	class SquadCoordinator
	{
	public:
		using LogCallback = std::function<void(const char* a_message)>;

		SquadCoordinator() = default;
		explicit SquadCoordinator(LogCallback a_logCb) : _logCallback(std::move(a_logCb)) {}

		void SetLogCallback(LogCallback a_logCb) noexcept
		{
			_logCallback = std::move(a_logCb);
		}

		void CalculateFormationOffsets(
			std::vector<SquadMember>& a_members,
			float a_ownerYawDeg,
			float a_spreadRadius) const;

		void CoordinateSquad(
			std::vector<SquadMember>& a_members,
			const std::vector<TargetCandidate>& a_candidates,
			const core::Vec3& a_ownerPos,
			float a_ownerYawDeg,
			const TargetSelector& a_targetSelector,
			const SquadConfig& a_config = {}) const;

	private:
		LogCallback _logCallback{ nullptr };
	};
}
