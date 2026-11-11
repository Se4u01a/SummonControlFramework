#include "Integrations/FlickMenu.h"
#include "Integrations/MinionPanel.h"
#include "Core/FrameworkContext.h"
#include "Core/Settings.h"
#include "PCH.h"

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include "FUCK_API.h"
#include "IconsFontAwesome6.h"

#include <atomic>
#include <string>

namespace scf::integrations
{
	namespace
	{
		enum class HotkeySlot : std::uint8_t
		{
			None,
			FocusTarget,
			Return,
			ToggleAggression,
			MoveTo,
			WaitGuard,
			Follow,
			CommandAction,
			MinionPanelToggle
		};

		class SCFMenuTool final : public FUCK::ITool
		{
		public:
			const char* Name() const override { return "Summon Control Framework"; }
			const char* Group() const override { return "Gameplay"; }

			bool OnAsyncInput(const void* a_event) override
			{
				const auto slot = _bindingSlot.load();
				if (slot == HotkeySlot::None) {
					return false;
				}

				std::uint32_t key = 0;
				std::int32_t mod1 = -1, mod2 = -1;
				switch (FUCK::UpdateBinding(a_event, &key, &mod1, &mod2)) {
				case FUCK::BindResult::kBound:
					_boundKey.store(key);
					_boundSlot.store(slot);
					_bindingSlot.store(HotkeySlot::None);
					break;
				case FUCK::BindResult::kCancelled:
					_bindingSlot.store(HotkeySlot::None);
					break;
				default:
					break;
				}
				return true;
			}

