#include "Engine/CommandSpells.h"
#include "Core/CommandSpells.h"
#include "Core/FrameworkContext.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/B/BSTEvent.h>
#include <RE/E/EffectSetting.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/S/ScriptEventSourceHolder.h>
#include <RE/S/SendHUDMessage.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESMagicEffectApplyEvent.h>
#include <RE/T/TESSpellCastEvent.h>

#ifdef GetObject
#	undef GetObject  // Win32 GDI macro collides with BSScript::Variable::GetObject.
#endif

#include <array>
#include <string>

namespace scf::engine::command_spells
{
	namespace
	{
		struct LoadedForms
		{
			RE::SpellItem* holdPosition{ nullptr };
			RE::SpellItem* follow{ nullptr };
			RE::SpellItem* attackTarget{ nullptr };
			RE::EffectSetting* attackTargetEffect{ nullptr };
			core::CommandSpellIds ids{};
		};

		LoadedForms s_forms{};
		bool s_sinksRegistered{ false };

		void Notify(const char* a_msg)
		{
			if (a_msg && a_msg[0]) {
				RE::SendHUDMessage::ShowHUDMessage(a_msg);
				logger::info("[CommandSpells] {}", a_msg);
			}
		}

		bool SquadIsPassive(RE::PlayerCharacter* a_player)
		{
			if (!a_player) {
				return false;
			}
			auto& registry = core::FrameworkContext::GetSingleton().GetRegistry();
			for (const auto summonId : registry.GetActiveSummonsForOwner(a_player->GetFormID())) {
				if (auto* model = registry.GetModel(summonId); model && model->aggression == core::AggressionMode::Passive) {
					return true;
				}
			}
			return false;
		}

		bool IsAllyOrOwnSummon(RE::PlayerCharacter* a_player, RE::Actor* a_target)
		{
			if (!a_player || !a_target) {
				return false;
			}
			if (a_target->IsPlayerTeammate()) {
				return true;
			}
			auto& registry = core::FrameworkContext::GetSingleton().GetRegistry();
			if (registry.IsRegistered(a_target->GetFormID())) {
				return true;
			}
			if (const auto commander = a_target->GetCommandingActor();
				commander && (commander.get() == a_player || commander->IsPlayerTeammate())) {
				return true;
			}
			return false;
		}

		// Use the game's script-facing combat command, including its normal handling
		// of a neutral actor. The command MGEF itself stays harmless on rejected hits.
		bool RequestCombatWithPlayer(RE::Actor* target, RE::PlayerCharacter* player)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			RE::BSScript::Variable receiver;
			receiver.Pack(target);
			if (!vm || !receiver.IsObject()) {
				return false;
			}
			auto object = receiver.GetObject();
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
			return object && vm->DispatchMethodCall(object, "StartCombat",
				RE::MakeFunctionArguments(static_cast<RE::Actor*>(player)), callback);
		}

		class SpellCastSink final : public RE::BSTEventSink<RE::TESSpellCastEvent>
		{
		public:
			static SpellCastSink* GetSingleton()
			{
				static SpellCastSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESSpellCastEvent* a_event,
				RE::BSTEventSource<RE::TESSpellCastEvent>*) override
			{
				if (!a_event || !a_event->object || !s_forms.ids.IsLoaded()) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto& ctx = core::FrameworkContext::GetSingleton();
				if (!ctx.GetSettings().Get().general.enabled) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || a_event->object.get() != player) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto resolution = core::ResolveSelfCommandCast(
					a_event->object->GetFormID(),
					player->GetFormID(),
					a_event->spell,
					s_forms.ids,
					ctx.GetScheduler().GetTotalElapsedTime());

				if (!resolution.Accepted()) {
					return RE::BSEventNotifyControl::kContinue;
				}

				ctx.GetCommandQueue().Enqueue(*resolution.command);
				ctx.GetScheduler().RequestImmediateTick();

