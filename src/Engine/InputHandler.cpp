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
		if (auto ref = crosshair->GetActiveTarget().get()) {
			targetActor = ref->As<RE::Actor>();
		}

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
		auto* crosshair = RE::CrosshairPickData::GetSingleton();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		core::Vec3 targetPos{};
		bool foundPos = false;

		if (crosshair) {
			if (auto ref = crosshair->GetActiveTarget().get()) {
				const auto pos = ref->GetPosition();
				if (std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z) &&
				    std::abs(pos.x) < 200000.0f && std::abs(pos.y) < 200000.0f) {
					targetPos = { pos.x, pos.y, pos.z };
					foundPos = true;
				}
			}
		}

		if (!foundPos) {
			// 500 units ahead of player in facing yaw direction
			const float angle = player->GetAngleZ();
			const auto pPos = player->GetPosition();
			targetPos.x = pPos.x + 500.0f * std::sin(angle);
			targetPos.y = pPos.y + 500.0f * std::cos(angle);
			targetPos.z = pPos.z;
		}

		core::Command cmd{};
		cmd.type = core::CommandType::SetOrder;
		cmd.order = core::SummonOrder::MoveTo;
		cmd.priority = core::CommandPriority::PlayerDirect;
		cmd.summonId = core::InvalidEntityID;
		cmd.targetPosition = targetPos;
		cmd.timestamp = core::FrameworkContext::GetSingleton().GetScheduler().GetTotalElapsedTime();

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

			if (button->device != RE::INPUT_DEVICE::kKeyboard) {
				continue;
			}

			const auto key = button->GetIDCode();

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
