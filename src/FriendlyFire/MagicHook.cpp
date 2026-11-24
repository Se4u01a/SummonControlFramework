#include "FriendlyFire/MagicHook.h"
#include "Core/FrameworkContext.h"
#include "Core/KeywordApi.h"
#include "Engine/KeywordApi.h"
#include "FriendlyFire/RelationshipClassifier.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/E/Effect.h>
#include <RE/E/EffectSetting.h>
#include <RE/E/EnchantmentItem.h>
#include <RE/M/MagicItem.h>
#include <RE/M/MagicTarget.h>
#include <RE/P/PlayerCharacter.h>

#include <atomic>
#include <chrono>

namespace scf::friendly_fire
{
	namespace
	{
		// TESObjectREFR owns vtables [0..3] ([3] at +0x38 is IAnimationGraphManagerHolder).
		// Actor/Character::MagicTarget is sub-vtable [4] (+0x98 on SE 1.5.97, +0xA0 on AE).
		constexpr std::size_t kMagicTargetVtableIndex = 4;
		constexpr std::size_t kAddTargetSlot = 1;
		constexpr REL::ID kCharacterAddTargetSE{ 37832 };  // SkyrimSE.exe+0x633090 (Actor::AddTarget)

		std::atomic<std::int64_t> s_lastLogMs{ 0 };
		std::atomic<bool> s_observed{ false };

		bool RateLimitedLog()
		{
			const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			auto last = s_lastLogMs.load(std::memory_order_relaxed);
			return now - last >= 500 && s_lastLogMs.compare_exchange_strong(last, now, std::memory_order_relaxed);
		}

		// Who cast it, as seen by the friendly-fire matrix. Engine facts only (commanding
		// actor, teammate flag), no registry access: AddTarget is not guaranteed to run on
		// the main thread.
		AttackSourceKind ClassifyCaster(RE::Actor* a_caster, const RE::Actor* a_player)
		{
			if (!a_caster) {
				return AttackSourceKind::Unknown;
			}
			if (a_caster == a_player) {
				return AttackSourceKind::Player;
			}
			const auto commander = a_caster->GetCommandingActor();
			if (commander && commander.get() == a_player) {
				return AttackSourceKind::PlayerOwnedSummon;
			}
			if (a_caster->IsPlayerTeammate()) {
				return AttackSourceKind::PlayerAlly;
			}
			if (commander && commander->IsPlayerTeammate()) {
				return AttackSourceKind::AlliedSummon;
			}
			return AttackSourceKind::Unknown;
		}

		bool IsProtected(RE::Actor* a_target, const RE::Actor* a_player, const FriendlyFireSettings& a_settings)
		{
			bool baseProtected = false;
			if (const auto commander = a_target->GetCommandingActor()) {
				if (commander.get() == a_player) {
					baseProtected = a_settings.protectPlayerOwnedSummons;
				} else {
					baseProtected = a_settings.protectAllyOwnedSummons && commander->IsPlayerTeammate();
				}
			}
			return core::ResolveFriendlyFireProtection(
				baseProtected,
				engine::keywords::FriendlyFireFlagsOf(a_target->GetFormID()));
		}

		struct AddTargetHook
		{
			static bool thunk(RE::MagicTarget* a_this, RE::MagicTarget::AddTargetData& a_data)
			{
				if (!s_observed.exchange(true, std::memory_order_relaxed)) {
					logger::info("[MagicHook] Character MagicTarget::AddTarget seat OBSERVED running.");
				}
				if (ShouldRefuse(a_this, a_data)) {
					return false;
				}
				return func(a_this, a_data);
			}

