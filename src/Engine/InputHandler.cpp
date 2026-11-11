#include "Engine/InputHandler.h"
#include "Core/CommandQueue.h"
#include "Core/FrameworkContext.h"
#include "Integrations/MinionPanel.h"
#include "PCH.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <RE/A/Actor.h>
#include <RE/B/ButtonEvent.h>
#include <RE/C/CrosshairPickData.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/ProcessLists.h>
#include <RE/S/SendHUDMessage.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/U/UI.h>
#include <SKSE/InputMap.h>

namespace scf::engine
{
	namespace
	{
		// Aim-projection fallback for MoveTo: when the crosshair is over terrain the
		// interaction pick leaves `collisionPoint` at zero, so project the aim onto the
		// ground plane instead. |aim angle| keeps this independent of the pitch sign.
		constexpr float kEyeHeight = 110.0f;
		constexpr float kAimFlatDistance = 500.0f;
		constexpr float kAimMinDistance = 200.0f;
		constexpr float kAimMaxDistance = 2500.0f;

		void Notify(const char* a_msg)
		{
			if (a_msg && a_msg[0]) {
				RE::SendHUDMessage::ShowHUDMessage(a_msg);
				logger::info("[InputHandler] {}", a_msg);
			}
		}

		bool IsSaneWorldPoint(const RE::NiPoint3& a_point) noexcept
		{
			return std::isfinite(a_point.x) && std::isfinite(a_point.y) && std::isfinite(a_point.z) &&
			       !(a_point.x == 0.0f && a_point.y == 0.0f && a_point.z == 0.0f) &&
			       std::abs(a_point.x) < 200000.0f && std::abs(a_point.y) < 200000.0f;
		}

		// CrosshairPickData carries two handles: `target` is what the interaction
		// (activate) pick found, `targetActor` is what the aim pick found. CommonLib's
		// GetActiveTarget() only exposes the first, which is why an enemy across the
		// room read as "no target". Resolve the actor first, then the plain reference.
		RE::TESObjectREFR* ResolveCrosshairRef()
		{
			auto* pick = RE::CrosshairPickData::GetSingleton();
			if (!pick) {
				return nullptr;
			}

		#if defined(EXCLUSIVE_SKYRIM_FLAT)
			const RE::ObjectRefHandle handles[]{ pick->targetActor, pick->target };
		#else
			const RE::ObjectRefHandle handles[]{ pick->targetActor[0], pick->target[0] };
		#endif
			for (const auto& handle : handles) {
				auto ref = handle.get();
				if (ref) {
					return ref.get();
				}
			}
			return nullptr;
		}

		RE::Actor* ResolveCrosshairActor()
		{
			if (auto* ref = ResolveCrosshairRef()) {
				return ref->As<RE::Actor>();
			}
			return nullptr;
		}

		bool ResolveCrosshairPoint(RE::NiPoint3& a_outPoint)
		{
			auto* pick = RE::CrosshairPickData::GetSingleton();
			if (!pick) {
				return false;
			}

		#if defined(EXCLUSIVE_SKYRIM_FLAT)
			const auto& point = pick->collisionPoint;
		#else
			const auto& point = pick->collisionPoint[0];
		#endif
			if (!IsSaneWorldPoint(point)) {
				return false;
			}

			a_outPoint = point;
			return true;
		}

		void LogCrosshairState(const char* a_action)
		{
			auto* pick = RE::CrosshairPickData::GetSingleton();
			if (!pick) {
				logger::warn("[Crosshair] {}: CrosshairPickData singleton is null", a_action);
				return;
			}

		#if defined(EXCLUSIVE_SKYRIM_FLAT)
			const auto target = pick->target.get();
			const auto targetActor = pick->targetActor.get();
			const auto& point = pick->collisionPoint;
			const auto* resolved = ResolveCrosshairRef();

			logger::info("[Crosshair] {}: target=0x{:08X} targetActor=0x{:08X} ref=0x{:08X} point=({:.1f}, {:.1f}, {:.1f})",
				a_action,
				target ? target->GetFormID() : 0u,
				targetActor ? targetActor->GetFormID() : 0u,
				resolved ? resolved->GetFormID() : 0u,
				point.x, point.y, point.z);
		#else
			logger::info("[Crosshair] {}: non-flat build, raw dump omitted", a_action);
		#endif
		}

		// Passive is absolute (CombatPolicy denies every fight), so a Focus order would
		// charge a summon that is forbidden to swing. Say so instead of a false success.
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

