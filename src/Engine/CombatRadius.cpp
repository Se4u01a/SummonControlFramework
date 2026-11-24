#include "PCH.h"
#include "Engine/CombatRadius.h"
#include "Core/PassThrough.h"
#include "Engine/ActorSpace.h"
#include "Engine/CombatGate.h"
#include "Integrations/CombatRadiusMenu.h"

#include <format>
#include <fstream>
#include <optional>
#include <unordered_map>

namespace scf::engine::radius
{
	namespace
	{
		struct Rule
		{
			RE::BGSKeyword* keyword;
			float radius;
		};

		struct Attempt
		{
			RE::ActorHandle summon;
			core::EntityID target{ core::InvalidEntityID };
			std::optional<float> startedAt;
			bool accepted{};
			std::string_view status;
		};

		core::CombatRadiusSettings settings;
		bool hasIni{};
		std::vector<Rule> rules;
		std::unordered_map<core::EntityID, Attempt> attempts;
		std::vector<const keywords::WorldActor*> enemies;
		std::vector<core::RadiusEnemy> candidates;
		std::vector<integrations::radius_menu::SummonView> rows;
		core::Vec3 playerPosition;

		core::Vec3 Position(const RE::Actor& a_actor)
		{
			const auto& pos = a_actor.GetPosition();
			return { pos.x, pos.y, pos.z };
		}

		void ResolveKeywords()
		{
			rules.clear();
			if (auto* data = RE::TESDataHandler::GetSingleton()) {
				for (auto* keyword : data->GetFormArray<RE::BGSKeyword>()) {
					if (!keyword || !keyword->GetFormEditorID()) {
						continue;
					}
					for (const auto& entry : settings.keywords) {
						if (entry.editorId == keyword->GetFormEditorID()) {
							rules.push_back({ keyword, entry.radius });
						}
					}
				}
			}
		}

		float Distance(const core::Vec3& a_lhs, const core::Vec3& a_rhs)
		{
			return std::hypot(a_lhs.x - a_rhs.x, a_lhs.y - a_rhs.y);
		}
	}

	void Load()
	{
		std::ifstream ini("Data/SKSE/Plugins/SummonCombatRadius.ini");
		hasIni = ini.is_open();
		settings = core::ReadCombatRadiusSettings(ini);
		ResolveKeywords();
		integrations::radius_menu::Configure(settings);
		logger::info("[CombatRadius] Settings from {}, enabled={}, combat={}, resolved keywords={}",
			hasIni ? "SummonCombatRadius.ini" : "defaults", settings.enabled, settings.combatEnabled, rules.size());
	}

	void ConsumeMenu()
	{
		if (!integrations::radius_menu::Available()) {
			return;
		}
		auto changes = integrations::radius_menu::Consume();
		if (changes.reload) {
			Load();
		} else if (changes.settings) {
			settings = std::move(*changes.settings);
		}
		if (changes.reset) {
			Reset();
			combat::ClearRadiusPolicies();
		}
	}

	void BeginTick()
	{
		enemies.clear();
		rows.clear();
	}

	float Resolve(RE::Actor& a_summon, const RE::EffectSetting* a_source)
	{
		float radius = 0.0f;
		for (const auto& entry : rules) {
			const auto* race = a_summon.GetRace();
			if ((radius == 0.0f || entry.radius < radius) &&
			    (a_summon.HasKeyword(entry.keyword) || (race && race->HasKeyword(entry.keyword)) ||
			     a_summon.HasMagicEffectWithKeyword(entry.keyword) ||
			     (a_source && a_source->HasKeyword(entry.keyword)))) {
				radius = entry.radius;
			}
		}
		return radius;
	}

	bool UsesCollisionBypass(bool a_hasTaggedSummon)
	{
		return settings.enabled && settings.collisionEnabled && (hasIni || a_hasTaggedSummon);
	}

	const core::CombatRadiusSettings& Settings()
	{
		return settings;
	}

	void PublishEnemies(RE::Actor& a_player, std::span<const keywords::WorldActor> a_world)
	{
		playerPosition = Position(a_player);
		for (const auto& actor : a_world) {
			if (actor.actor != &a_player && actor.leader != a_player.GetFormID() &&
			    !(actor.flags & core::kUntargetable) && ShareLoadedSpace(&a_player, actor.actor) &&
			    actor.actor->IsHostileToActor(&a_player)) {
				enemies.push_back(&actor);
			}
		}
	}

