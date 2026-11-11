#include "Engine/KeywordApi.h"
#include "Core/KeywordApi.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/B/BGSKeyword.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/ProcessLists.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESRace.h>

#include <array>
#include <vector>

namespace scf::engine::keywords
{
	namespace
	{
		struct FlagKeywords
		{
			std::uint8_t flag{ 0 };
			std::vector<RE::BGSKeyword*> keywords;
		};

		std::array<FlagKeywords, 3> s_flags{ { { core::kPassThroughFriends, {} }, { core::kPassThroughAll, {} }, { core::kUntargetable, {} } } };
		bool s_active{ false };

		bool HasAny(RE::Actor* a_actor, const std::vector<RE::BGSKeyword*>& a_keywords)
		{
			auto* race = a_actor->GetRace();
			for (auto* keyword : a_keywords) {
				if (a_actor->HasKeyword(keyword) || (race && race->HasKeyword(keyword)) || a_actor->HasMagicEffectWithKeyword(keyword)) {
					return true;
				}
			}
			return false;
		}
	}

	void Load()
	{
		for (auto& entry : s_flags) {
			entry.keywords.clear();
		}
		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return;
		}
		std::size_t found = 0;
		for (auto* keyword : handler->GetFormArray<RE::BGSKeyword>()) {
			if (!keyword) {
				continue;
			}
			const char* editorId = keyword->GetFormEditorID();
			const auto flag = core::FlagForKeywordEditorId(editorId ? editorId : "");
			if (flag == 0) {
				continue;
			}
			for (auto& entry : s_flags) {
				if (entry.flag == flag) {
					entry.keywords.push_back(keyword);
				}
			}
			++found;
			const auto* file = keyword->GetFile(0);
			logger::info("[KeywordApi] {} = 0x{:08X} ({}).", editorId, keyword->GetFormID(), file ? file->GetFilename() : "unknown plugin");
		}
		s_active = found > 0;
		logger::info("[KeywordApi] {} API keyword record(s) found; {}.", found,
			s_active ? "actors are scanned for SCF_PassThroughFriends / SCF_PassThroughAll / SCF_Untargetable" :
					   "no mod defines them, the keyword API is idle");
	}

	bool Active() noexcept
	{
		return s_active;
	}

	std::uint8_t FlagsOf(RE::Actor* a_actor)
	{
		if (!s_active || !a_actor) {
			return 0;
		}
		std::uint8_t flags = 0;
		for (const auto& entry : s_flags) {
			if (!entry.keywords.empty() && HasAny(a_actor, entry.keywords)) {
				flags |= entry.flag;
			}
		}
		return flags;
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

	std::vector<WorldActor> ScanWorld(RE::PlayerCharacter* a_player)
	{
		std::vector<WorldActor> world;
		if (!s_active || !a_player) {
			return world;
		}
		const auto add = [&](RE::Actor* a_actor) {
			if (!a_actor || a_actor->IsDead() || a_actor->IsDisabled() || !a_actor->Is3DLoaded()) {
				return;
			}
			world.push_back({ a_actor, a_actor->GetFormID(), LeaderOf(a_actor), FlagsOf(a_actor) });
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
