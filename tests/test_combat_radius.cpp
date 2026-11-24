#include "check.h"
#include "Core/CombatRadius.h"
#include "Core/TargetPin.h"

#include <sstream>

using namespace scf::core;

TEST(combat_radius_uses_entry_and_retention_boundaries)
{
	const RadiusEnemy candidates[]{ { 42, { 500, 0, 999 } }, { 72, { 575, 0, -999 } } };
	CHECK(DecideCombatRadius({}, {}, 500, 75, candidates, 0, true).target == 42);
	CHECK(DecideCombatRadius({}, {}, 500, 75, candidates, 72, true).action == RadiusAction::Hold);
	const RadiusEnemy out[]{ { 72, { 576, 0, 0 } }, { 98, { 501, 0, 0 } } };
	CHECK(DecideCombatRadius({}, {}, 500, 75, out, 72, true).action == RadiusAction::Park);
}

TEST(combat_radius_picks_nearest_to_summon_and_waits_after_invalidating_old_target)
{
	const RadiusEnemy candidates[]{ { 42, { 250, 0, 0 } }, { 41, { 0, 250, 0 } },
		{ 50, { 50, 50, 0 } }, { 1, { 576, 0, 0 } } };
	const Vec3 summon{ 250, 250, 0 };
	const auto decision = DecideCombatRadius({}, summon, 500, 75, candidates, 1, true);
	CHECK(decision.action == RadiusAction::Start);
	CHECK(decision.target == 41);
	CHECK(DecideCombatRadius({}, summon, 500, 75, candidates, 1, false).action == RadiusAction::Wait);
	CHECK(DecideCombatRadius({}, summon, 500, 75, candidates, 50, false).target == 50);
}

TEST(combat_radius_preserves_orders_and_parking_does_not_enable_attack)
{
	CombatRadiusSettings settings;
	SummonModel model;
	CHECK(!UsesCombatRadius(settings, model, 0.0f));
	CHECK(UsesCombatRadius(settings, model, 500.0f));
	for (const auto order : { SummonOrder::Attack, SummonOrder::Return, SummonOrder::MoveTo, SummonOrder::WaitGuard }) {
		model.order = order;
		CHECK(!UsesCombatRadius(settings, model, 500.0f));
	}
	model.order = SummonOrder::Follow;
	model.aggression = AggressionMode::Passive;
	CHECK(!UsesCombatRadius(settings, model, 500.0f));
	model.aggression = AggressionMode::Defensive;
	settings.enabled = false;
	CHECK(!UsesCombatRadius(settings, model, 500.0f));

	const auto parked = BuildRadiusPolicy(0);
	CHECK(parked.radiusManaged);
	CHECK(!parked.allowCombat);
	CHECK(parked.disableAttacks);
	const auto engaged = BuildRadiusPolicy(42);
	CHECK(engaged.radiusManaged);
	CHECK(engaged.allowCombat);
	CHECK(engaged.restrictToTarget);
	CHECK(engaged.target == 42);
	CHECK(!BuildCombatPolicy(model, false).radiusManaged);
}

TEST(combat_radius_selector_never_returns_an_out_of_zone_engine_target)
{
	CHECK(ChooseRadiusSelectorAnswer(9, 42, true, PinMembership::Held) == 42);
	CHECK(ChooseRadiusSelectorAnswer(0, 42, true, PinMembership::Held) == 0);
	CHECK(ChooseRadiusSelectorAnswer(9, 42, true, PinMembership::Lost) == 0);
	CHECK(ChooseRadiusSelectorAnswer(9, 42, false, PinMembership::Held) == 0);
}

TEST(combat_radius_reads_legacy_ini_and_replaces_keyword_defaults)
{
	std::istringstream input{
		"\xEF\xBB\xBF[Main]\r\nenabled=no\r\ntickMs=350\r\n[Keywords]\r\nCustom=600 ; comment\r\n"
		"Custom=400\r\nBad=nan\r\n[Combat]\r\nhysteresis=-1\r\nretargetCooldownMs=90000\r\n"
		"[Approach]\r\nenabled=false\r\nringMin=600\r\nringMax=300\r\n"
		"[Collision]\r\nenabled=off\r\n[Menu]\r\nenabled=false\r\n" };
	const auto settings = ReadCombatRadiusSettings(input);
	CHECK(!settings.enabled);
	CHECK(settings.tickMs == 350);
	CHECK(settings.keywords.size() == 1);
	CHECK(settings.keywords.front().editorId == "Custom");
	CHECK(settings.keywords.front().radius == 600);
	CHECK(settings.hysteresis == 0);
	CHECK(settings.retargetCooldownMs == 60000);
	CHECK(!settings.approachEnabled);
	CHECK(settings.ringMin == 300);
	CHECK(settings.ringMax == 600);
	CHECK(!settings.collisionEnabled);
	CHECK(!settings.menuEnabled);
	std::istringstream defaults;
	CHECK(ReadCombatRadiusSettings(defaults).keywords.size() == 3);
	std::istringstream emptyKeywords{ "[Keywords]\n" };
	CHECK(ReadCombatRadiusSettings(emptyKeywords).keywords.empty());
}