			void Draw() override
			{
				auto& context = core::FrameworkContext::GetSingleton();
				auto& settingsObj = context.GetSettings();
				auto& data = settingsObj.GetMutable();

				// Process bound key from input thread
				if (const auto slot = _boundSlot.exchange(HotkeySlot::None); slot != HotkeySlot::None) {
					const auto key = _boundKey.exchange(0);
					// Keyboard scan codes and FLICK's mouse codes (>= kMBBase) are both valid:
					// InputHandler compares the same number space for each device.
					if (key != 0) {
						switch (slot) {
						case HotkeySlot::FocusTarget:      data.hotkeys.focusTargetKey = key; break;
						case HotkeySlot::Return:           data.hotkeys.commandReturnKey = key; break;
						case HotkeySlot::ToggleAggression: data.hotkeys.toggleAggressionKey = key; break;
						case HotkeySlot::MoveTo:           data.hotkeys.moveToKey = key; break;
						case HotkeySlot::WaitGuard:        data.hotkeys.waitGuardKey = key; break;
						case HotkeySlot::Follow:           data.hotkeys.followKey = key; break;
						case HotkeySlot::CommandAction:    data.hotkeys.commandActionKey = key; break;
						case HotkeySlot::MinionPanelToggle: data.hotkeys.minionPanelToggleKey = key; break;
						default: break;
						}
						settingsObj.ValidateAndSanitize();
					}
				}

				if (_bindingSlot.load() != HotkeySlot::None && !FUCK::IsBinding()) {
					_bindingSlot.store(HotkeySlot::None);
				}

				FUCK::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", FUCK::IconText(ICON_FA_DOG, "Summon Control Framework"));
				FUCK::TextDisabled("Tactical squad coordination, friendly fire protection, and in-game controls");
				FUCK::Separator();
				FUCK::Spacing();

				// --- Section 1: Tactical Hotkeys ---
				FUCK::SeparatorText("Tactical Squad Hotkeys");
				DrawHotkeyRow("Focus Target (Crosshair)", "Command summons to attack target under crosshair (Default: Numpad 1)", HotkeySlot::FocusTarget, data.hotkeys.focusTargetKey);
				DrawHotkeyRow("Return to Owner", "Emergency recall: drop combat and return to owner (Default: Numpad 2)", HotkeySlot::Return, data.hotkeys.commandReturnKey);
				DrawHotkeyRow("Toggle Aggression Mode", "Cycle between Passive, Defensive, and Aggressive modes (Default: Numpad 3)", HotkeySlot::ToggleAggression, data.hotkeys.toggleAggressionKey);
				DrawHotkeyRow("Move To Point", "Order squad to move to position under crosshair and hold guard (Default: Numpad 4)", HotkeySlot::MoveTo, data.hotkeys.moveToKey);
				DrawHotkeyRow("Guard / Hold Position", "Hold current position and defend against nearby threats (Default: Numpad 5)", HotkeySlot::WaitGuard, data.hotkeys.waitGuardKey);
				DrawHotkeyRow("Follow in Formation", "Resume following player in tactical formation (Default: Numpad 6)", HotkeySlot::Follow, data.hotkeys.followKey);
				DrawHotkeyRow("Command Action", "Each summon performs its type's command at the aimed enemy: rush, cast, detonate (SummonControlFramework_Commands.ini; Default: Numpad 7)", HotkeySlot::CommandAction, data.hotkeys.commandActionKey);
				DrawHotkeyRow("Toggle Minion Panel", "Show or hide the minion HUD panel (Default: unbound)", HotkeySlot::MinionPanelToggle, data.hotkeys.minionPanelToggleKey);

				FUCK::Spacing();

				// --- Section 2: Combat & Squad Parameters ---
				FUCK::SeparatorText("Tactical & Combat AI");
				FUCK::SliderFloat("Formation Distance", &data.movement.followMinDistance, 50.0f, 1000.0f, "%.0f");
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Distance at which summons hold their formation slot around the player");

				FUCK::SliderFloat("Catch-Up Run Distance", &data.movement.followMaxDistance, 100.0f, 2000.0f, "%.0f");
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Beyond this distance from its slot a summon runs instead of walks");

				FUCK::SliderFloat("Return Leash Distance", &data.combat.returnDistanceThreshold, 800.0f, 5000.0f, "%.0f");
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Leash threshold: if player exceeds this distance, summons break combat and return");

				FUCK::Checkbox("Combat Leash", &data.combat.enableCombatLeash);
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Break off combat when a summon strays too far from the player");

				FUCK::SliderFloat("Combat Leash Radius", &data.combat.combatLeashRadius, 200.0f, 8000.0f, "%.0f");
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Maximum distance from the player during combat before the leash snaps");

				FUCK::Checkbox("Assist Player Attacks", &data.combat.assistPlayerAttacks);
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Summons switch to whatever enemy the player hits (not in Passive, Guard, Return or Move To)");

				FUCK::SliderFloat("Aggression Radius", &data.combat.aggressionRadius, 0.0f, 6000.0f, "%.0f");
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Summons start their own fights only with enemies this close to the player (0 = no limit)");

				FUCK::Checkbox("Catch-Up Warp", &data.recovery.enableCatchUpWarp);
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Emergency teleport behind the player when a returning summon is far away or stuck");

				FUCK::SliderFloat("Catch-Up Warp Distance", &data.recovery.catchUpDistance, 500.0f, 20000.0f, "%.0f");
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Distance beyond which a Return order warps the summon instead of walking");

				FUCK::SliderFloat("Target Lost Debounce", &data.combat.targetLostDebounceTime, 0.5f, 5.0f, "%.1fs");
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Hysteresis: time to maintain pursuit after momentary loss of line-of-sight");

				FUCK::SliderFloat("Formation Spread", &data.squad.formationSpreadRadius, 80.0f, 400.0f, "%.0f");
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Spread distance between summons in formation");

				FUCK::Spacing();

				// --- PoE2 minions: follow paces, persistence, panel ---
				FUCK::SeparatorText("PoE2 Minions");
				{
					int limit = static_cast<int>(data.general.playerSummonLimit);
					if (FUCK::SliderInt("Player Summon Limit", &limit, 0, 32, limit == 0 ? "vanilla" : "%d")) {
						data.general.playerSummonLimit = static_cast<std::uint32_t>(limit);
					}
					if (FUCK::IsItemHovered()) FUCK::SetTooltip("How many summons you may command at once (0 = vanilla: 1, Twin Souls 2). Applies to the next summon cast.");
				}
				FUCK::SliderFloat("Sprint Distance", &data.movement.sprintDistance, 400.0f, 5000.0f, "%.0f");
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("A following summon lagging further than this behind its slot sprints");

				FUCK::SliderFloat("Run Speed (SpeedMult)", &data.movement.followRunSpeed, 100.0f, 300.0f, "%.0f");
				FUCK::SliderFloat("Sprint Speed (SpeedMult)", &data.movement.sprintSpeed, 100.0f, 400.0f, "%.0f");

				FUCK::SliderFloat("Stuck Recall Seconds", &data.recovery.stuckWarpSeconds, 0.0f, 15.0f, "%.1fs");
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("A following summon that makes no progress this long is recalled behind the player (0 = off)");

				FUCK::Checkbox("Persistent Summons", &data.persistence.persistentSummons);
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Summons do not expire; each reserves max Magicka while it lives");

				FUCK::Checkbox("Auto-Revive", &data.persistence.autoRevive);
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("A persistent summon that dies is re-summoned after the delay, unless another summon died recently");

				FUCK::SliderFloat("Revive Delay", &data.persistence.reviveDelaySeconds, 1.0f, 60.0f, "%.0fs");
				FUCK::SliderFloat("Magicka Reserve per Summon", &data.persistence.magickaReservePerSummon, 0.0f, 300.0f, "%.0f");

				if (FUCK::Checkbox("Show Minion Panel", &data.minionPanel.enabled)) {
					MinionPanel::SetEnabled(data.minionPanel.enabled);
				}
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("HUD panel with every summon's health, order, state, target and command cooldown");

				FUCK::Spacing();

				// --- Section 3: Friendly Fire & Pass-Through ---
				FUCK::SeparatorText("Friendly Fire Protection & Pass-Through");
				FUCK::Checkbox("Enable Summon Protection", &data.friendlyFire.enabled);
				FUCK::Checkbox("Melee Pass-Through (Precision)", &data.friendlyFire.meleePassThrough);
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Melee weapon swings pass physically through summons without hitstop to strike enemies behind");

				FUCK::Checkbox("Arrow & Bolt Pass-Through", &data.friendlyFire.arrowPassThrough);
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Arrows and bolts fly through friendly summons into targets behind them");

				FUCK::Checkbox("Block Friendly Melee Damage", &data.friendlyFire.blockMeleeDamage);
				FUCK::Checkbox("Block Friendly Arrow Damage", &data.friendlyFire.blockArrowDamage);
				FUCK::Checkbox("Block Friendly Magic Damage", &data.friendlyFire.blockMagicProjectileDamage);

				FUCK::Spacing();

				// --- Section 4: Physical Collision Bypass ---
				FUCK::SeparatorText("Physical Actor Collision");
				FUCK::Checkbox("Player-to-Summon Passthrough", &data.collision.enableCollisionBypass);
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Player walks freely through summons without doorway blocking");

				FUCK::Checkbox("Summon-to-Summon Passthrough", &data.collision.enableSummonToSummonBypass);
				if (FUCK::IsItemHovered()) FUCK::SetTooltip("Summons pass through each other in narrow doorways");

				FUCK::Spacing();
				FUCK::Separator();
				FUCK::Spacing();

				// --- Section 5: Persistence Footer ---
				const std::string iniPath = "Data/SKSE/Plugins/SummonControlFramework.ini";
				if (FUCK::Button(FUCK::IconText(ICON_FA_FLOPPY_DISK, "Save to INI"))) {
					settingsObj.SaveToFile(iniPath);
				}
				FUCK::SameLine();
				if (FUCK::Button(FUCK::IconText(ICON_FA_ARROWS_ROTATE, "Reload INI"))) {
					settingsObj.LoadFromFile(iniPath);
					MinionPanel::SetEnabled(settingsObj.Get().minionPanel.enabled);
				}
				FUCK::SameLine();
				if (FUCK::Button(FUCK::IconText(ICON_FA_ROTATE_LEFT, "Restore Defaults"))) {
					settingsObj.ResetToDefaults();
				}
			}

