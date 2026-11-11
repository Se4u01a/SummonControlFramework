#pragma once

#include "Core/FrameworkContext.h"
#include "FriendlyFire/RelationshipClassifier.h"

#include <RE/A/Actor.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESForm.h>

// Engine-side questions the friendly-fire seats ask about a hit that is already being
// processed: "who hit whom, and is the victim ours to protect". Kept in one place so the
// damage seats (HitHook, PrecisionBackend) and the reaction seats (KillmoveHook,
// HitReactionHook) answer identically - a hit that blocks damage must also be the hit
// that suppresses a killmove, and vice versa.
namespace scf::friendly_fire::query
{
	// PlayerCharacter's form id, or the vanilla player id when the player is not there
	// yet. Thread safe: GetSingleton is a plain pointer read.
	[[nodiscard]] inline core::EntityID PlayerId() noexcept
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		return player ? player->GetFormID() : 0x14;
	}

	// A player teammate ("ally") by the engine's own flag, the same test HitHook uses.
	[[nodiscard]] inline bool IsAllyActor(core::EntityID a_actorId)
	{
		auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorId);
		return actor && actor->IsPlayerTeammate();
	}

	[[nodiscard]] inline core::EntityID CommandingActorOf(core::EntityID a_actorId)
	{
		auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorId);
		if (!actor) {
			return core::InvalidEntityID;
		}
		auto* commander = actor->GetCommandingActor().get();
		return commander ? commander->GetFormID() : core::InvalidEntityID;
	}

	// AttackSourceKind of an attacker pointer, through the shared classifier (registry +
	// engine commanding chain + teammate flag). Unknown when the attacker is not an actor:
	// the classifier's fail-safe keeps every unrecognised source unfiltered.
	[[nodiscard]] inline AttackSourceKind ClassifySourceActor(const RE::TESForm* a_source) noexcept
	{
		auto* actor = a_source && a_source->Is(RE::FormType::ActorCharacter) ?
		                  static_cast<const RE::Actor*>(a_source) :
		                  nullptr;
		const auto sourceId = actor ? actor->GetFormID() : core::InvalidEntityID;
		return RelationshipClassifier::ClassifySource(
			sourceId,
			core::FrameworkContext::GetSingleton().GetRegistry(),
			PlayerId(),
			IsAllyActor,
			CommandingActorOf);
	}

	// True when the friendly-fire matrix considers this actor a protected summon: either
	// registered in SCF's SummonRegistry, or - for a summon created a moment ago and not
	// registered yet - commanded by the player (or by a player teammate when allies are
	// protected). Mirrors HitHook's dynamic fallback so the reaction seats cannot drift.
	[[nodiscard]] inline bool IsProtectedActor(const RE::Actor* a_target, const FriendlyFireSettings& a_settings)
	{
		if (!a_target || !a_settings.enabled) {
			return false;
		}

		const auto playerId = PlayerId();
		if (RelationshipClassifier::IsProtectedTarget(
				a_target->GetFormID(),
				core::FrameworkContext::GetSingleton().GetRegistry(),
				a_settings,
				playerId,
				IsAllyActor)) {
			return true;
		}

		auto commanding = a_target->GetCommandingActor();
		if (!commanding) {
			return false;
		}
		const auto commanderId = commanding->GetFormID();
		if (commanderId == playerId) {
			return a_settings.protectPlayerOwnedSummons;
		}
		return a_settings.protectAllyOwnedSummons && IsAllyActor(commanderId);
	}
}
