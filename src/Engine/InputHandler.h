#pragma once

#include <RE/B/BSInputDeviceManager.h>
#include <RE/B/BSTEvent.h>
#include <RE/I/InputEvent.h>

namespace scf::engine
{
	class InputHandler : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputHandler* GetSingleton()
		{
			static InputHandler singleton;
			return &singleton;
		}

		static void Register();

		RE::BSEventNotifyControl ProcessEvent(
			RE::InputEvent* const* a_event,
			RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

		void HandleFocusTarget();
		void HandleReturnOrder();
		void HandleToggleAggression();
		void HandleMoveTo();
		void HandleWaitGuard();
		void HandleFollow();
		void HandleCommandAction();

	private:
		InputHandler() = default;
		~InputHandler() override = default;

		InputHandler(const InputHandler&) = delete;
		InputHandler& operator=(const InputHandler&) = delete;
	};
}