			static bool ShouldRefuse(RE::MagicTarget* a_this, const RE::MagicTarget::AddTargetData& a_data) noexcept
			{
				try {
					const auto& settings = core::FrameworkContext::GetSingleton().GetSettings().Get().friendlyFire;
					if (!settings.enabled || !a_this || !a_data.effect || !a_data.effect->baseEffect) {
						return false;
					}
					auto* base = a_data.effect->baseEffect;
					if (!base->IsHostile() && !base->IsDetrimental()) {
						return false;  // buffs, heals, utility effects always apply
					}

					// MagicTarget is a secondary base subobject at a runtime-dependent offset inside
					// Actor (+0x98 SE, +0xA0 AE). In a dual SE+AE CommonLibSSE-NG build TESObjectREFR
					// omits its 0x10-byte RUNTIME_DATA_CONTENT, so static_cast<RE::Actor*>(a_this)
					// subtracts 0x88 instead of 0x98/0xA0 and returns a shifted invalid pointer!
					// Always recover the Actor via MagicTarget::GetTargetAsActor() (virtual slot 2).
					auto* target = a_this->GetTargetAsActor();
					auto* player = RE::PlayerCharacter::GetSingleton();
					if (!target || !player || target == player || !IsProtected(target, player, settings)) {
						return false;
					}

					auto* caster = a_data.caster ? a_data.caster->As<RE::Actor>() : nullptr;
					if (caster == target) {
						return false;  // the summon's own self effects
					}
					const auto sourceKind = ClassifyCaster(caster, player);

					auto* item = a_data.magicItem;
					const bool concentration = item && item->GetCastingType() == RE::MagicSystem::CastingType::kConcentration;
					const bool enchantment = item && item->GetSpellType() == RE::MagicSystem::SpellType::kEnchantment;
					const bool shout = item && item->GetSpellType() == RE::MagicSystem::SpellType::kVoicePower;
					const auto kind = RelationshipClassifier::ClassifyMagicAttack(a_data.areaTarget, concentration, enchantment, shout);

					const auto decision = RelationshipClassifier::EvaluateAttack(sourceKind, kind, true, settings);
					if (!decision.blockDamage) {
						// Visible in the field log: why a hostile effect still reached a protected summon.
						if (RateLimitedLog()) {
							logger::info("[MagicHook] Allowed {} effect '{}' on summon 0x{:08X} from 0x{:08X} ({})",
								ToString(kind), base->GetFullName(), target->GetFormID(),
								caster ? caster->GetFormID() : 0u, ToString(sourceKind));
						}
						return false;
					}
					if (RateLimitedLog()) {
						logger::info("[MagicHook] Refused friendly {} effect '{}' ({}) on summon 0x{:08X} ({}) from 0x{:08X} ({})",
							ToString(kind),
							base->GetFullName(),
							item ? item->GetName() : "?",
							target->GetFormID(), target->GetName(),
							caster ? caster->GetFormID() : 0u, ToString(sourceKind));
					}
					return true;
				} catch (...) {
					return false;  // never throw into engine code; the effect applies
				}
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	bool MagicHook::Install() noexcept
	{
		if (REL::Module::get().version() != SKSE::RUNTIME_SSE_1_5_97) {
			logger::info("[MagicHook] verified on SE 1.5.97 only; friendly magic filter not installed on this runtime.");
			return false;
		}

		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_Character[kMagicTargetVtableIndex] };
		const auto current = *reinterpret_cast<const std::uintptr_t*>(vtable.address() + kAddTargetSlot * sizeof(void*));
		const auto expected = kCharacterAddTargetSE.address();
		const auto& module = REL::Module::get();
		const bool insideGame = current >= module.base() &&
		                        current < module.base() + module.segment(REL::Segment::textx).offset() + module.segment(REL::Segment::textx).size();
		if (current != expected && insideGame) {
			logger::error("[MagicHook] Character MagicTarget (vtable[{}]) slot {} holds 0x{:X}, expected 0x{:X}; not installed.",
				kMagicTargetVtableIndex, kAddTargetSlot, current, expected);
			return false;
		}

		AddTargetHook::func = current;
		vtable.write_vfunc(kAddTargetSlot, AddTargetHook::thunk);
		logger::info("[MagicHook] Character MagicTarget::AddTarget (vtable[{}] slot {}) hooked, chaining (original 0x{:X}){}.",
			kMagicTargetVtableIndex, kAddTargetSlot, AddTargetHook::func.address(),
			current != expected ? " - slot was already hooked by another plugin" : "");
		return true;
	}
}
