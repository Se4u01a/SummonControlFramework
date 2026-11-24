#pragma once

#include "Core/CombatPolicy.h"

#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace scf::core
{
	struct RadiusKeyword
	{
		std::string editorId;
		float radius;
	};

	struct CombatRadiusSettings
	{
		bool enabled{ true };
		bool verbose{ false };
		bool combatEnabled{ true };
		bool collisionEnabled{ true };
		bool menuEnabled{ true };
		float hysteresis{ 75.0f };
		int retargetCooldownMs{ 2000 };
		int tickMs{ 500 };
		bool approachEnabled{ true };
		std::string approachPackage{ "aaMZpkg_FollowPlayer" };
		float ringMin{ 300.0f };
		float ringMax{ 450.0f };
		std::vector<RadiusKeyword> keywords{
			{ "aaMZkw_SummonCombatArea200", 200.0f },
			{ "aaMZkw_SummonCombatArea500", 500.0f },
			{ "aaMZkw_SummonCombatArea1000", 1000.0f }
		};
	};

	CombatRadiusSettings ReadCombatRadiusSettings(std::istream& a_input);
	bool UsesCombatRadius(const CombatRadiusSettings& a_settings, const SummonModel& a_model, float a_radius) noexcept;

	struct RadiusEnemy
	{
		EntityID id;
		Vec3 position;
	};

	enum class RadiusAction
	{
		Hold,
		Start,
		Wait,
		Park
	};

	struct RadiusDecision
	{
		RadiusAction action;
		EntityID target{ InvalidEntityID };
	};

	RadiusDecision DecideCombatRadius(
		const Vec3& a_player, const Vec3& a_summon, float a_radius, float a_hysteresis,
		std::span<const RadiusEnemy> a_candidates, EntityID a_current, bool a_ready) noexcept;

	CombatPolicy BuildRadiusPolicy(EntityID a_target) noexcept;
}