				if (resolution.kind == core::CommandSpellKind::HoldPosition) {
					Notify("Приказ: Держать позицию");
				} else if (resolution.kind == core::CommandSpellKind::Follow) {
					Notify("Приказ: Следовать");
				}

				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class MagicEffectApplySink final : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
		{
		public:
			static MagicEffectApplySink* GetSingleton()
			{
				static MagicEffectApplySink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESMagicEffectApplyEvent* a_event,
				RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
			{
				if (!a_event || !a_event->caster || !a_event->target || !s_forms.ids.IsLoaded()) {
					return RE::BSEventNotifyControl::kContinue;
				}

				if (a_event->magicEffect != s_forms.ids.attackTargetEffect) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto& ctx = core::FrameworkContext::GetSingleton();
				if (!ctx.GetSettings().Get().general.enabled) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || a_event->caster.get() != player) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* targetActor = a_event->target->As<RE::Actor>();
				if (!targetActor) {
					return RE::BSEventNotifyControl::kContinue;
				}

				core::TargetActorCandidate candidate{};
				candidate.targetId = targetActor->GetFormID();
				candidate.isDead = targetActor->IsDead();
				candidate.isOwner = (targetActor == player);
				candidate.isAllyOrOwnSummon = IsAllyOrOwnSummon(player, targetActor);

				const auto resolution = core::ResolveAttackTargetHit(
					a_event->caster->GetFormID(),
					player->GetFormID(),
					a_event->magicEffect,
					s_forms.ids,
					candidate,
					SquadIsPassive(player),
					ctx.GetScheduler().GetTotalElapsedTime());

				if (!resolution.Accepted()) {
					if (resolution.rejectReason == core::CommandSpellRejectReason::SquadPassive) {
						Notify("Приказ отклонён: пассивный режим");
					} else {
						logger::debug("[CommandSpells] AttackTarget hit on 0x{:08X} ignored ({})",
							candidate.targetId, core::ToString(resolution.rejectReason));
					}
					return RE::BSEventNotifyControl::kContinue;
				}

				ctx.GetCommandQueue().Enqueue(*resolution.command);
				ctx.GetScheduler().RequestImmediateTick();

				if (!RequestCombatWithPlayer(targetActor, player)) {
					logger::warn("[CommandSpells] Could not dispatch StartCombat(player) for 0x{:08X}", candidate.targetId);
				}

				const char* name = targetActor->GetName();
				std::string msg = "Приказ: В атаку -> ";
				msg += (name && name[0]) ? name : "цель";
				Notify(msg.c_str());

				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Load()
	{
		s_forms = LoadedForms{};

		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			logger::error("[CommandSpells] TESDataHandler singleton not available.");
			return;
		}

		const std::string_view plugin = core::kCommandSpellsPluginName;
		s_forms.holdPosition = handler->LookupForm<RE::SpellItem>(core::kHoldPositionSpellLocalId, plugin);
		s_forms.follow = handler->LookupForm<RE::SpellItem>(core::kFollowSpellLocalId, plugin);
		s_forms.attackTarget = handler->LookupForm<RE::SpellItem>(core::kAttackTargetSpellLocalId, plugin);
		s_forms.attackTargetEffect = handler->LookupForm<RE::EffectSetting>(core::kAttackTargetEffectLocalId, plugin);

		if (s_forms.holdPosition) {
			s_forms.ids.holdPositionSpell = s_forms.holdPosition->GetFormID();
		}
		if (s_forms.follow) {
			s_forms.ids.followSpell = s_forms.follow->GetFormID();
		}
		if (s_forms.attackTarget) {
			s_forms.ids.attackTargetSpell = s_forms.attackTarget->GetFormID();
		}
		if (s_forms.attackTargetEffect) {
			s_forms.ids.attackTargetEffect = s_forms.attackTargetEffect->GetFormID();
		}

		if (s_forms.ids.IsLoaded()) {
			logger::info(
				"[CommandSpells] Loaded command spells from {}: Hold=0x{:08X}, Follow=0x{:08X}, Attack=0x{:08X} (Effect=0x{:08X}).",
				plugin,
				s_forms.ids.holdPositionSpell,
				s_forms.ids.followSpell,
				s_forms.ids.attackTargetSpell,
				s_forms.ids.attackTargetEffect);
		} else {
			logger::warn(
				"[CommandSpells] {} is not active or missing records (Hold=0x{:08X}, Follow=0x{:08X}, Attack=0x{:08X}, Effect=0x{:08X}); command spells disabled.",
				plugin,
				s_forms.ids.holdPositionSpell,
				s_forms.ids.followSpell,
				s_forms.ids.attackTargetSpell,
				s_forms.ids.attackTargetEffect);
		}

		if (!s_sinksRegistered) {
			if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
				holder->AddEventSink<RE::TESSpellCastEvent>(SpellCastSink::GetSingleton());
				holder->AddEventSink<RE::TESMagicEffectApplyEvent>(MagicEffectApplySink::GetSingleton());
				s_sinksRegistered = true;
				logger::info("[CommandSpells] Registered TESSpellCastEvent and TESMagicEffectApplyEvent sinks.");
			} else {
				logger::error("[CommandSpells] Failed to acquire ScriptEventSourceHolder singleton.");
			}
		}
	}

