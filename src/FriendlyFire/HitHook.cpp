#include "FriendlyFire/HitHook.h"
#include "Core/FrameworkContext.h"
#include "FriendlyFire/HitReactionHook.h"
#include "FriendlyFire/RelationshipClassifier.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/H/HitData.h>
#include <RE/P/PlayerCharacter.h>

#include <cstring>

namespace scf::friendly_fire
{
	namespace
	{
		// SE 1.5.97 / AE Address Library ID for melee hit resolution function
		constexpr std::uint32_t kMeleeHitFunctionId = 37673;
		constexpr std::ptrdiff_t kHitCallOffset = 0x3C0;

		// AIProcess::SetAggressor(bool a_aggressor) -> void, SE 1.5.97 id 38185 -> +644300:
		//   `test dl,dl; je +0A; or byte [rcx+0x136],4; ret; and byte [rcx+0x136],0xFB; ret`
		// Disassembled and verified byte for byte from the unpacked 1.5.97 image
		// (2026-11-11); the same function and bit are what APMF documents as AIProcess's
		// aggressor flag (AIProcess+0x136 bit 2, VERIFIED-ADDRESSES row
		// Substrate.AIProcess.SetAggressor). It is the engine's own setter, so
		// bSuppressFriendlyAggression becomes a real effect instead of a computed flag
		// nothing read: the summon stops treating a friendly attacker as somebody to fight.
		constexpr std::uint32_t kSetAggressorId = 38185;
		constexpr std::uint8_t kExpectedSetAggressor[] = {
			0x84, 0xD2, 0x74, 0x08, 0x80, 0x89, 0x36, 0x01, 0x00, 0x00, 0x04, 0xC3
		};

		using ProcessHit_t = void (*)(RE::Actor*, RE::HitData*);
		using SetAggressor_t = void (*)(void*, bool);

		ProcessHit_t g_originalProcessHit{ nullptr };
		SetAggressor_t g_setAggressor{ nullptr };

		// Clear the "I was attacked by an aggressor" bit the engine sets on the victim while
		// the hit is applied. Called after the real ProcessHit returns, so it undoes the
		// reaction without touching damage, and only when the friendly-fire matrix asked for
		// it (bSuppressFriendlyAggression).
		void ClearAggressorReaction(RE::Actor* a_victim) noexcept
		{
			if (!g_setAggressor || !a_victim) {
				return;
			}
			auto* process = a_victim->GetActorRuntimeData().currentProcess;
			if (!process) {
				return;
			}
			g_setAggressor(process, false);
		}

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

			bool suppressAggressor = false;
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

				// bSuppressFriendlyHitReactions: the damage may pass, the flinch must not.
				// The stagger and the pushback are the reaction carried in the hit data, and
				// the engine call that applies them runs inside the window armed below, so
				// HitReactionHook::ApplyStaggerThunk refuses the stagger state itself.
				if (decision.suppressHitReaction) {
					a_hitData->stagger = 0.0f;
					a_hitData->pushBack = 0.0f;
					HitReactionHook::ArmFor(targetId);
					logger::info("[HitHook] Suppressing friendly hit reaction on summon 0x{:08X} ({}) from attacker 0x{:08X} ({})",
						targetId, a_victim->GetName(), attackerId, aggressorPtr ? aggressorPtr->GetName() : "Unknown");
				}

				// bSuppressFriendlyAggression: the hit still lands, the summon just does not
				// go looking for a fight over it (cleared after the engine applied the hit).
				suppressAggressor = decision.suppressAggression;
			}

			g_originalProcessHit(a_victim, a_hitData);
			HitReactionHook::Disarm();

			if (suppressAggressor) {
				ClearAggressorReaction(a_victim);
				logger::info("[HitHook] Cleared aggressor reaction on summon 0x{:08X} ({}) after friendly hit from attacker 0x{:08X} ({})",
					targetId, a_victim->GetName(), attackerId, aggressorPtr ? aggressorPtr->GetName() : "Unknown");
			}
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

		// The aggressor-bit setter is what makes bSuppressFriendlyAggression real; it is
		// resolved and byte-verified here so a wrong ID disables that one rule instead of
		// clearing an unrelated bit at runtime. Verified on SE 1.5.97 only.
		if (REL::Module::get().version() == SKSE::RUNTIME_SSE_1_5_97) {
			const REL::Relocation<std::uintptr_t> setAggressor{ REL::ID(kSetAggressorId) };
			if (setAggressor.address() &&
				std::memcmp(reinterpret_cast<const void*>(setAggressor.address()), kExpectedSetAggressor, sizeof(kExpectedSetAggressor)) == 0) {
				g_setAggressor = reinterpret_cast<SetAggressor_t>(setAggressor.address());
				logger::info("[HitHook] AIProcess::SetAggressor 0x{:X} verified (AIProcess+0x136 bit 2); bSuppressFriendlyAggression is live.",
					setAggressor.address());
			} else {
				logger::warn("[HitHook] ID {} is not the verified AIProcess::SetAggressor; bSuppressFriendlyAggression stays inert on this runtime.",
					kSetAggressorId);
			}
		} else {
			logger::info("[HitHook] aggressor clear verified on SE 1.5.97 only; bSuppressFriendlyAggression stays inert on this runtime.");
		}

		return true;
	}
}
