#pragma once

#include "Core/EntityID.h"
#include "Core/KeywordApi.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace RE
{
	class Actor;
	class EffectSetting;
	class PlayerCharacter;
	class SpellItem;
}

namespace scf::engine::keywords
{
	// Keyword API v2 (see Core/KeywordApi.h and docs/KEYWORDS.md).
	// Load() runs at kDataLoaded: every BGSKeyword whose EditorID is recognized by
	// core::ParseKeywordEditorId (any plugin, several records per name allowed) is bound
	// once at startup. If no mod defines any SCF_* keyword, Active() is false and nothing
	// is scanned.
	void Load();
	void Reset();

	[[nodiscard]] bool Active() noexcept;
	[[nodiscard]] bool WorldActive() noexcept;
	[[nodiscard]] bool SummonActive() noexcept;

	// Main thread. World-scoped flags of any actor (PassThrough, Threat, FriendlyFire,
	// AutoRegister): keyword on base NPC_, RACE, or active magic effect MGEF.
	[[nodiscard]] core::KeywordFlags WorldFlagsOf(RE::Actor* a_actor);

	// Main thread. Full flag bitmask of an actor (optionally including a conjuration
	// source MGEF on the commander).
	[[nodiscard]] core::KeywordFlags FlagsOf(RE::Actor* a_actor, const RE::EffectSetting* a_sourceEffect = nullptr);

	// Main thread. Full aggregated keyword profile (flags + numeric parameters) of a
	// summon, reading NPC_, RACE, active MGEFs on the actor, and the conjuration source
	// MGEF if provided.
	[[nodiscard]] core::SummonKeywordProfile ProfileOf(RE::Actor* a_actor, const RE::EffectSetting* a_sourceEffect = nullptr);

	// Checks whether any effect of `a_spell` (or its associated summoned NPC_/RACE) carries
	// any keyword matching `a_flag`. Safe to call from main/cast hooks (e.g. SummonLimit
	// and Persistence).
	[[nodiscard]] bool SpellHasFlag(const RE::SpellItem* a_spell, core::KeywordFlags a_flag) noexcept;

	// Convenience helper for SummonLimit::LimitThunk (SCF_FreeSlot / SCF_IgnoreLimit).
	[[nodiscard]] bool SpellHasFreeSlot(const RE::SpellItem* a_spell) noexcept;

	// Main thread -> ANY THREAD snapshot for FriendlyFire hooks
	// (kKwFriendlyFireProtected | kKwAllowFriendlyFire).
	void PublishFriendlyFireFlags(const std::unordered_map<core::EntityID, core::KeywordFlags>& a_flags);
	[[nodiscard]] core::KeywordFlags FriendlyFireFlagsOf(std::uint32_t a_actorId) noexcept;

	// Main thread. Head of the actor's side: the player for the player and the player's
	// teammates, a commanded actor's (summon's) commander's leader, else the actor.
	[[nodiscard]] core::EntityID LeaderOf(RE::Actor* a_actor);

	// One loaded actor as the world keyword rules see it this tick.
	struct WorldActor
	{
		RE::Actor* actor{ nullptr };
		core::EntityID id{ core::InvalidEntityID };
		core::EntityID leader{ core::InvalidEntityID };
		core::KeywordFlags flags{ core::kKwNone };
	};

	// Main thread, once per tactical tick: the player and every live, 3D-loaded actor of
	// the high process list with its world flags and leader. Empty while world scanning
	// is idle and `a_includeWithoutKeywords` is false.
	[[nodiscard]] std::vector<WorldActor> ScanWorld(RE::PlayerCharacter* a_player, bool a_includeWithoutKeywords = false);
}