		// Crosshair data is empty whenever the pick has not produced a reference (the
		// interaction pick only fills it for references, and it can lag a frame behind
		// fast aiming). Fall back to the closest hostile actor inside a narrow cone in
		// front of the player, so Focus Target stays usable.
		RE::Actor* FindAimConeTarget(RE::PlayerCharacter* a_player)
		{
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists || !a_player) {
				return nullptr;
			}

			const auto playerPos = a_player->GetPosition();
			const float yaw = a_player->GetAngleZ();
			const float forwardX = std::sin(yaw);
			const float forwardY = std::cos(yaw);
			constexpr float kMinDot = 0.9063f; // cos(25 degrees): narrow aim cone
			constexpr float kMaxDistance = 2500.0f;
			constexpr float kMaxHeightDelta = 600.0f;

			RE::Actor* best = nullptr;
			float bestScore = 1.0e30f;

			for (const auto& handle : lists->highActorHandles) {
				auto actorPtr = handle.get();
				if (!actorPtr || actorPtr.get() == a_player) {
					continue;
				}

				auto* actor = actorPtr.get();
				if (!actor || actor->IsDead() || !actor->Is3DLoaded() || !actor->IsHostileToActor(a_player)) {
					continue;
				}

				const auto pos = actor->GetPosition();
				const float dx = pos.x - playerPos.x;
				const float dy = pos.y - playerPos.y;
				const float dz = pos.z - playerPos.z;
				const float horizontal = std::sqrt(dx * dx + dy * dy);
				if (horizontal < 1.0f || horizontal > kMaxDistance || std::abs(dz) > kMaxHeightDelta) {
					continue;
				}

				const float dot = (dx * forwardX + dy * forwardY) / horizontal;
				if (dot < kMinDot) {
					continue;
				}

				const float score = horizontal * (2.0f - dot); // closer and better centred wins
				if (score < bestScore) {
					bestScore = score;
					best = actor;
				}
			}

