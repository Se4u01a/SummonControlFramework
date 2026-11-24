#include "Engine/KeywordApi.h"
#include "Core/KeywordApi.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/B/BGSKeyword.h>
#include <RE/E/Effect.h>
#include <RE/E/EffectSetting.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/ProcessLists.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESNPC.h>
#include <RE/T/TESRace.h>

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace scf::engine::keywords
{
	namespace
	{
		struct BoundKeyword
		{
			RE::BGSKeyword* keyword{ nullptr };
			core::ParsedKeyword parsed{};
			bool isWorld{ false };
		};

		std::vector<BoundKeyword> s_allKeywords;
		std::vector<BoundKeyword> s_worldKeywords;
		bool s_worldActive{ false };
		bool s_summonActive{ false };

		// Thread-safe FriendlyFire override snapshot published on the main thread each
		// tactical tick and queried from HitHook / MagicHook / ProjectileHook / Precision.
		std::shared_mutex s_ffMutex;
		std::unordered_map<std::uint32_t, core::KeywordFlags> s_ffFlags;
		std::atomic<bool> s_hasFfFlags{ false };

		bool ActorHasKeyword(
			RE::Actor* a_actor,
			RE::TESRace* a_race,
			const RE::EffectSetting* a_sourceEffect,
			RE::BGSKeyword* a_keyword)
		{
			return a_actor->HasKeyword(a_keyword) ||
			       (a_race && a_race->HasKeyword(a_keyword)) ||
			       a_actor->HasMagicEffectWithKeyword(a_keyword) ||
			       (a_sourceEffect && a_sourceEffect->HasKeyword(a_keyword));
		}
	}

	void Load()
	{
		s_allKeywords.clear();
		s_worldKeywords.clear();
		s_worldActive = false;
		s_summonActive = false;

		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return;
		}

		std::size_t worldCount = 0;
		std::size_t summonCount = 0;
		for (auto* keyword : handler->GetFormArray<RE::BGSKeyword>()) {
			if (!keyword) {
				continue;
			}
			const char* editorId = keyword->GetFormEditorID();
			const auto parsed = core::ParseKeywordEditorId(editorId ? editorId : "");
			if (!parsed.Recognized()) {
				continue;
			}
			const bool isWorld = core::IsWorldKeywordFlags(parsed.flags);
			const BoundKeyword bound{ keyword, parsed, isWorld };
			s_allKeywords.push_back(bound);
			if (isWorld) {
				s_worldKeywords.push_back(bound);
				++worldCount;
			} else {
				++summonCount;
			}
			const auto* file = keyword->GetFile(0);
			logger::info("[KeywordApi] {} = 0x{:08X} ({}, scope={}).",
				editorId,
				keyword->GetFormID(),
				file ? file->GetFilename() : "unknown plugin",
				isWorld ? "world" : "summon");
		}

		s_worldActive = !s_worldKeywords.empty();
		s_summonActive = !s_allKeywords.empty();
		logger::info("[KeywordApi] {} API keyword record(s) found ({} world, {} summon/parametric); {}.",
			s_allKeywords.size(),
			worldCount,
			summonCount,
			Active() ? "Keyword API v2 active" : "no mod defines SCF_* keywords, Keyword API is idle");
	}

	void Reset()
	{
		std::unique_lock lock(s_ffMutex);
		s_ffFlags.clear();
		s_hasFfFlags.store(false, std::memory_order_release);
	}

	bool Active() noexcept
	{
		return s_worldActive || s_summonActive;
	}

	bool WorldActive() noexcept
	{
		return s_worldActive;
	}

	bool SummonActive() noexcept
	{
		return s_summonActive;
	}

	core::KeywordFlags WorldFlagsOf(RE::Actor* a_actor)
	{
		if (!s_worldActive || !a_actor) {
			return core::kKwNone;
		}
		auto* race = a_actor->GetRace();
		core::KeywordFlags flags = core::kKwNone;
		for (const auto& entry : s_worldKeywords) {
			if (ActorHasKeyword(a_actor, race, nullptr, entry.keyword)) {
				flags |= entry.parsed.flags;
			}
		}
		return flags;
	}

	core::KeywordFlags FlagsOf(RE::Actor* a_actor, const RE::EffectSetting* a_sourceEffect)
	{
		if (!s_summonActive || !a_actor) {
			return core::kKwNone;
		}
		auto* race = a_actor->GetRace();
		core::KeywordFlags flags = core::kKwNone;
		for (const auto& entry : s_allKeywords) {
			if (entry.parsed.flags != core::kKwNone &&
			    ActorHasKeyword(a_actor, race, a_sourceEffect, entry.keyword)) {
				flags |= entry.parsed.flags;
			}
		}
		return flags;
	}

	core::SummonKeywordProfile ProfileOf(RE::Actor* a_actor, const RE::EffectSetting* a_sourceEffect)
	{
		core::SummonKeywordProfile profile{};
		if (!s_summonActive || !a_actor) {
			return profile;
		}
		auto* race = a_actor->GetRace();
		for (const auto& entry : s_allKeywords) {
			if (ActorHasKeyword(a_actor, race, a_sourceEffect, entry.keyword)) {
				core::MergeKeywordIntoProfile(profile, entry.parsed);
			}
		}
		return profile;
	}

	bool SpellHasFlag(const RE::SpellItem* a_spell, core::KeywordFlags a_flag) noexcept
	{
		if (!s_summonActive || !a_spell || a_flag == core::kKwNone) {
			return false;
		}
		try {
			for (const auto* effect : a_spell->effects) {
				const auto* base = effect ? effect->baseEffect : nullptr;
				if (!base) {
					continue;
				}
				const auto* associatedNpc = base->data.associatedForm ? base->data.associatedForm->As<RE::TESNPC>() : nullptr;
				const auto* associatedRace = associatedNpc ? associatedNpc->race : nullptr;

				for (const auto& entry : s_allKeywords) {
					if ((entry.parsed.flags & a_flag) == 0) {
						continue;
					}
					if (base->HasKeyword(entry.keyword)) {
						return true;
					}
					if (associatedNpc &&
					    (associatedNpc->HasKeyword(entry.keyword) ||
					     (associatedRace && associatedRace->HasKeyword(entry.keyword)))) {
						return true;
					}
				}
			}
		} catch (...) {
			return false;
		}
		return false;
	}

	bool SpellHasFreeSlot(const RE::SpellItem* a_spell) noexcept
	{
		return SpellHasFlag(a_spell, core::kKwFreeSlot);
	}

	void PublishFriendlyFireFlags(const std::unordered_map<core::EntityID, core::KeywordFlags>& a_flags)
	{
		std::unique_lock lock(s_ffMutex);
		s_ffFlags.clear();
		for (const auto& [id, flags] : a_flags) {
			const auto masked = flags & core::kKwFriendlyFireMask;
			if (masked != core::kKwNone) {
				s_ffFlags.emplace(static_cast<std::uint32_t>(id), masked);
			}
		}
		s_hasFfFlags.store(!s_ffFlags.empty(), std::memory_order_release);
	}

	core::KeywordFlags FriendlyFireFlagsOf(std::uint32_t a_actorId) noexcept
	{
		if (!s_hasFfFlags.load(std::memory_order_acquire) || a_actorId == 0) {
			return core::kKwNone;
		}
		try {
			std::shared_lock lock(s_ffMutex);
			const auto it = s_ffFlags.find(a_actorId);
			return it != s_ffFlags.end() ? it->second : core::kKwNone;
		} catch (...) {
			return core::kKwNone;
		}
	}

	core::EntityID LeaderOf(RE::Actor* a_actor)
	{
		RE::Actor* current = a_actor;
		for (int depth = 0; current && depth < 4; ++depth) {
			if (current->IsPlayerRef() || current->IsPlayerTeammate()) {
				return 0x14;
			}
			const auto commander = current->GetCommandingActor();
			if (!commander || commander.get() == current) {
				return current->GetFormID();
			}
			current = commander.get();
		}
		return current ? current->GetFormID() : core::InvalidEntityID;
	}

	std::vector<WorldActor> ScanWorld(RE::PlayerCharacter* a_player, bool a_includeWithoutKeywords)
	{
		std::vector<WorldActor> world;
		if ((!s_worldActive && !a_includeWithoutKeywords) || !a_player) {
			return world;
		}
		const auto add = [&](RE::Actor* a_actor) {
			if (!a_actor || a_actor->IsDead() || a_actor->IsDisabled() || !a_actor->Is3DLoaded()) {
				return;
			}
			world.push_back({ a_actor, a_actor->GetFormID(), LeaderOf(a_actor), WorldFlagsOf(a_actor) });
		};
		add(a_player);
		if (auto* lists = RE::ProcessLists::GetSingleton()) {
			world.reserve(lists->highActorHandles.size() + 1);
			for (const auto& handle : lists->highActorHandles) {
				const auto actor = handle.get();
				add(actor.get());
			}
		}
		return world;
	}
}
