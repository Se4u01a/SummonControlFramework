#include "Engine/CommandActions.h"
#include "Core/SummonModel.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/E/ExtraLeveledCreature.h>
#include <RE/M/MagicCaster.h>
#include <RE/S/SendHUDMessage.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESNPC.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace scf::engine::actions
{
	namespace
	{
		constexpr const char* kCatalogPath = "Data/SKSE/Plugins/SummonControlFramework_Commands.ini";

		struct Resolved
		{
			core::CommandAction action{ core::CommandAction::None };
			RE::SpellItem* spell{ nullptr };
			float cooldownSeconds{ 8.0f };
		};

		struct Rush
		{
			float bonus{ 0.0f };
			float endsAt{ 0.0f };
		};

		std::unordered_map<RE::FormID, Resolved> s_byBase;
		std::optional<Resolved> s_fallback;
		float s_rushBonus{ 60.0f };
		float s_rushSeconds{ 3.0f };
		core::CommandCooldowns s_cooldowns;
		std::unordered_map<RE::FormID, Rush> s_rushes;

		struct Batch
		{
			int rushes{ 0 };
			int casts{ 0 };
			int detonations{ 0 };
			int cooling{ 0 };
			int skipped{ 0 };
			float shortestWait{ 0.0f };
		} s_batch;

		std::optional<Resolved> Resolve(const core::CommandEntry& a_entry, const std::string& a_what)
		{
			Resolved resolved{ a_entry.action, nullptr, a_entry.cooldownSeconds };
			if (a_entry.spell) {
				auto* handler = RE::TESDataHandler::GetSingleton();
				resolved.spell = handler ? handler->LookupForm<RE::SpellItem>(a_entry.spell->localId, a_entry.spell->plugin) : nullptr;
				if (!resolved.spell) {
					logger::warn("[Command] {}: spell {}|0x{:06X} not found (plugin not loaded or not a SPEL); line ignored.",
						a_what, a_entry.spell->plugin, a_entry.spell->localId);
					return std::nullopt;
				}
			}
			return resolved;
		}

		const Resolved* FindForBase(RE::TESForm* a_form)
		{
			if (!a_form) {
				return nullptr;
			}
			const auto it = s_byBase.find(a_form->GetFormID());
			return it != s_byBase.end() ? &it->second : nullptr;
		}

		// Summon base candidates, most specific first: the leveled original / template
		// base, the actor's own base, then its TPLT chain.
		const Resolved* FindForSummon(RE::Actor* a_summon)
		{
			std::vector<RE::TESForm*> candidates;
			if (auto* leveled = a_summon->extraList.GetByType<RE::ExtraLeveledCreature>()) {
				candidates.push_back(leveled->originalBase);
				candidates.push_back(leveled->templateBase);
			}
			auto* npc = a_summon->GetActorBase();
			for (int depth = 0; npc && depth < 8; ++depth) {
				candidates.push_back(npc);
				auto* next = npc->baseTemplateForm;
				npc = next ? next->As<RE::TESNPC>() : nullptr;
			}
			for (auto* form : candidates) {
				if (const auto* found = FindForBase(form)) {
					return found;
				}
			}
			return s_fallback ? &*s_fallback : nullptr;
		}

		void NudgeSpeed(RE::Actor* a_actor)
		{
			// Movement speed is recomputed when carry weight changes; a net-zero nudge
			// makes a SpeedMult change take effect without waiting for the next equip.
			auto* values = a_actor->AsActorValueOwner();
			values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f);
			values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
		}

		void EndRush(RE::FormID a_id, const Rush& a_rush)
		{
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
			if (!actor || actor->IsDeleted()) {
				return;
			}
			actor->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -a_rush.bonus);
			NudgeSpeed(actor);
			logger::info("[Command] Summon 0x{:08X} rush over (SpeedMult -{:.0f}).", a_id, a_rush.bonus);
		}

		bool IsValidTarget(RE::Actor* a_summon, RE::Actor* a_target)
		{
			return a_target && a_target != a_summon && !a_target->IsDead() && !a_target->IsPlayerRef();
		}
	}

	void Load()
	{
		std::string text;
		if (!std::filesystem::exists(kCatalogPath)) {
			text = core::DefaultCommandCatalogText();
			std::ofstream out(kCatalogPath, std::ios::binary);
			out << text;
			logger::info("[Command] {} not found; default catalog written.", kCatalogPath);
		} else {
			std::ifstream in(kCatalogPath, std::ios::binary);
			std::ostringstream buffer;
			buffer << in.rdbuf();
			text = buffer.str();
		}

		const auto catalog = core::ParseCommandCatalog(text);
		for (const auto& warning : catalog.warnings) {
			logger::warn("[Command] {}: {}", kCatalogPath, warning);
		}

		s_byBase.clear();
		auto* handler = RE::TESDataHandler::GetSingleton();
		for (const auto& [key, entry] : catalog.byBase) {
			auto* base = handler ? handler->LookupForm(key.localId, key.plugin) : nullptr;
			if (!base) {
				logger::warn("[Command] summon {}|0x{:06X} not found (plugin not loaded?); line ignored.", key.plugin, key.localId);
				continue;
			}
			const auto what = std::format("{}|0x{:06X}", key.plugin, key.localId);
			if (auto resolved = Resolve(entry, what)) {
				s_byBase[base->GetFormID()] = *resolved;
			}
		}
		s_fallback.reset();
		if (catalog.fallback) {
			s_fallback = Resolve(*catalog.fallback, "Default");
		}
		s_rushBonus = catalog.rushSpeedBonus;
		s_rushSeconds = catalog.rushSeconds;
		logger::info("[Command] catalog: {} summon types, default {}, rush +{:.0f} SpeedMult for {:.1f} s.",
			s_byBase.size(), s_fallback ? core::ToString(s_fallback->action) : "none", s_rushBonus, s_rushSeconds);
	}

	Outcome Execute(RE::Actor* a_summon, const core::SummonModel* a_model, RE::Actor* a_target, float a_now)
	{
		if (!a_summon || a_summon->IsDead()) {
			return Outcome::Skipped;
		}
		const auto id = a_summon->GetFormID();
		const auto* entry = FindForSummon(a_summon);
		if (!entry || entry->action == core::CommandAction::None) {
			logger::info("[Command] Summon 0x{:08X} ({}) has no command action.", id, a_summon->GetName());
			++s_batch.skipped;
			return Outcome::Skipped;
		}
		if (!s_cooldowns.IsReady(id, a_now)) {
			const auto wait = s_cooldowns.Remaining(id, a_now);
			logger::info("[Command] Summon 0x{:08X} {} on cooldown ({:.1f} s left).", id, core::ToString(entry->action), wait);
			s_batch.shortestWait = s_batch.cooling == 0 ? wait : std::min(s_batch.shortestWait, wait);
			++s_batch.cooling;
			return Outcome::Skipped;
		}

		const bool targetOk = IsValidTarget(a_summon, a_target);
		switch (entry->action) {
		case core::CommandAction::Rush:
			{
				if (!targetOk) {
					++s_batch.skipped;
					return Outcome::Skipped;
				}
				if (a_model && a_model->aggression == core::AggressionMode::Passive) {
					logger::info("[Command] Summon 0x{:08X} rush refused: Passive.", id);
					++s_batch.skipped;
					return Outcome::Skipped;
				}
				auto& rush = s_rushes[id];
				if (rush.bonus == 0.0f && s_rushBonus > 0.0f) {
					rush.bonus = s_rushBonus;
					a_summon->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, rush.bonus);
					NudgeSpeed(a_summon);
				}
				rush.endsAt = a_now + s_rushSeconds;
				s_cooldowns.Start(id, a_now, entry->cooldownSeconds);
				logger::info("[Command] Summon 0x{:08X} RUSH at 0x{:08X} ({}): SpeedMult +{:.0f} for {:.1f} s, cooldown {:.0f} s.",
					id, a_target->GetFormID(), a_target->GetName(), rush.bonus, s_rushSeconds, entry->cooldownSeconds);
				++s_batch.rushes;
				return Outcome::Rush;
			}

		case core::CommandAction::CastAtTarget:
		case core::CommandAction::Detonate:
			{
				const bool detonate = entry->action == core::CommandAction::Detonate;
				RE::Actor* castTarget = targetOk ? a_target : nullptr;
				if (!castTarget && detonate && entry->spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf) {
					castTarget = a_summon;
				}
				auto* caster = a_summon->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
				if (!castTarget || !caster) {
					++s_batch.skipped;
					return Outcome::Skipped;
				}
				caster->CastSpellImmediate(entry->spell, false, castTarget, 1.0f, false, 0.0f, a_summon);
				s_cooldowns.Start(id, a_now, entry->cooldownSeconds);
				logger::info("[Command] Summon 0x{:08X} {} '{}' (0x{:08X}) at 0x{:08X}, cooldown {:.0f} s.",
					id, detonate ? "DETONATES with" : "casts", entry->spell->GetName(), entry->spell->GetFormID(),
					castTarget->GetFormID(), entry->cooldownSeconds);
				if (detonate) {
					s_cooldowns.Forget(id);
					a_summon->KillImmediate();
					++s_batch.detonations;
					return Outcome::Detonated;
				}
				++s_batch.casts;
				return Outcome::Cast;
			}

		case core::CommandAction::None:
		default:
			++s_batch.skipped;
			return Outcome::Skipped;
		}
	}

	void ReportBatch()
	{
		std::string message = "SCF: Command";
		const auto add = [&](int a_count, const char* a_label) {
			if (a_count > 0) {
				message += std::format(" | {} {}", a_count, a_label);
			}
		};
		add(s_batch.rushes, "rush");
		add(s_batch.casts, "cast");
		add(s_batch.detonations, "detonate");
		if (s_batch.cooling > 0) {
			message += std::format(" | {} on cooldown ({:.0f} s)", s_batch.cooling, s_batch.shortestWait);
		}
		add(s_batch.skipped, "skipped");
		if (s_batch.rushes + s_batch.casts + s_batch.detonations + s_batch.cooling + s_batch.skipped == 0) {
			message += " | no summons";
		}
		RE::SendHUDMessage::ShowHUDMessage(message.c_str());
		logger::info("[Command] {}", message);
		s_batch = {};
	}

	void Tick(float a_now)
	{
		for (auto it = s_rushes.begin(); it != s_rushes.end();) {
			if (a_now >= it->second.endsAt) {
				EndRush(it->first, it->second);
				it = s_rushes.erase(it);
			} else {
				++it;
			}
		}
	}

	void Reset()
	{
		for (const auto& [id, rush] : s_rushes) {
			EndRush(id, rush);
		}
		s_rushes.clear();
		s_cooldowns.Clear();
		s_batch = {};
	}
}
