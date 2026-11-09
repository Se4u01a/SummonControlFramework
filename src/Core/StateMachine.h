#pragma once

#include "Core/DecisionContext.h"
#include "Core/SummonModel.h"

#include <functional>

namespace scf::core
{
	class StateMachine
	{
	public:
		using LogCallback = std::function<void(EntityID a_id, SummonState a_from, SummonState a_to, const char* a_reason)>;

		StateMachine() = default;
		explicit StateMachine(LogCallback a_logCb) : _logCallback(std::move(a_logCb)) {}

		void SetLogCallback(LogCallback a_logCb) noexcept
		{
			_logCallback = std::move(a_logCb);
		}

		void TransitionTo(SummonModel& a_model, SummonState a_newState, const char* a_reason);

		void Update(SummonModel& a_model, const DecisionContext& a_ctx, float a_dt);

	private:
		[[nodiscard]] bool CanEngageCombat(const SummonModel& a_model, const DecisionContext& a_ctx) const noexcept;

		LogCallback _logCallback{ nullptr };
	};
}
