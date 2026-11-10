#include "FriendlyFire/HitHook.h"
#include "Core/FrameworkContext.h"
#include "FriendlyFire/RelationshipClassifier.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/H/HitData.h>
#include <RE/P/PlayerCharacter.h>

namespace scf::friendly_fire
{
	namespace
	{
		// SE 1.5.97 / AE Address Library ID for melee hit resolution function
		constexpr std::uint32_t kMeleeHitFunctionId = 37673;
		constexpr std::ptrdiff_t kHitCallOffset = 0x3C0;

		using ProcessHit_t = void (*)(RE::Actor*, RE::HitData*);
		ProcessHit_t g_originalProcessHit{ nullptr };

		void ProcessHitThunk(RE::Actor* a_victim, RE::HitData* a_hitData)
		{
			if (!a_victim || !a_hitData) {
				if (g_originalProcessHit) {
					g_originalProcessHit(a_victim, a_hitData);
				}
				return;
			}

			auto& ctx = core::FrameworkContext::GetSingleton();
			const auto& settings = ctx.GetSettings().Get().friendlyFire;
			if (!settings.enabled) {
				g_originalProcessHit(a_victim, a_hitData);
				return;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto playerId = player ? player->GetFormID() : 0x14;

			const auto targetId = a_victim->GetFormID();
			auto aggressorPtr = a_hitData->aggressor.get();
			const auto attackerId = aggressorPtr ? aggressorPtr->GetFormID() : core::InvalidEntityID;

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
				auto commanding = a_victim->GetCommandingActor();
				if (commanding) {
					const auto cmdId = commanding->GetFormID();
					if (cmdId == playerId) {
						isProtected = settings.protectPlayerOwnedSummons;
					} else if (settings.protectAllyOwnedSummons && isAlly(cmdId)) {
						isProtected = true;
					}
				}
			}

			if (isProtected) {
				const auto decision = RelationshipClassifier::EvaluateAttack(
					sourceKind,
					AttackKind::Melee,
					true,
					settings);

				if (decision.blockDamage) {
					// Nullify damage, stagger, and pushback to eliminate friendly fire impact
					a_hitData->totalDamage = 0.0f;
					a_hitData->physicalDamage = 0.0f;
					a_hitData->stagger = 0.0f;
					a_hitData->pushBack = 0.0f;
					logger::info("[HitHook] Blocked friendly hit on summon 0x{:08X} ({}) from attacker 0x{:08X} ({})",
						targetId, a_victim->GetName(), attackerId, aggressorPtr ? aggressorPtr->GetName() : "Unknown");
					return; // Drop hit processing entirely
				}
			}

			g_originalProcessHit(a_victim, a_hitData);
		}
	}

	bool HitHook::Install() noexcept
	{
		const REL::Relocation<std::uintptr_t> function{ REL::ID(kMeleeHitFunctionId) };
		const auto site = function.address() + kHitCallOffset;
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(site);

		if (bytes[0] != 0xE8) {
			logger::error("[HitHook] Expected CALL (0xE8) at site 0x{:X}, hook not installed.", site);
			return false;
		}

		REL::Relocation<std::uintptr_t> callSite{ site };
		g_originalProcessHit = reinterpret_cast<ProcessHit_t>(callSite.write_call<5>(ProcessHitThunk));
		logger::info("[HitHook] ProcessHit hook installed successfully at site 0x{:X} (original 0x{:X}).", site, reinterpret_cast<std::uintptr_t>(g_originalProcessHit));
		return true;
	}
}
