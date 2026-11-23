#include "PCH.h"
#include "Engine/AttackTrace.h"
#include "Core/SummonRegistry.h"
#include "Engine/ActorSpace.h"

#include <mutex>
#include <numbers>
#include <optional>
#include <unordered_map>

namespace scf::engine::attack_trace
{
	namespace
	{
		using Clock = core::AttackTraceClock;
		using Time = Clock::time_point;
		using namespace std::chrono_literals;
		constexpr auto kTimeout = 60s;

		struct Event
		{
			Time time;
			core::EntityID source;
		};

		struct Trace
		{
			core::AttackTraceOrigin origin;
			RE::ActorHandle summon;
			RE::ActorHandle target;
			core::EntityID focus;
			Time dispatched;
			Time logged;
			core::AttackTracePath path;
			core::AttackTraceWeapon weapon;
			std::optional<Event> hit;
			std::optional<Event> cast;
			std::optional<Time> firstAttack;
			float lastLoggedDistance{};
			float lastLoggedForward{};
		};

		// Hit and spell callbacks may arrive outside the update thread. Only the
		// update thread reads live positions and combat state.
		std::mutex mutex;
		std::unordered_map<core::EntityID, Trace> traces;

		float Seconds(Time a_from, Time a_to)
		{
			return std::chrono::duration<float>(a_to - a_from).count();
		}

		core::Vec3 Position(const RE::Actor& a_actor)
		{
			const auto& pos = a_actor.GetPosition();
			return { pos.x, pos.y, pos.z };
		}

		void FirstAttack(core::EntityID a_summon, Trace& a_trace, Time a_time, const char* a_signal)
		{
			a_trace.firstAttack = a_time;
			logger::info("[AttackTrace] order={} summon=0x{:08X} target=0x{:08X} FIRST_ATTACK t={:.3f}s signal={}",
				a_trace.origin.id, a_summon, a_trace.focus, Seconds(a_trace.origin.impactAt, a_time), a_signal);
		}

		void Finish(core::EntityID a_summon, Trace& a_trace, Time a_now, const char* a_reason)
		{
			if (a_trace.hit) {
				if (!a_trace.firstAttack) {
					FirstAttack(a_summon, a_trace, a_trace.hit->time, "hit_only");
				}
				logger::info("[AttackTrace] order={} summon=0x{:08X} target=0x{:08X} FIRST_HIT t={:.3f}s source=0x{:08X}",
					a_trace.origin.id, a_summon, a_trace.focus, Seconds(a_trace.origin.impactAt, a_trace.hit->time), a_trace.hit->source);
			}
			logger::info("[AttackTrace] order={} summon=0x{:08X} target=0x{:08X} END t={:.3f}s reason={} first_attack={} first_hit={} path_xy={:.1f}",
				a_trace.origin.id, a_summon, a_trace.focus, Seconds(a_trace.origin.impactAt, a_now), a_reason,
				a_trace.firstAttack.has_value(), a_trace.hit.has_value(), a_trace.path.distance);
		}

		void Sample(core::EntityID a_summon, Trace& a_trace, const RE::Actor& a_actor,
			const RE::Actor& a_target, Time a_now, bool a_final)
		{
			const auto& pos = a_trace.path.previous;
			const auto target = Position(a_target);
			const float distance = a_trace.path.distance - a_trace.lastLoggedDistance;
			const float forward = a_trace.path.forwardDistance - a_trace.lastLoggedForward;
			const float duration = Seconds(a_trace.logged, a_now);
			logger::info("[AttackTrace] order={} summon=0x{:08X} target=0x{:08X} {} t={:.3f}s pos=({:.1f},{:.1f},{:.1f}) goal=({:.1f},{:.1f},{:.1f}) range_xy={:.1f} path_xy={:.1f} speed_xy={:.1f} forward_ratio={:.2f} yaw_deg={:.1f}",
				a_trace.origin.id, a_summon, a_trace.focus, a_final ? "ATTACK_POS" : "MOVE",
				Seconds(a_trace.origin.impactAt, a_now), pos.x, pos.y, pos.z, target.x, target.y, target.z,
				std::hypot(target.x - pos.x, target.y - pos.y), a_trace.path.distance,
				duration > 0.0f ? distance / duration : 0.0f,
				distance > 0.01f ? forward / distance : 0.0f, a_actor.GetAngleZ() * 180.0f / std::numbers::pi_v<float>);
			a_trace.logged = a_now;
			a_trace.lastLoggedDistance = a_trace.path.distance;
			a_trace.lastLoggedForward = a_trace.path.forwardDistance;
		}