	core::CombatPolicy Update(
		RE::Actor& a_summon,
		const core::SummonModel& a_model,
		float a_radius,
		float a_now,
		const combat::SummonCombatDirectives& a_directives)
	{
		const auto id = a_summon.GetFormID();
		const auto summonHandle = a_summon.GetHandle();
		const bool inCombat = a_summon.IsInCombat();
		const auto current = inCombat ? a_summon.GetActorRuntimeData().currentCombatTarget.get() : RE::NiPointer<RE::Actor>{};
		const auto currentId = current ? current->GetFormID() : core::InvalidEntityID;
		const auto position = Position(a_summon);
		const char* status = core::ToString(a_model.order);
		core::CombatPolicy policy = core::BuildCombatPolicy(a_model, a_model.currentTargetId != core::InvalidEntityID);
		core::EntityID selected = currentId;

		candidates.clear();
		for (const auto* enemy : enemies) {
			if (enemy->id != id && ShareLoadedSpace(&a_summon, enemy->actor)) {
				candidates.push_back({ enemy->id, Position(*enemy->actor) });
			}
		}

		if (core::UsesCombatRadius(settings, a_model, a_radius)) {
			auto& attempt = attempts[id];
			if (attempt.summon != summonHandle) {
				attempt = {};
				attempt.summon = summonHandle;
			}
			const float cooldown = static_cast<float>(settings.retargetCooldownMs) / 1000.0f;
			const bool ready = !attempt.startedAt || a_now - *attempt.startedAt >= cooldown;
			const auto decision = core::DecideCombatRadius(playerPosition, position, a_radius, settings.hysteresis,
				candidates, currentId, ready);
			selected = decision.target;

			switch (decision.action) {
			case core::RadiusAction::Hold:
				policy = core::BuildRadiusPolicy(selected);
				status = "Combat";
				break;
			case core::RadiusAction::Wait:
				// An accepted StartCombat can precede acquisition of the controller's target.
				if (inCombat && !current && attempt.accepted && attempt.target == selected) {
					policy = core::BuildRadiusPolicy(selected);
					status = "Acquiring";
					break;
				}
				[[fallthrough]];
			case core::RadiusAction::Park:
				policy = core::BuildRadiusPolicy(core::InvalidEntityID);
				status = decision.action == core::RadiusAction::Park ? "Parked" : "Cooldown";
				break;
			case core::RadiusAction::Start:
				policy = core::BuildRadiusPolicy(selected);
				status = "Acquiring";
				break;
			}

			if (inCombat && (decision.action == core::RadiusAction::Start || !policy.allowCombat)) {
				combat::UpdateForSummon(&a_summon, core::BuildRadiusPolicy(core::InvalidEntityID), a_directives);
				if (!policy.allowCombat && settings.verbose) {
					logger::info("[CombatRadius] {:08X} ended fight against {:08X} ({}, R {:.0f}, owner distance {:.0f})",
						id, currentId, status, a_radius, current ? Distance(playerPosition, Position(*current)) : -1.0f);
				}
			}
			combat::UpdateForSummon(&a_summon, policy, a_directives);
			if (decision.action == core::RadiusAction::Start) {
				auto* target = RE::TESForm::LookupByID<RE::Actor>(selected);
				const bool started = target && a_summon.GetActorRuntimeData().currentProcess &&
					!combat::ShouldDenyStartCombat(id, target) && a_summon.StartCombat(target);
				attempt.target = selected;
				attempt.startedAt = a_now;
				attempt.accepted = started;
				logger::info("[CombatRadius] {:08X} -> {:08X} R {:.0f} owner-to-target {:.0f} summon-to-target {:.0f} StartCombat={}",
					id, selected, a_radius, target ? Distance(playerPosition, Position(*target)) : -1.0f,
					target ? Distance(position, Position(*target)) : -1.0f, started);
				if (!started) {
					policy = core::BuildRadiusPolicy(core::InvalidEntityID);
					combat::UpdateForSummon(&a_summon, policy, a_directives);
					status = "Cooldown";
				}
			}
			if (attempt.status != status) {
				logger::info("[CombatRadius] {:08X} {} -> {} (R {:.0f}, current {:08X}, selected {:08X})",
					id, attempt.status.empty() ? "Follow" : attempt.status, status, a_radius, currentId, selected);
				attempt.status = status;
			}
		} else {
			attempts.erase(id);
			combat::UpdateForSummon(&a_summon, policy, a_directives);
		}

		if (integrations::radius_menu::Available()) {
			const auto* target = RE::TESForm::LookupByID<RE::Actor>(selected);
			const auto* process = a_summon.GetActorRuntimeData().currentProcess;
			const auto* package = process ? process->GetRunningPackage() : nullptr;
			const auto count = std::ranges::count_if(candidates, [&](const auto& enemy) {
				return Distance(playerPosition, enemy.position) <= a_radius;
			});
			rows.push_back({ a_summon.GetName(), id, a_radius, Distance(playerPosition, position), selected,
				target ? Distance(playerPosition, Position(*target)) : 0.0f,
				package ? std::format("{} [{:08X}]", package->GetFormEditorID() ? package->GetFormEditorID() : "", package->GetFormID()) : "-",
				static_cast<std::size_t>(count), status });
		}
		return policy;
	}

	void FinishTick(const std::unordered_map<core::EntityID, float>& a_tagged)
	{
		std::erase_if(attempts, [&](const auto& entry) { return !a_tagged.contains(entry.first); });
		if (integrations::radius_menu::Available()) {
			integrations::radius_menu::Publish(rows, enemies.size());
		}
		enemies.clear();
	}

	void Reset()
	{
		attempts.clear();
		BeginTick();
		if (integrations::radius_menu::Available()) {
			integrations::radius_menu::Publish(rows, 0);
		}
	}
}
