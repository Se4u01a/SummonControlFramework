#pragma once

#include "Core/EntityID.h"
#include "Core/MinionPanelModel.h"

#include <cstddef>
#include <vector>

namespace scf::integrations
{
	// PoE2 stage M6 - the minion HUD panel.
	//
	// A FLICK (FUCK.dll) IWindow flagged as a HUD widget: no decoration, no resize, no
	// input capture, hidden with the game HUD and with the native menus. The panel only
	// displays; picking a subgroup by clicking belongs to a later stage.
	//
	// Threading: the window's Draw() is called by FLICK's render callback, whose thread is
	// not part of this plugin's contract. Nothing game-side is read from there: the main
	// thread publishes a snapshot with PublishSnapshot() (called from the tactical tick,
	// where a TESForm lookup and an actor-value read are legal) and Draw() only formats
	// that snapshot under a mutex. See src/Integrations/MinionPanel.cpp.

	struct MinionPanelConfig
	{
		bool enabled{ false };            // the new HUD is opt-in
		bool showHealthNumbers{ true };   // "71% (142 / 200)" next to the name
		bool showTarget{ true };          // "Target: Bandit" line
		bool showCommand{ true };         // "Cmd: ready" / "Cmd: 3.4 s" line
		bool showDuration{ true };        // "Duration: 2:14" line
		bool hideWhenEmpty{ false };      // keep the header visible while there are no summons
		float posX{ 40.0f };              // default screen position, UI-scaled by FLICK
		float posY{ 240.0f };
		float width{ 260.0f };            // health-bar width, UI-scaled by FLICK
		std::size_t maxRows{ 32 };        // rows drawn per frame (the registry tops out at 32)
	};

	// Per-summon command cooldown, supplied by the caller because the cooldown table lives
	// in the private state of src/Engine/CommandActions.cpp.
	struct CommandCooldownView
	{
		core::EntityID summonId{ core::InvalidEntityID };
		float remainingSeconds{ 0.0f };
		float totalSeconds{ 0.0f };
	};

	class MinionPanel
	{
	public:
		// Registers the HUD window with FLICK. Call once, after integrations::FlickMenu::Install()
		// (kDataLoaded). With no FUCK.dll it logs and does nothing; the rest of the plugin is
		// unaffected.
		static void Install();

		// Applies the display settings and the visibility toggle. Call on settings load and
		// whenever a setting changes; it logs one line per call.
		static void Configure(const MinionPanelConfig& a_config);

		// Current settings, including the live visibility toggle.
		[[nodiscard]] static MinionPanelConfig GetConfig();

		// Show/hide toggle for a hotkey or a console command. Any thread.
		static void SetEnabled(bool a_enabled);
		[[nodiscard]] static bool IsEnabled();

		// Main thread, from the tactical tick: reads the registry and the live actors for
		// a_ownerId and publishes the rows the panel draws. a_now is the same monotonic clock
		// the registry stamps deaths with (update::s_totalElapsed); it only decides how long a
		// summon that went down stays on screen.
		// a_cooldowns may be empty: the command line is then omitted.
		static void PublishSnapshot(
			core::EntityID a_ownerId,
			float a_now,
			const std::vector<CommandCooldownView>& a_cooldowns = {});

		// Drops the published rows (save load / new game), so the panel does not show a
		// previous save's summons.
		static void ClearSnapshot();
	};
}
