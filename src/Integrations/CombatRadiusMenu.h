#pragma once

#include "Core/CombatRadius.h"

#include <optional>

namespace scf::integrations::radius_menu
{
	struct SummonView
	{
		std::string name;
		core::EntityID id;
		float radius;
		float distanceToPlayer;
		core::EntityID target;
		float targetDistanceToPlayer;
		std::string package;
		std::size_t enemiesInRadius;
		const char* state;
	};

	struct Changes
	{
		std::optional<core::CombatRadiusSettings> settings;
		bool reload{};
		bool reset{};
	};

	void Configure(const core::CombatRadiusSettings& a_settings);
	bool Available();
	Changes Consume();
	void Publish(const std::vector<SummonView>& a_summons, std::size_t a_enemies);
}