			return best;
		}
	}

	void InputHandler::Register()
	{
		auto* deviceManager = RE::BSInputDeviceManager::GetSingleton();
		if (deviceManager) {
			deviceManager->AddEventSink(GetSingleton());
			logger::info("[InputHandler] Registered input event sink successfully.");
		} else {
			logger::error("[InputHandler] Failed to acquire BSInputDeviceManager singleton!");
		}
	}

	void InputHandler::HandleFocusTarget()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		if (SquadIsPassive(player)) {
			Notify("SCF: Passive mode - attack suppressed");
			return;
		}

		auto* targetActor = ResolveCrosshairActor();
		if (!targetActor) {
			targetActor = FindAimConeTarget(player);
			if (targetActor) {
				logger::info("[Crosshair] crosshair empty; aim-cone fallback selected 0x{:08X} ({})",
					targetActor->GetFormID(), targetActor->GetName());
			}
		}

		if (targetActor) {
			if (targetActor->IsDead()) {
				Notify("SCF: Target is dead");
				return;
			}

			if (targetActor == player) {
				Notify("SCF: Cannot target owner");
				return;
			}

			const auto targetId = targetActor->GetFormID();
			core::Command cmd{};
			cmd.type = core::CommandType::SetFocusTarget;
			cmd.priority = core::CommandPriority::PlayerDirect;
			cmd.summonId = core::InvalidEntityID;
			cmd.focusTargetId = targetId;
			cmd.timestamp = core::FrameworkContext::GetSingleton().GetScheduler().GetTotalElapsedTime();

			core::FrameworkContext::GetSingleton().GetCommandQueue().Enqueue(cmd);
			core::FrameworkContext::GetSingleton().GetScheduler().RequestImmediateTick();

			const char* name = targetActor->GetName();
			std::string msg = "SCF: Target Focused -> ";
			msg += (name && name[0]) ? name : "Target";
			Notify(msg.c_str());
		} else {
			Notify("SCF: No target under crosshair");
			LogCrosshairState("FocusTarget");
		}
	}

	void InputHandler::HandleReturnOrder()
	{
		core::Command cmd{};
		cmd.type = core::CommandType::SetOrder;
		cmd.order = core::SummonOrder::Return;
		cmd.priority = core::CommandPriority::PlayerDirect;
		cmd.summonId = core::InvalidEntityID;
		cmd.timestamp = core::FrameworkContext::GetSingleton().GetScheduler().GetTotalElapsedTime();

		core::FrameworkContext::GetSingleton().GetCommandQueue().Enqueue(cmd);
		core::FrameworkContext::GetSingleton().GetScheduler().RequestImmediateTick();

		Notify("SCF: Order -> Return to Owner");
	}

	void InputHandler::HandleToggleAggression()
	{
		static core::AggressionMode currentMode = core::AggressionMode::Defensive;
		switch (currentMode) {
		case core::AggressionMode::Defensive:
			currentMode = core::AggressionMode::Aggressive;
			break;
		case core::AggressionMode::Aggressive:
			currentMode = core::AggressionMode::Passive;
			break;
		case core::AggressionMode::Passive:
		default:
			currentMode = core::AggressionMode::Defensive;
			break;
		}

		core::Command cmd{};
		cmd.type = core::CommandType::SetAggression;
		cmd.aggression = currentMode;
		cmd.priority = core::CommandPriority::ModeChange;
		cmd.summonId = core::InvalidEntityID;
		cmd.timestamp = core::FrameworkContext::GetSingleton().GetScheduler().GetTotalElapsedTime();

		core::FrameworkContext::GetSingleton().GetCommandQueue().Enqueue(cmd);
		core::FrameworkContext::GetSingleton().GetScheduler().RequestImmediateTick();

		std::string msg = "SCF: Aggression Mode -> ";
		msg += core::ToString(currentMode);
		Notify(msg.c_str());
	}

	void InputHandler::HandleMoveTo()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		core::Vec3 targetPos{};
		const char* source = "none";
		bool found = false;

		RE::NiPoint3 point{};
		if (ResolveCrosshairPoint(point)) {
			targetPos = { point.x, point.y, point.z };
			source = "crosshair collision point";
			found = true;
		} else if (auto* ref = ResolveCrosshairRef()) {
			const auto refPos = ref->GetPosition();
			if (IsSaneWorldPoint(refPos)) {
				targetPos = { refPos.x, refPos.y, refPos.z };
				source = "crosshair reference";
				found = true;
			}
		}

		if (!found) {
			// Terrain under the crosshair leaves the pick data without a collision point
			// (the interaction pick fills it only for references), so project the aim
			// onto the ground plane instead of guessing "straight ahead".
			const float yaw = player->GetAngleZ();
			const float pitch = std::abs(player->GetAimAngle());

			float distance = kAimFlatDistance;
			if (pitch > 0.05f) {
				distance = kEyeHeight / std::tan(pitch);
			}
			distance = std::clamp(distance, kAimMinDistance, kAimMaxDistance);

			const auto playerPos = player->GetPosition();
			targetPos = {
				playerPos.x + std::sin(yaw) * distance,
				playerPos.y + std::cos(yaw) * distance,
				playerPos.z
			};
			source = "aim projection";
		}

		logger::info("[InputHandler] MoveTo destination from {}: ({:.0f}, {:.0f}, {:.0f}) [aimAngle={:.2f} rad, distance-to-player={:.0f}]",
			source, targetPos.x, targetPos.y, targetPos.z, player->GetAimAngle(),
			core::Vec3{ player->GetPosition().x, player->GetPosition().y, player->GetPosition().z }.DistanceTo(targetPos));

		core::Command cmd{};
		cmd.type = core::CommandType::SetOrder;
		cmd.order = core::SummonOrder::MoveTo;
		cmd.priority = core::CommandPriority::PlayerDirect;
		cmd.summonId = core::InvalidEntityID;
		cmd.targetPosition = targetPos;
		cmd.timestamp = core::FrameworkContext::GetSingleton().GetScheduler().GetTotalElapsedTime();

		core::FrameworkContext::GetSingleton().GetCommandQueue().Enqueue(cmd);
		core::FrameworkContext::GetSingleton().GetScheduler().RequestImmediateTick();

		char buf[160];
		std::snprintf(buf, sizeof(buf), "SCF: Move To Point (%.0f, %.0f, %.0f)", targetPos.x, targetPos.y, targetPos.z);
		Notify(buf);
	}

	void InputHandler::HandleWaitGuard()
	{
		core::Command cmd{};
		cmd.type = core::CommandType::SetOrder;
		cmd.order = core::SummonOrder::WaitGuard;
		cmd.priority = core::CommandPriority::PlayerDirect;
		cmd.summonId = core::InvalidEntityID;
		cmd.timestamp = core::FrameworkContext::GetSingleton().GetScheduler().GetTotalElapsedTime();

		core::FrameworkContext::GetSingleton().GetCommandQueue().Enqueue(cmd);
		core::FrameworkContext::GetSingleton().GetScheduler().RequestImmediateTick();

		Notify("SCF: Order -> Guard Position");
	}

	void InputHandler::HandleFollow()
	{
		core::Command cmd{};
		cmd.type = core::CommandType::SetOrder;
		cmd.order = core::SummonOrder::Follow;
		cmd.priority = core::CommandPriority::PlayerDirect;
		cmd.summonId = core::InvalidEntityID;
		cmd.timestamp = core::FrameworkContext::GetSingleton().GetScheduler().GetTotalElapsedTime();

		core::FrameworkContext::GetSingleton().GetCommandQueue().Enqueue(cmd);
		core::FrameworkContext::GetSingleton().GetScheduler().RequestImmediateTick();

		Notify("SCF: Order -> Follow Owner");
	}

	void InputHandler::HandleCommandAction()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		// The aimed enemy, never the owner or one of the owner's own summons (the
		// crosshair often rests on a summon in front of the player).
		auto& registry = core::FrameworkContext::GetSingleton().GetRegistry();
		auto* target = ResolveCrosshairActor();
		if (target && (target == player || target->IsDead() || registry.IsRegistered(target->GetFormID()))) {
			target = nullptr;
		}
		if (!target) {
			target = FindAimConeTarget(player);
		}

		core::Command cmd{};
		cmd.type = core::CommandType::CommandAction;
		cmd.priority = core::CommandPriority::PlayerDirect;
		cmd.summonId = core::InvalidEntityID;
		cmd.focusTargetId = target ? target->GetFormID() : core::InvalidEntityID;
		cmd.timestamp = core::FrameworkContext::GetSingleton().GetScheduler().GetTotalElapsedTime();

		core::FrameworkContext::GetSingleton().GetCommandQueue().Enqueue(cmd);
		core::FrameworkContext::GetSingleton().GetScheduler().RequestImmediateTick();

		// The per-summon result (rush / cast / cooldown) is shown by the tick that executes it.
		logger::info("[InputHandler] Command action requested at 0x{:08X} ({})",
			target ? target->GetFormID() : 0u, target ? target->GetName() : "no target");
	}

	RE::BSEventNotifyControl InputHandler::ProcessEvent(
		RE::InputEvent* const* a_event,
		RE::BSTEventSource<RE::InputEvent*>*)
	{
		if (!a_event || !*a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		auto* ui = RE::UI::GetSingleton();
		if (ui && ui->GameIsPaused()) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const auto& settings = core::FrameworkContext::GetSingleton().GetSettings().Get();
		if (!settings.general.enabled) {
			return RE::BSEventNotifyControl::kContinue;
		}

		for (auto* event = *a_event; event; event = event->next) {
			if (event->eventType != RE::INPUT_EVENT_TYPE::kButton) {
				continue;
			}

			auto* button = event->AsButtonEvent();
			if (!button || !button->IsDown()) {
				continue;
			}

			// Keyboard settings hold DirectX scan codes; mouse settings hold the same
			// offset codes FLICK binds (SKSE's macro mouse offset), so the FLICK menu can
			// bind either device and the comparison stays in one number space.
			std::uint32_t key = 0;
			if (button->device == RE::INPUT_DEVICE::kKeyboard) {
				key = button->GetIDCode();
			} else if (button->device == RE::INPUT_DEVICE::kMouse) {
				key = button->GetIDCode() + SKSE::InputMap::kMacro_MouseButtonOffset;
			} else {
				continue;
			}

			if (key == settings.hotkeys.focusTargetKey) {
				HandleFocusTarget();
			} else if (key == settings.hotkeys.commandReturnKey) {
				HandleReturnOrder();
			} else if (key == settings.hotkeys.toggleAggressionKey) {
				HandleToggleAggression();
			} else if (key == settings.hotkeys.moveToKey) {
				HandleMoveTo();
			} else if (key == settings.hotkeys.waitGuardKey) {
				HandleWaitGuard();
			} else if (key == settings.hotkeys.followKey) {
				HandleFollow();
			} else if (key == settings.hotkeys.commandActionKey) {
				HandleCommandAction();
			} else if (settings.hotkeys.minionPanelToggleKey != 0 && key == settings.hotkeys.minionPanelToggleKey) {
				const bool show = !integrations::MinionPanel::IsEnabled();
				integrations::MinionPanel::SetEnabled(show);
				Notify(show ? "SCF: Minion panel shown" : "SCF: Minion panel hidden");
			}
		}

		return RE::BSEventNotifyControl::kContinue;
	}
}
