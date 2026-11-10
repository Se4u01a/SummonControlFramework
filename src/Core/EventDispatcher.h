#pragma once

#include "Core/EntityID.h"
#include "Core/SummonRegistry.h"

#include <cstdint>
#include <functional>
#include <string>

namespace scf::core
{
	struct ActiveEffectEvent
	{
		EntityID casterId{ InvalidEntityID };
		EntityID targetId{ InvalidEntityID };
		std::uint16_t activeEffectUniqueId{ 0 };
		bool isApplied{ false };
	};

	struct DeathEvent
	{
		EntityID dyingId{ InvalidEntityID };
		EntityID killerId{ InvalidEntityID };
		bool isDead{ false };
	};

	struct CellAttachDetachEvent
	{
		EntityID entityId{ InvalidEntityID };
		bool isAttached{ false };
	};

	class EventDispatcher
	{
	public:
		using LogCallback = std::function<void(const char* a_message)>;
		using DisplaceCallback = std::function<void(EntityID a_displacedId)>;

		EventDispatcher() = default;
		explicit EventDispatcher(LogCallback a_logCb, DisplaceCallback a_displaceCb = nullptr) :
			_logCallback(std::move(a_logCb)),
			_displaceCallback(std::move(a_displaceCb))
		{}

		void SetLogCallback(LogCallback a_logCb) noexcept
		{
			_logCallback = std::move(a_logCb);
		}

		void SetDisplaceCallback(DisplaceCallback a_displaceCb) noexcept
		{
			_displaceCallback = std::move(a_displaceCb);
		}

		bool HandleActiveEffect(
			const ActiveEffectEvent& a_event,
			SummonRegistry& a_registry,
			float a_currentTimestamp = 0.0f);

		bool HandleDeath(
			const DeathEvent& a_event,
			SummonRegistry& a_registry);

		bool HandleCellAttachDetach(
			const CellAttachDetachEvent& a_event,
			SummonRegistry& a_registry);

	private:
		LogCallback _logCallback{ nullptr };
		DisplaceCallback _displaceCallback{ nullptr };
	};
}