	bool IsLoaded() noexcept
	{
		return s_forms.ids.IsLoaded();
	}

	bool IsCommandSpell(std::uint32_t a_formId) noexcept
	{
		if (a_formId == 0 || !s_forms.ids.IsLoaded()) {
			return false;
		}
		return a_formId == s_forms.ids.holdPositionSpell ||
		       a_formId == s_forms.ids.followSpell ||
		       a_formId == s_forms.ids.attackTargetSpell ||
		       a_formId == s_forms.ids.attackTargetEffect;
	}

	std::size_t GrantSpellsToPlayer()
	{
		if (!s_forms.ids.IsLoaded()) {
			logger::warn("[CommandSpells] Cannot grant spells: {} not loaded.", core::kCommandSpellsPluginName);
			return 0;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return 0;
		}

		const std::array<RE::SpellItem*, 3> spells{
			s_forms.holdPosition,
			s_forms.follow,
			s_forms.attackTarget
		};

		std::size_t added = 0;
		for (auto* spell : spells) {
			if (spell && !player->HasSpell(spell)) {
				if (player->AddSpell(spell)) {
					++added;
				}
			}
		}

		if (added > 0) {
			logger::info("[CommandSpells] Granted {} command spell(s) to player.", added);
		}
		return added;
	}

	std::size_t RemoveSpellsFromPlayer()
	{
		if (!s_forms.ids.IsLoaded()) {
			return 0;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return 0;
		}

		const std::array<RE::SpellItem*, 3> spells{
			s_forms.holdPosition,
			s_forms.follow,
			s_forms.attackTarget
		};

		std::size_t removed = 0;
		for (auto* spell : spells) {
			if (spell && player->HasSpell(spell)) {
				if (player->RemoveSpell(spell)) {
					++removed;
				}
			}
		}

		if (removed > 0) {
			logger::info("[CommandSpells] Removed {} command spell(s) from player.", removed);
		}
		return removed;
	}

	void SyncPlayerSpells()
	{
		if (!s_forms.ids.IsLoaded()) {
			return;
		}
		const auto& settings = core::FrameworkContext::GetSingleton().GetSettings().Get();
		if (settings.spells.autoGrantCommandSpells) {
			GrantSpellsToPlayer();
		}
	}

	void QueueGrantSpellsToPlayer()
	{
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([]() {
				const auto added = GrantSpellsToPlayer();
				if (added > 0) {
					Notify("SCF: Заклинания приказов добавлены");
				} else if (!IsLoaded()) {
					Notify("SCF: Плагин SummonControlFramework.esp не активен");
				}
			});
		}
	}

	void QueueRemoveSpellsFromPlayer()
	{
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([]() {
				const auto removed = RemoveSpellsFromPlayer();
				if (removed > 0) {
					Notify("SCF: Заклинания приказов удалены");
				}
			});
		}
	}
}
