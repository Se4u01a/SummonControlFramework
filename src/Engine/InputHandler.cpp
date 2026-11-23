#include "Engine/InputHandler.h"
#include "Core/FrameworkContext.h"
#include "Integrations/MinionPanel.h"
#include "PCH.h"

#include <RE/B/ButtonEvent.h>
#include <RE/S/SendHUDMessage.h>
#include <RE/U/UI.h>
#include <SKSE/InputMap.h>

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
		if (!settings.general.enabled || settings.hotkeys.minionPanelToggleKey == 0) {
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

			if (key == settings.hotkeys.minionPanelToggleKey) {
				const bool show = !integrations::MinionPanel::IsEnabled();
				integrations::MinionPanel::SetEnabled(show);
				Notify(show ? "SCF: Minion panel shown" : "SCF: Minion panel hidden");
			}
		}

		return RE::BSEventNotifyControl::kContinue;
	}
}