		const char* Observe(core::EntityID a_id, Trace& a_trace, core::SummonRegistry& a_registry, Time a_now)
		{
			const auto actor = a_trace.summon.get();
			const auto target = a_trace.target.get();
			const auto* model = a_registry.GetModel(a_id);
			const bool loaded = actor && target && actor->Is3DLoaded() && target->Is3DLoaded() &&
				ShareLoadedSpace(actor.get(), target.get());
			const bool orderActive = model && model->order == core::SummonOrder::Attack &&
				model->currentTargetId == a_trace.focus;

			if (!a_trace.firstAttack && loaded && orderActive) {
				using Attack = RE::ATTACK_STATE_ENUM;
				const auto phase = actor->AsActorState()->GetAttackState();
				const bool hasFocus = actor->GetActorRuntimeData().currentCombatTarget == a_trace.target;
				const bool weaponStart = a_trace.weapon.Observe(phase == Attack::kNone,
					phase == Attack::kDraw || phase == Attack::kSwing || phase == Attack::kBash ||
					phase == Attack::kBowDraw || phase == Attack::kFire, hasFocus);
				a_trace.path.Observe(Position(*actor), actor->GetAngleZ());

				if (hasFocus && a_trace.cast && (!a_trace.hit || a_trace.cast->time <= a_trace.hit->time)) {
					FirstAttack(a_id, a_trace, a_trace.cast->time, "hostile_spell_cast");
				} else if (a_trace.hit) {
					FirstAttack(a_id, a_trace, a_trace.hit->time, "hit_only");
				} else if (weaponStart) {
					FirstAttack(a_id, a_trace, a_now, "weapon_animation");
				}

				if (a_trace.firstAttack || a_now - a_trace.logged >= 100ms) {
					Sample(a_id, a_trace, *actor, *target, a_now, a_trace.firstAttack.has_value());
				}
				a_trace.cast.reset();
			}

			if (a_trace.hit) {
				return "first_hit";
			}
			if (!actor || !model || actor->IsDead() || actor->IsDisabled()) {
				return "summon_gone";
			}
			if (!target || target->IsDead() || target->IsDisabled()) {
				return "target_gone";
			}
			if (!loaded) {
				return "unloaded";
			}
			if (!orderActive) {
				return "order_changed";
			}
			return a_now - a_trace.origin.impactAt >= kTimeout ? "timeout" : nullptr;
		}
	}

	void Begin(RE::Actor& a_summon, RE::Actor& a_target, const core::AttackTraceOrigin& a_origin)
	{
		std::lock_guard lock(mutex);
		const auto now = Clock::now();
		const auto id = a_summon.GetFormID();
		const auto start = Position(a_summon);

		if (const auto it = traces.find(id); it != traces.end()) {
			Finish(id, it->second, now, "new_order");
		}
		traces.insert_or_assign(id, Trace{
			.origin = a_origin,
			.summon = a_summon.GetHandle(),
			.target = a_target.GetHandle(),
			.focus = a_target.GetFormID(),
			.dispatched = now,
			.logged = now,
			.path = { start },
			.weapon = { a_summon.AsActorState()->GetAttackState() == RE::ATTACK_STATE_ENUM::kNone }
		});
		const auto& goal = a_origin.impactTarget;
		logger::info("[AttackTrace] order={} summon=0x{:08X} target=0x{:08X} START dispatch={:.3f}s from=({:.1f},{:.1f},{:.1f}) impact_goal=({:.1f},{:.1f},{:.1f})",
			a_origin.id, id, a_target.GetFormID(), Seconds(a_origin.impactAt, now),
			start.x, start.y, start.z, goal.x, goal.y, goal.z);
	}

	void ObserveHit(core::EntityID a_summon, core::EntityID a_target, core::EntityID a_source)
	{
		const auto now = Clock::now();
		std::lock_guard lock(mutex);
		const auto it = traces.find(a_summon);
		if (it != traces.end() && it->second.focus == a_target && !it->second.hit &&
		    now >= it->second.dispatched && now - it->second.origin.impactAt <= kTimeout) {
			it->second.hit = Event{ now, a_source };
		}
	}

	void ObserveCast(core::EntityID a_summon, core::EntityID a_spell)
	{
		const auto now = Clock::now();
		std::lock_guard lock(mutex);
		const auto it = traces.find(a_summon);
		if (it == traces.end() || it->second.cast || it->second.firstAttack ||
		    now < it->second.dispatched || now - it->second.origin.impactAt > kTimeout) {
			return;
		}
		const auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spell);
		if (spell && spell->IsHostile() && spell->GetDelivery() != RE::MagicSystem::Delivery::kSelf) {
			// TESSpellCastEvent has no victim. Confirm this cast against the
			// actor's current combat target on the next update.
			it->second.cast = Event{ now, a_spell };
		}
	}

	void Tick(core::SummonRegistry& a_registry)
	{
		std::lock_guard lock(mutex);
		const auto now = Clock::now();
		for (auto it = traces.begin(); it != traces.end();) {
			if (const auto* reason = Observe(it->first, it->second, a_registry, now)) {
				Finish(it->first, it->second, now, reason);
				it = traces.erase(it);
			} else {
				++it;
			}
		}
	}

	void Reset(const char* a_reason)
	{
		std::lock_guard lock(mutex);
		const auto now = Clock::now();
		for (auto& [id, trace] : traces) {
			Finish(id, trace, now, a_reason);
		}
		traces.clear();
	}
}
