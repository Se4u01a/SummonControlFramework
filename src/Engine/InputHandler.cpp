#include "Engine/InputHandler.h"
#include "Core/CommandQueue.h"
#include "Core/FrameworkContext.h"
#include "PCH.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <RE/A/Actor.h>
#include <RE/B/ButtonEvent.h>
#include <RE/C/CrosshairPickData.h>
#include <RE/P/PlayerCharacter.h>
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
		if (!RE::CrosshairPickData::GetSingleton()) {
			return;
		}

		auto* targetActor = ResolveCrosshairActor();

		if (targetActor) {
			if (targetActor->IsDead()) {
				Notify("SCF: Target is dead");
				return;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
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
			}
		}

		return RE::BSEventNotifyControl::kContinue;
	}
}
