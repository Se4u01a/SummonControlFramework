#pragma once

#include "Core/EntityID.h"
#include "Core/StealthContext.h"
#include "Core/SummonModel.h"
#include "Core/Types.h"

#include <functional>
#include <vector>

namespace scf::ai
{
	struct TargetCandidate
	{
		core::EntityID id{ core::InvalidEntityID };
		core::Vec3 position{};

		bool isDead{ false };
		bool isDisabled{ false };
		bool isOwnerOrAlly{ false };

		bool isHostileToOwner{ false };
		bool isHostileToSummon{ false };
		bool isAttackingOwner{ false };
		bool isAttackingSummon{ false };

		float healthPercent{ 1.0f };
	};

	struct TargetSelectorConfig
	{
		float maxTargetRadius{ 2000.0f };
		float currentTargetStickyBonus{ 400.0f };
		float attackingOwnerBonus{ 1000.0f };
		float attackingSummonBonus{ 500.0f };
		float lowHealthBonus{ 200.0f };
	};

	class TargetSelector
	{
	public:
		using LogCallback = std::function<void(core::EntityID a_summonId, core::EntityID a_chosenTarget, const char* a_reason)>;

		TargetSelector() = default;
		explicit TargetSelector(LogCallback a_logCb) : _logCallback(std::move(a_logCb)) {}

		void SetLogCallback(LogCallback a_logCb) noexcept
		{
			_logCallback = std::move(a_logCb);
		}

		[[nodiscard]] core::EntityID SelectTarget(
			const core::SummonModel& a_summon,
			const std::vector<TargetCandidate>& a_candidates,
			core::EntityID a_explicitFocusTarget = core::InvalidEntityID,
			const TargetSelectorConfig& a_config = {},
			const core::StealthContext& a_stealth = {}) const;

	private:
		[[nodiscard]] bool IsCandidateValid(
			const core::SummonModel& a_summon,
			const TargetCandidate& a_candidate,
			const TargetSelectorConfig& a_config,
			const core::StealthContext& a_stealth = {}) const noexcept;

		[[nodiscard]] float ScoreCandidate(
			const core::SummonModel& a_summon,
			const TargetCandidate& a_candidate,
			const TargetSelectorConfig& a_config) const noexcept;

		LogCallback _logCallback{ nullptr };
	};
}
