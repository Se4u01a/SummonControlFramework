#include "Engine/Persistence.h"
#include "Core/SummonRegistry.h"
#include "PCH.h"

#include <RE/A/ActiveEffect.h>
#include <RE/A/Actor.h>
#include <RE/A/ActorValueOwner.h>
#include <RE/E/Effect.h>
#include <RE/E/EffectSetting.h>
#include <RE/M/MagicCaster.h>
#include <RE/M/MagicItem.h>
#include <RE/M/MiddleHighProcessData.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESFile.h>
#include <RE/T/TESForm.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace scf::engine::persistence
{
	namespace
	{
		constexpr std::uint32_t kRecordType{ 0x53435046 };   // 'SCPF'
		constexpr std::uint32_t kRecordVersion{ 1 };
		constexpr std::uint32_t kUniqueId{ 0x53434634 };     // 'SCF4'

		core::PersistenceState s_state;
		core::SavedState s_pending;
		bool s_hasPending{ false };
		bool s_installed{ false };
		float s_lastNow{ 0.0f };

		bool EqualsIgnoreCase(std::string_view a_lhs, std::string_view a_rhs) noexcept
		{
			if (a_lhs.size() != a_rhs.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a_lhs.size(); ++i) {
				const auto l = static_cast<unsigned char>(a_lhs[i]);
				const auto r = static_cast<unsigned char>(a_rhs[i]);
				const auto lower = [](unsigned char a_c) {
					return (a_c >= 'A' && a_c <= 'Z') ? static_cast<unsigned char>(a_c - 'A' + 'a') : a_c;
				};
				if (lower(l) != lower(r)) {
					return false;
				}
			}
			return true;
		}

		// `GetFile(0)` is the ORIGINAL source file of the record; `GetFile()` (index -1)
		// walks to the last one - the winning override, which in a real load order is
		// USSEP / a patch / a mod. The `vanillaSpellsOnly` guard asks about the origin,
		// so it must read index 0. Only consulted when that guard is on.
		bool IsOfficialMasterOrigin(const RE::TESForm* a_form)
		{
			const auto* file = a_form ? a_form->GetFile(0) : nullptr;
			if (!file) {
				return false;
			}
			static constexpr std::array<std::string_view, 5> kMasters{
				"Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"
			};
			const auto name = file->GetFilename();
			for (const auto master : kMasters) {
				if (EqualsIgnoreCase(name, master)) {
					return true;
				}
			}
			return false;
		}

		bool IsSummonCreatureSpell(const RE::MagicItem* a_spell)
		{
			if (!a_spell) {
				return false;
			}
			for (const auto* effect : a_spell->effects) {
				if (effect && effect->baseEffect &&
					effect->baseEffect->GetArchetype() == RE::EffectSetting::Archetype::kSummonCreature) {
					return true;
				}
			}
			return false;
		}

		void CollectCommandedEffects(RE::PlayerCharacter* a_player, std::unordered_map<core::EntityID, RE::ActiveEffect*>& a_out)
		{
			a_out.clear();
			if (!a_player) {
				return;
			}
			auto* process = a_player->GetActorRuntimeData().currentProcess;
			if (!process || !process->middleHigh) {
				return;
			}
			for (const auto& data : process->middleHigh->commandedActors) {
				auto actor = data.commandedActor.get();
				if (!actor || !data.activeEffect) {
					continue;
				}
				a_out[actor->GetFormID()] = data.activeEffect;
			}
		}

		std::uint32_t SpellFormId(const RE::ActiveEffect* a_effect)
		{
			return a_effect && a_effect->spell ? a_effect->spell->GetFormID() : 0;
		}

		bool CastByPlayer(const RE::ActiveEffect* a_effect, RE::PlayerCharacter* a_player)
		{
			if (!a_effect || !a_player) {
				return false;
			}
			const auto caster = a_effect->GetCasterActor();
			return caster && caster.get() == a_player;
		}

		void RefreshPersistentEffect(RE::ActiveEffect* a_effect, const core::PersistenceSettings& a_settings)
		{
			if (!a_effect) {
				return;
			}
			const float remaining = a_effect->duration - a_effect->elapsedSeconds;
			if (!core::NeedsPersistentEffectRefresh(remaining, a_settings)) {
				return;
			}
			logger::debug("[Persistence] refreshing conjuration effect: {:.0f} s left of {:.0f} s elapsed {:.0f} s.",
				remaining, a_effect->duration, a_effect->elapsedSeconds);
			a_effect->duration = a_settings.persistentEffectSeconds;
			a_effect->elapsedSeconds = 0.0f;
		}

		bool ValidFocusTarget(core::EntityID a_targetId, RE::PlayerCharacter* a_player)
		{
			if (a_targetId == core::InvalidEntityID) {
				return false;
			}
			auto* target = RE::TESForm::LookupByID<RE::Actor>(a_targetId);
			return target && !target->IsDead() && target != a_player;
		}

		void ApplyPendingIntent(
			core::PersistenceState& a_state,
			core::EntityID a_actorId,
			core::SummonModel& a_model,
			RE::PlayerCharacter* a_player)
		{
			const auto intent = a_state.TakePendingIntent(a_actorId);
			if (!intent) {
				return;
			}
			a_model.order = intent->order;
			a_model.aggression = intent->aggression;
			a_model.currentTargetId = ValidFocusTarget(intent->focusTargetId, a_player) ? intent->focusTargetId : core::InvalidEntityID;
			a_model.hasMoveTarget = false;
			logger::info("[Persistence] summon 0x{:08X}: order {} / aggression {} restored.",
				a_actorId, core::ToString(a_model.order), core::ToString(a_model.aggression));
		}

		bool CastReviveSummon(RE::PlayerCharacter* a_player, std::uint32_t a_spellFormId)
		{
			if (!a_player || a_spellFormId == 0) {
				return false;
			}
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spellFormId);
			if (!spell) {
				logger::warn("[Persistence] revive spell 0x{:08X} not found; the slot cannot be revived.", a_spellFormId);
				return false;
			}
			auto* caster = a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
			if (!caster) {
				logger::warn("[Persistence] player has no instant magic caster; revive of '{}' not issued.", spell->GetName());
				return false;
			}
			// CastSpellImmediate is the same entry point Papyrus Spell.Cast uses: it applies
			// the summon effect without charging the caster (unverified in game).
			//
			// Placement, per the CommonLibSSE-NG headers: a summon is an
			// RE::SummonCreatureEffect (an ActiveEffect) that owns its own `location` /
			// `rotation` and receives them through ActiveEffect::SetLocation;
			// RE::SummonPlacementEffect is only the ReferenceEffect
			// (TEMP_EFFECT_TYPE::kMagicSummon) that plays the appearance, not a delivery.
			// SpellItem::data.delivery (kSelf / kTargetLocation) is resolved by the magic
			// system, and CastSpellImmediate has no point argument - the reference passed
			// as `a_target` is the only anchor. The player is therefore the correct target
			// for a self-delivery conjuration (the caster) and the only sensible anchor for
			// a kTargetLocation one (its position). Whether the engine rejects a spawn
			// point overlapping the player is unverified in game; a failed spawn shows up
			// as the revive cast timing out and retrying.
			caster->CastSpellImmediate(spell, false, a_player, 1.0f, false, 0.0f, a_player);
			return true;
		}

		void SyncReserve(RE::PlayerCharacter* a_player)
		{
			auto* values = a_player ? a_player->AsActorValueOwner() : nullptr;
			if (!values) {
				return;
			}

			const float applied = s_state.GetAppliedReserve();
			const float baselineMax = std::max(0.0f, values->GetPermanentActorValue(RE::ActorValue::kMagicka) + applied);
			const float desired = s_state.DesiredReserve(baselineMax);
			const float delta = desired - applied;

			if (std::fabs(delta) < 0.5f) {
				if (desired != applied) {
					s_state.SetAppliedReserve(desired);
				}
				return;
			}

			values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kMagicka, -delta);
			s_state.SetAppliedReserve(desired);

			const float newMax = std::max(0.0f, baselineMax - desired);
			if (const float current = values->GetActorValue(RE::ActorValue::kMagicka); current > newMax + 0.5f) {
				values->SetActorValue(RE::ActorValue::kMagicka, newMax);
			}

			logger::info("[Persistence] max Magicka reserve {:.0f} -> {:.0f} ({} persistent summon(s), max {:.0f} -> {:.0f}).",
				applied, desired, s_state.ReservedSummonCount(), baselineMax, newMax);
		}

		void ReleaseReserve(RE::PlayerCharacter* a_player)
		{
			const float applied = s_state.GetAppliedReserve();
			if (applied > 0.0f) {
				if (auto* values = a_player ? a_player->AsActorValueOwner() : nullptr) {
					values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kMagicka, applied);
				}
				logger::info("[Persistence] max Magicka reserve released ({:.0f}).", applied);
			}
			s_state.SetAppliedReserve(0.0f);
		}

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			Save(a_intfc);
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			Load(a_intfc);
		}

		void OnRevert(SKSE::SerializationInterface* a_intfc)
		{
			Revert(a_intfc);
		}
	}

	void Install()
	{
		if (s_installed) {
			return;
		}
		auto* serialization = SKSE::GetSerializationInterface();
		if (!serialization) {
			logger::error("[Persistence] serialization interface unavailable: persistent summons will not survive a save.");
			return;
		}
		serialization->SetUniqueID(kUniqueId);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
		s_installed = true;
		logger::info("[Persistence] co-save callbacks registered (unique id 0x{:08X}, record 0x{:08X}).", kUniqueId, kRecordType);
	}

	void Tick(
		RE::PlayerCharacter* a_player,
		float a_now,
		const std::vector<RE::Actor*>& a_liveSummons,
		core::SummonRegistry& a_registry,
		const core::PersistenceSettings& a_settings)
	{
		if (!a_player) {
			return;
		}
		s_lastNow = a_now;

		const auto settings = a_settings.Sanitized();
		if (!(settings == s_state.GetSettings())) {
			s_state.SetSettings(settings);
		}
		const auto& current = s_state.GetSettings();

		if (s_hasPending) {
			s_state.Restore(s_pending, a_now);
			s_pending = {};
			s_hasPending = false;
			logger::info("[Persistence] co-save restored: {} slot(s), reserve {:.0f}.",
				s_state.GetSlotCount(), s_state.GetAppliedReserve());
		}

		if (!current.persistentSummons) {
			if (s_state.GetSlotCount() > 0 || s_state.GetAppliedReserve() > 0.0f) {
				logger::info("[Persistence] persistent mode off: dropping {} slot(s).", s_state.GetSlotCount());
				ReleaseReserve(a_player);
				s_state.Clear();
			}
			return;
		}

		std::unordered_map<core::EntityID, RE::ActiveEffect*> effects;
		CollectCommandedEffects(a_player, effects);

		std::unordered_set<core::EntityID> live;
		live.reserve(a_liveSummons.size());
		for (const auto* actor : a_liveSummons) {
			if (actor) {
				live.insert(actor->GetFormID());
			}
		}

		// 1. A tracked summon that is alive but no longer in the roster was dispelled
		//    (a death is reported through OnSummonDied and leaves a Dead slot instead).
		{
			std::vector<core::EntityID> gone;
			for (const auto& slot : s_state.GetSlots()) {
				if (slot.state == core::PersistentSlotState::Alive && slot.actorId != core::InvalidEntityID &&
					live.find(slot.actorId) == live.end()) {
					gone.push_back(slot.actorId);
				}
			}
			for (const auto actorId : gone) {
				s_state.RemoveByActor(actorId);
				logger::info("[Persistence] summon 0x{:08X} left the commanded roster; persistent slot dropped.", actorId);
			}
		}

		// 2. Bind the actor a revive produced, bind a restored slot, or track a new summon.
		for (auto* actor : a_liveSummons) {
			if (!actor) {
				continue;
			}
			const auto actorId = actor->GetFormID();
			if (s_state.FindByActor(actorId)) {
				continue;
			}

			const auto effectIt = effects.find(actorId);
			auto* effect = effectIt != effects.end() ? effectIt->second : nullptr;
			const auto spellId = SpellFormId(effect);
			if (spellId == 0) {
				// The commanded entry exists before the effect is initialised; a later tick
				// binds it. Revives keep waiting until their cast timeout.
				continue;
			}

			if (s_state.BindReviving(spellId, actorId, a_now)) {
				logger::info("[Persistence] summon 0x{:08X} revived from spell 0x{:08X}; slot restored.", actorId, spellId);
				continue;
			}
			if (s_state.BindAliveUnbound(spellId, actorId, a_now)) {
				logger::info("[Persistence] summoned actor 0x{:08X} matched a restored slot (spell 0x{:08X}).", actorId, spellId);
				continue;
			}

			const bool isSummonCreature = IsSummonCreatureSpell(effect->spell);
			const bool castByPlayer = CastByPlayer(effect, a_player);
			const bool officialOrigin = IsOfficialMasterOrigin(effect->spell);
			const bool blacklisted = !current.IsSpellAllowed(spellId);
			if (!core::ShouldTrackSummonSpell(isSummonCreature, castByPlayer, officialOrigin, blacklisted, current)) {
				logger::debug("[Persistence] summon 0x{:08X}: spell 0x{:08X} left unmanaged "
							  "(summon creature {}, cast by player {}, official master {}, blacklisted {}, vanilla-only {}).",
					actorId, spellId, isSummonCreature, castByPlayer, officialOrigin, blacklisted, current.vanillaSpellsOnly);
				continue;
			}

			if (const auto* slot = s_state.TrackAlive(actorId, spellId, a_now)) {
				logger::info("[Persistence] summon 0x{:08X} ({}) is now persistent: spell 0x{:08X}, slot {}.",
					actorId, actor->GetName(), spellId, slot->slotId);
			} else {
				logger::warn("[Persistence] slot limit {} reached; summon 0x{:08X} stays a normal summon.",
					current.maxTrackedSlots, actorId);
			}
		}

		// 3. Keep the conjuration effect from expiring.
		for (const auto& slot : s_state.GetSlots()) {
			if (slot.state != core::PersistentSlotState::Alive || slot.actorId == core::InvalidEntityID) {
				continue;
			}
			const auto effectIt = effects.find(slot.actorId);
			if (effectIt != effects.end()) {
				RefreshPersistentEffect(effectIt->second, current);
			}
		}

		// 4. Write a restored intent into the new actor's model once, then keep capturing
		//    the player's current orders into the slot for the co-save.
		for (const auto& slot : s_state.GetSlots()) {
			if (slot.state != core::PersistentSlotState::Alive || slot.actorId == core::InvalidEntityID) {
				continue;
			}
			auto* model = a_registry.GetModel(slot.actorId);
			if (!model) {
				continue;  // the registry sync registers a very fresh actor on the next tick
			}
			if (slot.intentPending) {
				ApplyPendingIntent(s_state, slot.actorId, *model, a_player);
			} else {
				s_state.UpdateIntentFromModel(slot.actorId, *model);
			}
		}

		// 5. Revives: give up on disabled auto-revive, expire casts that produced nothing,
		//    then re-cast the spell of every slot whose timer elapsed.
		if (!current.autoRevive) {
			std::vector<core::EntityID> dropped;
			for (const auto& slot : s_state.GetSlots()) {
				if (slot.state != core::PersistentSlotState::Alive) {
					dropped.push_back(slot.slotId);
				}
			}
			for (const auto slotId : dropped) {
				const auto* slot = s_state.FindSlot(slotId);
				if (!slot) {
					continue;
				}
				logger::info("[Persistence] auto-revive off: dead slot {} (summon 0x{:08X}, spell 0x{:08X}) dropped, reserve released.",
					slotId, slot->actorId, slot->spellFormId);
				s_state.Remove(slotId);
			}
		} else {
			for (const auto slotId : s_state.ExpireReviveCasts(a_now)) {
				const auto* slot = s_state.FindSlot(slotId);
				const auto spellId = slot ? slot->spellFormId : 0;
				const bool retry = s_state.MarkReviveFailed(slotId, a_now);
				if (retry) {
					logger::warn("[Persistence] revive of spell 0x{:08X} produced no actor in time; retry scheduled.", spellId);
				} else {
					logger::warn("[Persistence] revive of spell 0x{:08X} failed too often; slot dropped, reserve released.", spellId);
				}
			}

			if (!a_player->IsDead()) {
				for (const auto slotId : s_state.CollectDueRevives(a_now)) {
					const auto* slot = s_state.FindSlot(slotId);
					if (!slot) {
						continue;
					}
					const auto spellId = slot->spellFormId;
					if (!CastReviveSummon(a_player, spellId)) {
						const bool retry = s_state.MarkReviveFailed(slotId, a_now);
						logger::warn("[Persistence] revive cast of spell 0x{:08X} could not be issued ({}).", spellId,
							retry ? "retry scheduled" : "slot dropped");
						continue;
					}
					s_state.MarkReviveCast(slotId, a_now);
					logger::info("[Persistence] re-cast summon spell 0x{:08X}; waiting for the new actor (slot {}).", spellId, slotId);
				}
			}
		}

		// 6. Reserve of max Magicka.
		SyncReserve(a_player);
	}

	void OnSummonDied(core::EntityID a_summonId, float a_now)
	{
		if (a_summonId == core::InvalidEntityID || !s_state.GetSettings().persistentSummons) {
			return;
		}
		if (s_state.MarkDead(a_summonId, a_now)) {
			const auto* slot = s_state.FindByActor(a_summonId);
			logger::info("[Persistence] summon 0x{:08X} died; revive in {:.1f} s (slot {}, spell 0x{:08X}).",
				a_summonId, s_state.GetSettings().reviveDelaySeconds, slot ? slot->slotId : 0u, slot ? slot->spellFormId : 0u);
			return;
		}
		// Untracked or already reported: only the first report feeds the recent-death rule.
		s_state.RecordDeath(a_summonId, a_now);
	}

	void OnSummonRemoved(core::EntityID a_summonId)
	{
		const auto* slot = s_state.FindByActor(a_summonId);
		if (!slot || slot->state != core::PersistentSlotState::Alive) {
			return;
		}
		s_state.RemoveByActor(a_summonId);
		logger::info("[Persistence] summon 0x{:08X} was removed (dispel/effect gone); persistent slot dropped.", a_summonId);
	}

	bool IsEnabled() noexcept
	{
		return s_state.GetSettings().persistentSummons;
	}

	void Reset()
	{
		s_state.Clear();
		s_state.SetAppliedReserve(0.0f);
		s_pending = {};
		s_hasPending = false;
		s_lastNow = 0.0f;
		logger::info("[Persistence] runtime state reset (slots, revive timers, pending co-save restore).");
	}

	bool Save(SKSE::SerializationInterface* a_intfc)
	{
		if (!a_intfc) {
			return false;
		}
		const auto saved = s_state.Save(s_lastNow);
		const auto bytes = core::EncodeState(saved);
		const bool ok = a_intfc->WriteRecord(
			kRecordType,
			kRecordVersion,
			bytes.data(),
			static_cast<std::uint32_t>(bytes.size()));
		if (ok) {
			logger::info("[Persistence] co-save written: {} slot(s), reserve {:.0f}, {} bytes.",
				saved.slots.size(), saved.reserveApplied, bytes.size());
		} else {
			logger::error("[Persistence] co-save write failed ({} bytes).", bytes.size());
		}
		return ok;
	}

	bool Load(SKSE::SerializationInterface* a_intfc)
	{
		s_pending = {};
		s_hasPending = false;
		if (!a_intfc) {
			return false;
		}

		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;
		bool found = false;

		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type != kRecordType) {
				continue;
			}
			if (version != kRecordVersion) {
				logger::warn("[Persistence] co-save record version {} is not supported (expected {}); state ignored.",
					version, kRecordVersion);
				return false;
			}
			if (length == 0) {
				logger::warn("[Persistence] co-save record is empty; state ignored.");
				return false;
			}

			std::vector<std::uint8_t> buffer(length);
			const auto read = a_intfc->ReadRecordData(buffer.data(), length);
			if (read != length) {
				logger::warn("[Persistence] co-save read short ({} of {} bytes); state ignored.", read, length);
				return false;
			}

			auto decoded = core::DecodeState(buffer);
			if (!decoded) {
				logger::warn("[Persistence] co-save payload is corrupt or of a foreign format; state ignored.");
				return false;
			}

			// Spell FormIDs are stable forms: remap them through the save's load order.
			for (auto& slot : decoded->slots) {
				RE::FormID resolved = 0;
				if (slot.spellFormId != 0 && a_intfc->ResolveFormID(slot.spellFormId, resolved)) {
					slot.spellFormId = resolved;
				} else {
					logger::warn("[Persistence] spell 0x{:08X} from the co-save is gone (plugin removed?); slot dropped.",
						slot.spellFormId);
					slot.spellFormId = 0;
				}
			}

			s_pending = std::move(*decoded);
			s_hasPending = true;
			found = true;
			break;
		}

		if (!found) {
			logger::info("[Persistence] no co-save record for this plugin (old save or first run).");
			return false;
		}
		logger::info("[Persistence] co-save loaded: {} slot(s), reserve {:.0f}; applied on the next tick.",
			s_pending.slots.size(), s_pending.reserveApplied);
		return true;
	}

	void Revert(SKSE::SerializationInterface*)
	{
		// A new game / revert: the current player is being discarded, so only drop state.
		// Releasing the reserve here is wrong - the loaded actor carries its own modifier
		// and the co-save restores the amount that was applied.
		s_state.Clear();
		s_state.SetAppliedReserve(0.0f);
		s_pending = {};
		s_hasPending = false;
		s_lastNow = 0.0f;
		logger::info("[Persistence] revert: persistent state dropped.");
	}
}