		private:
			void DrawHotkeyRow(const char* a_label, const char* a_tooltip, HotkeySlot a_slot, std::uint32_t a_key)
			{
				const bool isBinding = (_bindingSlot.load() == a_slot);
				std::string btnText;
				if (isBinding) {
					btnText = "Press any key...###" + std::string(a_label);
				} else {
					btnText = std::string(a_label) + "###" + std::string(a_label);
				}

				const bool busy = !isBinding && FUCK::IsBinding();
				if (busy) {
					FUCK::BeginDisabled();
				}

				if (FUCK::Hotkey(btnText.c_str(), isBinding ? 0 : a_key, -1, -1, true, true, isBinding) && !isBinding) {
					_bindingSlot.store(a_slot);
					FUCK::StartBinding(0, -1, -1, true);
				}

				if (busy) {
					FUCK::EndDisabled();
				}

				if (FUCK::IsItemHovered()) {
					FUCK::SetTooltip(a_tooltip);
				}
			}

			std::atomic<HotkeySlot> _bindingSlot{ HotkeySlot::None };
			std::atomic<HotkeySlot> _boundSlot{ HotkeySlot::None };
			std::atomic<std::uint32_t> _boundKey{ 0 };
		};

		SCFMenuTool g_menuTool;
	}

	void FlickMenu::Install()
	{
		if (!GetModuleHandleW(L"FUCK.dll")) {
			logger::info("[FlickMenu] FLICK (FUCK.dll) not loaded; in-game UI menu disabled.");
			return;
		}

		if (!FUCK::Connect("SummonControlFramework")) {
			logger::warn("[FlickMenu] FLICK found but API connection failed.");
			return;
		}

		FUCK::RegisterTool(&g_menuTool);
		logger::info("[FlickMenu] Successfully registered Summon Control Framework tool with FLICK.");
	}
}
