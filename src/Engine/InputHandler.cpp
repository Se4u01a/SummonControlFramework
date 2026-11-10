#include "Engine/InputHandler.h"
#include "Core/CommandQueue.h"
#include "Core/FrameworkContext.h"
#include "PCH.h"

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

namespace scf::engine
{
	namespace
	{
		void Notify(const char* a_msg)
		{
			if (a_msg && a_msg[0]) {
				RE::SendHUDMessage::ShowHUDMessage(a_msg);
				logger::info("[InputHandler] {}", a_msg);
			}
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
		auto* crosshair = RE::CrosshairPickData::GetSingleton();
		if (!crosshair) {
			return;
		}

		RE::Actor* targetActor = nullptr;
#if defined(EXCLUSIVE_SKYRIM_FLAT)
		if (auto ref = crosshair->targetActor.get()) {
			targetActor = ref->As<RE::Actor>();
		}
		if (!targetActor) {
			if (auto ref = crosshair->target.get()) {
				targetActor = ref->As<RE::Actor>();
			}
		}
#else
		if (auto ref = crosshair->targetActor[0].get()) {
			targetActor = ref->As<RE::Actor>();
		}
		if (!targetActor) {
			if (auto ref = crosshair->target[0].get()) {
				targetActor = ref->As<RE::Actor>();
			}
		}
#endif

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

			core::FrameworkContext::GetSingleton().GetCommandQueue().Enqueue(cmd);
			core::FrameworkContext::GetSingleton().GetScheduler().RequestImmediateTick();

			const char* name = targetActor->GetName();
			std::string msg = "SCF: Target Focused -> ";
			msg += (name && name[0]) ? name : "Target";
			Notify(msg.c_str());
		} else {
			Notify("SCF: No target under crosshair");
		}
	}

	void InputHandler::HandleReturnOrder()
	{
		core::Command cmd{};
		cmd.type = core::CommandType::SetOrder;
		cmd.order = core::SummonOrder::Return;
		cmd.priority = core::CommandPriority::PlayerDirect;
		cmd.summonId = core::InvalidEntityID;

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

		core::FrameworkContext::GetSingleton().GetCommandQueue().Enqueue(cmd);
		core::FrameworkContext::GetSingleton().GetScheduler().RequestImmediateTick();

		std::string msg = "SCF: Aggression Mode -> ";
		msg += core::ToString(currentMode);
		Notify(msg.c_str());
	}

	void InputHandler::HandleMoveTo()
	{
		auto* crosshair = RE::CrosshairPickData::GetSingleton();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		core::Vec3 targetPos{};
		bool foundPos = false;

		if (crosshair) {
#if defined(EXCLUSIVE_SKYRIM_FLAT)
			const auto& pt = crosshair->collisionPoint;
#else
			const auto& pt = crosshair->collisionPoint[0];
#endif
			if (std::abs(pt.x) > 0.1f || std::abs(pt.y) > 0.1f || std::abs(pt.z) > 0.1f) {
				targetPos = { pt.x, pt.y, pt.z };
				foundPos = true;
			}
		}

		if (!foundPos) {
			// Fallback: 600 units ahead of player in facing yaw direction
			const float angle = player->GetAngleZ();
			const auto pPos = player->GetPosition();
			targetPos.x = pPos.x + 600.0f * std::sin(angle);
			targetPos.y = pPos.y + 600.0f * std::cos(angle);
			targetPos.z = pPos.z;
		}

		core::Command cmd{};
		cmd.type = core::CommandType::SetOrder;
		cmd.order = core::SummonOrder::MoveTo;
		cmd.priority = core::CommandPriority::PlayerDirect;
		cmd.summonId = core::InvalidEntityID;
		cmd.targetPosition = targetPos;

		core::FrameworkContext::GetSingleton().GetCommandQueue().Enqueue(cmd);
		core::FrameworkContext::GetSingleton().GetScheduler().RequestImmediateTick();

		char buf[128];
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

		// Defaults: Numpad 1=0x4F, Numpad 2=0x50, Numpad 3=0x51, Numpad 4=0x4B, Numpad 5=0x4C, Numpad 6=0x4D
		const std::uint32_t keyFocus = settings.hotkeys.focusTargetKey != 0 ? settings.hotkeys.focusTargetKey : 0x4F;
		const std::uint32_t keyReturn = settings.hotkeys.commandReturnKey != 0 ? settings.hotkeys.commandReturnKey : 0x50;
		const std::uint32_t keyAggression = settings.hotkeys.toggleAggressionKey != 0 ? settings.hotkeys.toggleAggressionKey : 0x51;
		const std::uint32_t keyMoveTo = settings.hotkeys.moveToKey != 0 ? settings.hotkeys.moveToKey : 0x4B;
		const std::uint32_t keyWaitGuard = settings.hotkeys.waitGuardKey != 0 ? settings.hotkeys.waitGuardKey : 0x4C;
		const std::uint32_t keyFollow = settings.hotkeys.followKey != 0 ? settings.hotkeys.followKey : 0x4D;

		for (auto* event = *a_event; event; event = event->next) {
			if (event->eventType != RE::INPUT_EVENT_TYPE::kButton) {
				continue;
			}

			auto* button = event->AsButtonEvent();
			if (!button || !button->IsDown()) {
				continue;
			}

			const auto idCode = button->GetIDCode();
			const auto device = button->GetDevice();

			if (device == RE::INPUT_DEVICE::kKeyboard) {
				if (idCode == keyFocus) {
					HandleFocusTarget();
				} else if (idCode == keyReturn) {
					HandleReturnOrder();
				} else if (idCode == keyAggression) {
					HandleToggleAggression();
				} else if (idCode == keyMoveTo) {
					HandleMoveTo();
				} else if (idCode == keyWaitGuard) {
					HandleWaitGuard();
				} else if (idCode == keyFollow) {
					HandleFollow();
				}
			}
		}

		return RE::BSEventNotifyControl::kContinue;
	}
}
