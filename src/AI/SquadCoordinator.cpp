#include "AI/SquadCoordinator.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace scf::ai
{
	namespace
	{
		constexpr float DegToRad(float a_deg) noexcept
		{
			return a_deg * (static_cast<float>(std::numbers::pi) / 180.0f);
		}

		float GetFormationRelativeAngleDeg(std::size_t a_index, std::size_t a_total) noexcept
		{
			if (a_total <= 1) {
				return -140.0f; // Left-rear
			}

			switch (a_total) {
			case 2:
				return (a_index == 0) ? -130.0f : 130.0f;

			case 3:
				if (a_index == 0) return -120.0f;
				if (a_index == 1) return 120.0f;
				return 180.0f; // Straight behind

			case 4:
				if (a_index == 0) return -70.0f;   // Left flank
				if (a_index == 1) return 70.0f;    // Right flank
				if (a_index == 2) return -145.0f;  // Left rear
				return 145.0f;                     // Right rear

			default:
				// Equispaced around rear arc (from 90 to 270 degrees)
				const float step = 180.0f / static_cast<float>(a_total + 1);
				return 90.0f + step * static_cast<float>(a_index + 1);
			}
		}
	}

	void SquadCoordinator::CalculateFormationOffsets(
		std::vector<SquadMember>& a_members,
		float a_ownerYawDeg,
		float a_spreadRadius) const
	{
		const std::size_t count = a_members.size();

		for (std::size_t i = 0; i < count; ++i) {
			const float relAngle = GetFormationRelativeAngleDeg(i, count);
			const float totalAngleRad = DegToRad(a_ownerYawDeg + relAngle);

			// Standard Skyrim 2D orientation: sin is X (East), cos is Y (North)
			a_members[i].formationOffset.x = std::sin(totalAngleRad) * a_spreadRadius;
			a_members[i].formationOffset.y = std::cos(totalAngleRad) * a_spreadRadius;
			a_members[i].formationOffset.z = 0.0f;
		}
	}

	void SquadCoordinator::CoordinateSquad(
		std::vector<SquadMember>& a_members,
		const std::vector<TargetCandidate>& a_candidates,
		const core::Vec3& a_ownerPos,
		float a_ownerYawDeg,
		const TargetSelector& a_targetSelector,
		const SquadConfig& a_config) const
	{
		// 1. Calculate formation offsets for all members
		CalculateFormationOffsets(a_members, a_ownerYawDeg, a_config.formationSpreadRadius);

		// 2. Track claimed attack slots per candidate target
		std::unordered_map<core::EntityID, std::size_t> meleeSlotsClaimed;
		std::unordered_map<core::EntityID, std::size_t> rangedSlotsClaimed;

		char logBuffer[256];

		for (auto& member : a_members) {
			if (!member.model) {
				continue;
			}

			// Filter candidates that still have open attack slots for this member's role
			std::vector<TargetCandidate> eligibleCandidates;
			eligibleCandidates.reserve(a_candidates.size());

			for (const auto& candidate : a_candidates) {
				const float distToOwner = a_ownerPos.DistanceTo(candidate.position);
				if (distToOwner > a_config.maxSquadEngageRadius) {
					continue;
				}

				if (member.role == CombatRole::Melee) {
					const std::size_t currentMelee = meleeSlotsClaimed[candidate.id];
					if (currentMelee < a_config.maxMeleeSlotsPerTarget) {
						eligibleCandidates.push_back(candidate);
					}
				} else {
					const std::size_t currentRanged = rangedSlotsClaimed[candidate.id];
					if (currentRanged < a_config.maxRangedSlotsPerTarget) {
						eligibleCandidates.push_back(candidate);
					}
				}
			}

			// Select target for this member using filtered candidates
			const core::EntityID chosenTarget = a_targetSelector.SelectTarget(
				*member.model,
				eligibleCandidates,
				member.assignedTarget);

			if (chosenTarget != core::InvalidEntityID) {
				if (member.role == CombatRole::Melee) {
					++meleeSlotsClaimed[chosenTarget];
					if (_logCallback) {
						std::snprintf(logBuffer, sizeof(logBuffer),
							"[Squad] Summon %u (%s) assigned to Target %u (Melee slot %zu/%zu)",
							member.model->id,
							ToString(member.role),
							chosenTarget,
							meleeSlotsClaimed[chosenTarget],
							a_config.maxMeleeSlotsPerTarget);
						_logCallback(logBuffer);
					}
				} else {
					++rangedSlotsClaimed[chosenTarget];
					if (_logCallback) {
						std::snprintf(logBuffer, sizeof(logBuffer),
							"[Squad] Summon %u (%s) assigned to Target %u (Ranged slot %zu/%zu)",
							member.model->id,
							ToString(member.role),
							chosenTarget,
							rangedSlotsClaimed[chosenTarget],
							a_config.maxRangedSlotsPerTarget);
						_logCallback(logBuffer);
					}
				}

				member.assignedTarget = chosenTarget;
				member.model->currentTargetId = chosenTarget;
			} else {
				// No slots open or no enemies eligible
				member.assignedTarget = core::InvalidEntityID;
				member.model->currentTargetId = core::InvalidEntityID;

				if (_logCallback) {
					std::snprintf(logBuffer, sizeof(logBuffer),
						"[Squad] Summon %u (%s) holding in formation (no open attack slots or no valid enemies)",
						member.model->id,
						ToString(member.role));
					_logCallback(logBuffer);
				}
			}
		}
	}
}
