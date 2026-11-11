#pragma once

#include "Core/EntityID.h"

namespace RE
{
	class Actor;
}

namespace scf::integrations
{
	// FocusTarget indicator on top of TrueHUD (optional dependency, like PrecisionBackend).
	//
	// TrueHUD draws an info bar for the actor the player is currently targeting. SCF wants
	// the same bar for the squad's explicit FocusTarget, so the player keeps seeing the
	// health of the actor the summons were ordered to fight while aiming elsewhere.
	//
	// Rules this backend keeps:
	//   * No TrueHUD in the load order -> every call is a no-op and exactly one "not found"
	//     line is logged from Initialize(). Nothing else in the plugin changes behaviour.
	//   * TrueHUD's target control is never requested (RequestTargetControl): the player's
	//     own targeting keeps working exactly as TrueHUD decides it. Only the info bar of
	//     our own focus actor is added and removed.
	//   * The interface is touched from the game's main thread only. Initialize() records
	//     the thread it was called on; the mutating calls refuse to run anywhere else
	//     (TrueHUD answers APIResult::BadThread to off-thread commands).
	//   * Sync() is "form id in, at most one bar out": it compares the requested focus
	//     target with the one already drawn and only touches the API on a change, so it is
	//     safe to call from every tick.
	class TrueHUDBackend
	{
	public:
		TrueHUDBackend() = default;

		// Requests the TrueHUD interface. Call once from the SKSE messaging handler, on the
		// game's main thread - next to PrecisionBackend::Initialize() in
		// Plugin/Messaging.cpp (kDataLoaded). Returns whether the API is usable.
		static bool Initialize() noexcept;

		// True once TrueHUD answered the interface request. False when it is absent.
		static bool IsAvailable() noexcept;

		// Draws the focus indicator for a_target: adds its TrueHUD info bar and flashes the
		// health bar once, so a fresh focus is visible. Replaces whatever bar was shown
		// before. Main thread only; a no-op without TrueHUD.
		static void SetFocusTarget(RE::Actor* a_target) noexcept;

		// Removes the focus indicator, if one is shown. Main thread only; a no-op otherwise.
		static void ClearFocusTarget() noexcept;

		// Per-tick entry point. a_focusTargetFormId is the squad's current FocusTarget
		// (core::InvalidEntityID when there is none). The call only does work when the
		// requested target differs from the drawn one: the previous bar is removed and the
		// new one added. Main thread only; a no-op without TrueHUD.
		static void Sync(core::EntityID a_focusTargetFormId) noexcept;
	};
}
