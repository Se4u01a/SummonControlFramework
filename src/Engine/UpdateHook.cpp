#include "Engine/UpdateHook.h"
#include "Core/FrameworkContext.h"
#include "PCH.h"

#include <RE/A/ActiveEffect.h>
#include <RE/A/AIProcess.h>
#include <RE/M/MiddleHighProcessData.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/U/UI.h>
#include <unordered_set>

namespace scf::engine::update
{
	namespace
	{
		float s_totalElapsed = 0.0f;

		void InternalSyncPlayerSummons(RE::PlayerCharacter* a_player, float a_timestamp)
		{
			if (!a_player) {
				return;
			}

			auto* currentProcess = a_player->GetActorRuntimeData().currentProcess;
			if (!currentProcess || !currentProcess->middleHigh) {
				return;
			}

			auto& ctx = core::FrameworkContext::GetSingleton();
			auto& registry = ctx.GetRegistry();
			const auto ownerId = a_player->GetFormID();

			const auto& commanded = currentProcess->middleHigh->commandedActors;
			std::unordered_set<core::EntityID> currentEngineSummons;

			for (const auto& data : commanded) {
				auto actorPtr = data.commandedActor.get();
				if (!actorPtr) {
					continue;
				}

				const auto summonId = actorPtr->GetFormID();
				if (actorPtr->IsDead()) {
					registry.NotifyDeath(summonId, a_timestamp);
					continue;
				}

				currentEngineSummons.insert(summonId);

				float remainingDuration = -1.0f;
				if (data.activeEffect) {
					remainingDuration = data.activeEffect->duration - data.activeEffect->elapsedSeconds;
				}

				core::EntityID displaced = core::InvalidEntityID;
				registry.RegisterSummon(
					ownerId,
					summonId,
					core::SummonType::SummonCreature,
					remainingDuration,
					a_timestamp,
					&displaced);

				if (displaced != core::InvalidEntityID) {
					logger::info("[UpdateHook] Summon limit exceeded, displacing oldest summon 0x{:08X}", displaced);
				}
			}

			// Cleanly remove any summons that are no longer commanded by the player
			const auto activeSummons = registry.GetActiveSummonsForOwner(ownerId);
			for (const auto prevSummonId : activeSummons) {
				if (currentEngineSummons.find(prevSummonId) == currentEngineSummons.end()) {
					registry.NotifyEffectRemoved(ownerId, prevSummonId, a_timestamp);
				}
			}

			// Automatically garbage collect dead / dispatched summons after 3 seconds grace period (fadeout)
			registry.ClearStaleRecords(a_timestamp, 3.0f);
		}

		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_player, float a_delta)
			{
				Tick(a_player, a_delta);
				func(a_player, a_delta);
			}

			static void Tick(RE::PlayerCharacter* a_player, float a_delta)
			{
				if (!a_player || !(a_delta > 0.0f)) {
					return;
				}

				auto* ui = RE::UI::GetSingleton();
				if (ui && ui->GameIsPaused()) {
					return;
				}

				s_totalElapsed += a_delta;

				auto& ctx = core::FrameworkContext::GetSingleton();
				if (!ctx.GetSettings().Get().general.enabled) {
					return;
				}

				if (ctx.GetScheduler().ShouldTick(a_delta)) {
					InternalSyncPlayerSummons(a_player, s_totalElapsed);
				}
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void SyncPlayerSummons()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player) {
			InternalSyncPlayerSummons(player, s_totalElapsed);
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = vtbl.write_vfunc(0xAD, PlayerUpdate::thunk);
		logger::info("[UpdateHook] PlayerCharacter::Update hooked successfully at vfunc 0xAD.");
	}

	void Reset()
	{
		s_totalElapsed = 0.0f;
		core::FrameworkContext::GetSingleton().ResetRuntimeState();
	}
}
