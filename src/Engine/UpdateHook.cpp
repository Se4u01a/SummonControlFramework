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
		void InternalSyncPlayerSummons(RE::PlayerCharacter* a_player)
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
					registry.NotifyDeath(summonId);
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
					0.0f,
					&displaced);

				if (displaced != core::InvalidEntityID) {
					logger::info("[UpdateHook] Summon limit exceeded, displacing oldest summon 0x{:08X}", displaced);
				}
			}

			// Cleanly remove any summons that are no longer commanded by the player
			const auto activeSummons = registry.GetActiveSummonsForOwner(ownerId);
			for (const auto prevSummonId : activeSummons) {
				if (currentEngineSummons.find(prevSummonId) == currentEngineSummons.end()) {
					registry.NotifyEffectRemoved(ownerId, prevSummonId);
				}
			}
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

				auto& ctx = core::FrameworkContext::GetSingleton();
				if (!ctx.GetSettings().Get().general.enabled) {
					return;
				}

				if (ctx.GetScheduler().ShouldTick(a_delta)) {
					InternalSyncPlayerSummons(a_player);
				}
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void SyncPlayerSummons()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player) {
			InternalSyncPlayerSummons(player);
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
		core::FrameworkContext::GetSingleton().ResetRuntimeState();
	}
}
