#include "FriendlyFire/ProjectileHook.h"
#include "Core/FrameworkContext.h"
#include "FriendlyFire/RelationshipClassifier.h"
#include "PCH.h"

#include <RE/A/ArrowProjectile.h>
#include <RE/B/BeamProjectile.h>
#include <RE/C/ConeProjectile.h>
#include <RE/F/FlameProjectile.h>
#include <RE/M/MissileProjectile.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/Projectile.h>
#include <RE/T/TESHavokUtilities.h>
#include <vector>

namespace scf::friendly_fire
{
	namespace
	{
		constexpr std::size_t kAddImpactVfunc = 189;
		constexpr std::size_t kHandleHitsVfunc = 190;

		using AddImpact_t = RE::Projectile::ImpactData* (*)(
			RE::Projectile*,
			RE::TESObjectREFR*,
			const RE::NiPoint3*,
			const RE::NiPoint3*,
			RE::hkpCollidable*,
			std::int32_t,
			std::uint32_t);

		using HandleHits_t = void (*)(RE::Projectile*, RE::hkpAllCdPointCollector*);

		bool ShouldIgnoreProjectileHit(RE::Projectile* a_proj, const RE::TESObjectREFR* a_target)
		{
			if (!a_proj || !a_target) {
				return false;
			}

			auto* targetActor = a_target->As<RE::Actor>();
			if (!targetActor) {
				return false;
			}

			auto& ctx = core::FrameworkContext::GetSingleton();
			const auto& settings = ctx.GetSettings().Get().friendlyFire;
			if (!settings.enabled) {
				return false;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto playerId = player ? player->GetFormID() : 0x14;

			const auto targetId = targetActor->GetFormID();
			auto shooterPtr = a_proj->GetProjectileRuntimeData().shooter.get();
			const auto attackerId = shooterPtr ? shooterPtr->GetFormID() : core::InvalidEntityID;

			auto isAlly = [](core::EntityID a_id) -> bool {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
				return actor && actor->IsPlayerTeammate();
			};

			auto cmdChain = [](core::EntityID a_id) -> core::EntityID {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
				if (!actor) {
					return core::InvalidEntityID;
				}
				auto* commander = actor->GetCommandingActor().get();
				return commander ? commander->GetFormID() : core::InvalidEntityID;
			};

			const auto sourceKind = RelationshipClassifier::ClassifySource(
				attackerId,
				ctx.GetRegistry(),
				playerId,
				isAlly,
				cmdChain);

			bool isProtected = RelationshipClassifier::IsProtectedTarget(
				targetId,
				ctx.GetRegistry(),
				settings,
				playerId,
				isAlly);

			// Dynamic fallback check in case newly summoned actor is not yet registered
			if (!isProtected) {
				auto commanding = targetActor->GetCommandingActor();
				if (commanding) {
					const auto cmdId = commanding->GetFormID();
					if (cmdId == playerId) {
						isProtected = settings.protectPlayerOwnedSummons;
					} else if (settings.protectAllyOwnedSummons && isAlly(cmdId)) {
						isProtected = true;
					}
				}
			}

			if (!isProtected) {
				return false;
			}

			const auto decision = RelationshipClassifier::EvaluateAttack(
				sourceKind,
				AttackKind::Arrow,
				true,
				settings);

			if (decision.passThrough || decision.blockDamage) {
				logger::info("[ProjectileHook] Projectile 0x{:08X} passed through friendly summon 0x{:08X} ({}) from attacker 0x{:08X}",
					a_proj->GetFormID(), targetId, targetActor->GetName(), attackerId);
				return true;
			}

			return false;
		}

		bool PointIgnored(RE::Projectile* a_proj, const RE::hkpRootCdPoint& a_point)
		{
			for (const auto* root : { a_point.rootCollidableA, a_point.rootCollidableB }) {
				if (!root) {
					continue;
				}
				auto* ref = RE::TESHavokUtilities::FindCollidableRef(*root);
				if (ref && ShouldIgnoreProjectileHit(a_proj, ref)) {
					return true;
				}
			}
			return false;
		}

		template <int Kind>
		struct ProjectileHookImpl
		{
			static RE::Projectile::ImpactData* AddImpact(
				RE::Projectile* a_proj,
				RE::TESObjectREFR* a_target,
				const RE::NiPoint3* a_loc,
				const RE::NiPoint3* a_vel,
				RE::hkpCollidable* a_collidable,
				std::int32_t a_shapeKey,
				std::uint32_t a_flag)
			{
				if (ShouldIgnoreProjectileHit(a_proj, a_target)) {
					return nullptr;
				}
				return s_origAddImpact(a_proj, a_target, a_loc, a_vel, a_collidable, a_shapeKey, a_flag);
			}

			static void HandleHits(RE::Projectile* a_proj, RE::hkpAllCdPointCollector* a_collector)
			{
				if (!a_collector || a_collector->hits._size <= 0) {
					s_origHandleHits(a_proj, a_collector);
					return;
				}

				auto& hits = a_collector->hits;
				const std::int32_t count = hits._size;
				std::vector<RE::hkpRootCdPoint> saved(hits._data, hits._data + count);
				std::int32_t kept = 0;

				for (std::int32_t i = 0; i < count; ++i) {
					if (!PointIgnored(a_proj, saved[i])) {
						hits._data[kept++] = saved[i];
					}
				}

				if (kept == count) {
					s_origHandleHits(a_proj, a_collector);
					return;
				}

				if (kept > 0) {
					hits._size = kept;
					s_origHandleHits(a_proj, a_collector);
				}

				std::copy(saved.begin(), saved.end(), hits._data);
				hits._size = count;
			}

			static inline AddImpact_t s_origAddImpact{ nullptr };
			static inline HandleHits_t s_origHandleHits{ nullptr };
		};

		template <int Kind>
		void HookProjectileVtbl(REL::VariantID a_vtableId, const char* a_name)
		{
			REL::Relocation<std::uintptr_t> vtbl{ a_vtableId };
			ProjectileHookImpl<Kind>::s_origAddImpact = reinterpret_cast<AddImpact_t>(
				vtbl.write_vfunc(kAddImpactVfunc, ProjectileHookImpl<Kind>::AddImpact));
			ProjectileHookImpl<Kind>::s_origHandleHits = reinterpret_cast<HandleHits_t>(
				vtbl.write_vfunc(kHandleHitsVfunc, ProjectileHookImpl<Kind>::HandleHits));
			logger::info("[ProjectileHook] Hooked {} (AddImpact [189] + HandleHits [190]).", a_name);
		}
	}

	bool ProjectileHook::Install() noexcept
	{
		HookProjectileVtbl<0>(RE::VTABLE_ArrowProjectile[0], "ArrowProjectile");
		HookProjectileVtbl<1>(RE::VTABLE_MissileProjectile[0], "MissileProjectile");
		HookProjectileVtbl<2>(RE::VTABLE_FlameProjectile[0], "FlameProjectile");
		HookProjectileVtbl<3>(RE::VTABLE_ConeProjectile[0], "ConeProjectile");
		HookProjectileVtbl<4>(RE::VTABLE_BeamProjectile[0], "BeamProjectile");
		return true;
	}
}
