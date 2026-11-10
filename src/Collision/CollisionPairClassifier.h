#pragma once

#include "Core/EntityID.h"
#include "Core/Settings.h"
#include "Core/SummonRegistry.h"

#include <cstdint>
#include <functional>
#include <string>

namespace scf::collision
{
	enum class CollisionResult : std::uint8_t
	{
		Default,     // Defer to engine / other filters
		PassThrough, // Disable character physical body blocking
		Collide      // Maintain physical collision
	};

	constexpr const char* ToString(CollisionResult a_result) noexcept
	{
		switch (a_result) {
		case CollisionResult::Default:     return "Default";
		case CollisionResult::PassThrough: return "PassThrough";
		case CollisionResult::Collide:     return "Collide";
		default:                           return "Unknown";
		}
	}

	class CollisionPairClassifier
	{
	public:
		using LogCallback = std::function<void(const char* a_message)>;

		CollisionPairClassifier() = default;
		explicit CollisionPairClassifier(LogCallback a_logCb) : _logCallback(std::move(a_logCb)) {}

		void SetLogCallback(LogCallback a_logCb) noexcept
		{
			_logCallback = std::move(a_logCb);
		}

		[[nodiscard]] CollisionResult ClassifyPair(
			core::EntityID a_entityA,
			core::EntityID a_entityB,
			const core::SummonRegistry& a_registry,
			const core::CollisionSettings& a_settings,
			core::EntityID a_playerEntityId = 0x14) const noexcept;

	private:
		LogCallback _logCallback{ nullptr };
	};
}
