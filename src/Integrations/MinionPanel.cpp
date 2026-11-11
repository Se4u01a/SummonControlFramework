#include "Integrations/MinionPanel.h"
#include "Core/FrameworkContext.h"
#include "Core/MinionPanelModel.h"
#include "Core/SummonRegistry.h"
#include "PCH.h"

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include "FUCK_API.h"
#include "IconsFontAwesome6.h"

#include <RE/A/Actor.h>
#include <RE/A/ActorValueOwner.h>
#include <RE/T/TESForm.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scf::integrations
{
	namespace
	{
		// The published snapshot. Written only by PublishSnapshot() on the main thread, read
		// only by MinionPanelWindow::Draw() on FLICK's render thread, so every access is under
		// this mutex. Draw() copies the rows out and releases the lock before touching ImGui:
		// the main thread is never blocked behind a frame of drawing.
		std::mutex s_mutex;
		MinionPanelConfig s_config{};
		std::vector<core::MinionRow> s_rows;

		// Lock-free copies of the two flags IsOpen() needs, because FLICK may ask for them
		// from a thread that must not wait on the publisher.
		std::atomic<bool> s_enabled{ false };
		std::atomic<bool> s_hideWhenEmpty{ false };
		std::atomic<std::size_t> s_rowCount{ 0 };

		std::string FormatFormId(core::EntityID a_id)
		{
			char buf[16]{};
			std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned int>(a_id));
			return std::string(buf);
		}

		void DrawMinionRow(const core::MinionRow& a_row, const MinionPanelConfig& a_config)
		{
			const auto fraction = core::NormalizeHealthFraction(a_row.health, a_row.maxHealth);
			const auto color = core::HealthColor(fraction, a_row.alive);
			const ImVec4 barColor{ color.r, color.g, color.b, color.a };

			FUCK::PushID(static_cast<int>(a_row.summonId));

			// Name line: the summon's own name and, next to it, its health in the matching
			// colour so a wounded summon can be found without reading numbers.
			const auto title = core::FormatTitle(a_row);
			FUCK::TextColored(ImVec4(0.95f, 0.93f, 0.85f, 1.0f), "%s", FUCK::IconText(ICON_FA_PAW, title.c_str()));
			if (a_config.showHealthNumbers) {
				FUCK::SameLine();
				FUCK::TextColored(barColor, "%s", core::FormatHealthSummary(a_row).c_str());
			}

			// Health bar, drawn into the host window's draw list: no window handle needed and
			// no ImGui state pushed, so the panel cannot disturb the rest of the host's UI.
			const float barHeight = FUCK::Scale(5.0f);
			const float barWidth = std::max(0.0f, FUCK::GetContentRegionAvail().x);
			const ImVec2 barMin = FUCK::GetCursorScreenPos();
			const ImVec2 barMax{ barMin.x + barWidth, barMin.y + barHeight };
			FUCK::DrawRectFilled(barMin, barMax, ImVec4(0.16f, 0.16f, 0.18f, 0.90f), 2.0f);
			if (fraction > 0.0f) {
				FUCK::DrawRectFilled(barMin, ImVec2{ barMin.x + barWidth * fraction, barMax.y }, barColor, 2.0f);
			}
			FUCK::Dummy(ImVec2(barWidth, barHeight));

			// Order, FSM state and aggression mode in one dim line.
			FUCK::TextDisabled("%s", core::FormatStatus(a_row).c_str());

			if (a_config.showTarget) {
				const auto target = core::FormatTarget(a_row);
				if (!target.empty()) {
					FUCK::TextDisabled("%s", target.c_str());
				}
			}

			// Command cooldown and the vanilla summon timer share the last dim line.
			std::string extras;
			if (a_config.showCommand) {
				extras = core::FormatCommand(a_row);
			}
			if (a_config.showDuration) {
				const auto duration = core::FormatDuration(a_row);
				if (!extras.empty()) {
					extras += " | ";
				}
				extras += duration;
			}
			if (!extras.empty()) {
				FUCK::TextDisabled("%s", extras.c_str());
			}

			FUCK::Spacing();
			FUCK::PopID();
		}

		class MinionPanelWindow final : public FUCK::IWindow
		{
		public:
			const char* Id() const override { return "MinionPanel"; }
			const char* Title() const override { return "Summons"; }

			bool IsOpen() const override
			{
				if (!s_enabled.load(std::memory_order_relaxed)) {
					return false;
				}
				return !s_hideWhenEmpty.load(std::memory_order_relaxed) ||
				       s_rowCount.load(std::memory_order_relaxed) > 0;
			}

			void SetOpen(bool a_open) override { MinionPanel::SetEnabled(a_open); }

			// A HUD widget, not a menu: no chrome, sized to its contents, never consuming game
			// input, hidden with the game HUD (intro, cutscenes, dialogue) and with the native
			// menus, and movable only through FLICK's own menu (kEditable).
			FUCK::WindowFlags GetFlags() const override
			{
				using FUCK::WindowFlags;
				return WindowFlags::kNoDecoration | WindowFlags::kNoResize | WindowFlags::kAutoResize |
				       WindowFlags::kPassInputToGame | WindowFlags::kCloseOnGameMenu |
				       WindowFlags::kHideWithHUD | WindowFlags::kEditable;
			}

			ImVec2 GetDefaultPos() const override
			{
				const auto config = CopyConfig();
				return ImVec2(FUCK::Scale(config.posX), FUCK::Scale(config.posY));
			}

			ImVec2 GetDefaultSize() const override
			{
				const auto config = CopyConfig();
				return ImVec2(FUCK::UIScale(config.width), FUCK::UIScale(120.0f));
			}

			void Draw() override
			{
				if (!FUCK::GetInterface() || !s_enabled.load(std::memory_order_relaxed)) {
					return;
				}

				MinionPanelConfig config;
				std::vector<core::MinionRow> rows;
				{
					std::scoped_lock lock(s_mutex);
					config = s_config;
					rows = s_rows;
				}

				if (rows.empty()) {
					FUCK::TextDisabled("No active summons");
					return;
				}
				if (rows.size() > config.maxRows) {
					rows.resize(config.maxRows);
				}

				FUCK::TextColored(ImVec4(0.55f, 0.80f, 1.00f, 1.00f), "%s", core::FormatHeader(rows).c_str());
				FUCK::Separator();

				for (const auto& row : rows) {
					DrawMinionRow(row, config);
				}
			}

		private:
			static MinionPanelConfig CopyConfig()
			{
				std::scoped_lock lock(s_mutex);
				return s_config;
			}
		};

		MinionPanelWindow s_window;

		MinionPanelConfig ReadConfig()
		{
			std::scoped_lock lock(s_mutex);
			return s_config;
		}
	}

	void MinionPanel::Install()
	{
		static bool installed = false;
		if (installed) {
			return;
		}

		if (!FUCK::GetInterface()) {
			if (!GetModuleHandleW(L"FUCK.dll")) {
				logger::info("[MinionPanel] FLICK (FUCK.dll) not loaded; the minion HUD panel stays unavailable.");
				return;
			}
			// FlickMenu::Install normally connects first; connecting here keeps the panel
			// working when Install() is reached before it.
			if (!FUCK::Connect("SummonControlFramework")) {
				logger::warn("[MinionPanel] FLICK found but the API connection failed; the minion HUD panel stays unavailable.");
				return;
			}
		}

		FUCK::RegisterWindow(&s_window);
		installed = true;
		logger::info("[MinionPanel] Minion HUD panel registered with FLICK (hidden until enabled in the settings).");
	}

	void MinionPanel::Configure(const MinionPanelConfig& a_config)
	{
		{
			std::scoped_lock lock(s_mutex);
			s_config = a_config;
		}
		s_enabled.store(a_config.enabled, std::memory_order_relaxed);
		s_hideWhenEmpty.store(a_config.hideWhenEmpty, std::memory_order_relaxed);

		logger::info(
			"[MinionPanel] settings: enabled {}, targets {}, command {}, duration {}, health numbers {}, hide-when-empty {}, position ({:.0f}, {:.0f}), width {:.0f}, rows {}.",
			a_config.enabled ? "yes" : "no",
			a_config.showTarget ? "yes" : "no",
			a_config.showCommand ? "yes" : "no",
			a_config.showDuration ? "yes" : "no",
			a_config.showHealthNumbers ? "yes" : "no",
			a_config.hideWhenEmpty ? "yes" : "no",
			a_config.posX,
			a_config.posY,
			a_config.width,
			a_config.maxRows);
	}

	MinionPanelConfig MinionPanel::GetConfig()
	{
		auto config = ReadConfig();
		config.enabled = s_enabled.load(std::memory_order_relaxed);
		return config;
	}

	void MinionPanel::SetEnabled(bool a_enabled)
	{
		const bool changed = s_enabled.exchange(a_enabled, std::memory_order_relaxed) != a_enabled;
		{
			std::scoped_lock lock(s_mutex);
			s_config.enabled = a_enabled;
		}
		if (changed) {
			logger::info("[MinionPanel] Minion HUD panel {}.", a_enabled ? "shown" : "hidden");
		}
	}

	bool MinionPanel::IsEnabled()
	{
		return s_enabled.load(std::memory_order_relaxed);
	}

	void MinionPanel::PublishSnapshot(
		core::EntityID a_ownerId,
		float a_now,
		const std::vector<CommandCooldownView>& a_cooldowns)
	{
		std::vector<core::MinionRow> rows;

		auto& registry = core::FrameworkContext::GetSingleton().GetRegistry();
		for (const auto& [summonId, record] : registry.GetAllRecords()) {
			if (record.ownerId != a_ownerId) {
				continue;
			}
			// A summon that left the roster is shown for the registry's grace window, so the
			// panel can say who died or was displaced instead of the row simply vanishing.
			if (!core::ShouldShowRecord(record.lifecycle, record.isDead, record.deathTimestamp, a_now, core::kDeadRowGraceSeconds)) {
				continue;
			}

			core::MinionRow row;
			row.summonId = summonId;
			row.type = record.type;
			row.lifecycle = record.lifecycle;
			row.remainingDuration = record.remainingDuration;

			// Main thread: a FormID lookup and an actor-value read are legal here and only
			// here, which is why the panel draws a snapshot instead of asking for the actor.
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(summonId);
			row.alive = actor && !actor->IsDead() && !record.isDead;
			if (actor) {
				const char* name = actor->GetName();
				row.name = (name && *name) ? name : FormatFormId(summonId);
				if (auto* values = actor->AsActorValueOwner()) {
					row.health = values->GetActorValue(RE::ActorValue::kHealth);
					row.maxHealth = actor->GetActorValueMax(RE::ActorValue::kHealth);
					row.hasHealth = true;
				}
			} else {
				row.name = FormatFormId(summonId);
			}

			if (const auto* model = registry.GetModel(summonId)) {
				row.order = model->order;
				row.state = model->state;
				row.aggression = model->aggression;
				row.targetId = model->currentTargetId;
			}

			if (row.targetId != core::InvalidEntityID) {
				if (auto* target = RE::TESForm::LookupByID<RE::Actor>(row.targetId)) {
					const char* targetName = target->GetName();
					if (targetName && *targetName) {
						row.targetName = targetName;
					}
				}
			}

			for (const auto& cooldown : a_cooldowns) {
				if (cooldown.summonId != summonId) {
					continue;
				}
				row.hasCooldown = true;
				row.cooldownRemaining = cooldown.remainingSeconds;
				row.cooldownTotal = cooldown.totalSeconds;
				break;
			}

			rows.push_back(std::move(row));
		}

		core::SortRowsForDisplay(rows);

		{
			std::scoped_lock lock(s_mutex);
			s_rows = std::move(rows);
			s_rowCount.store(s_rows.size(), std::memory_order_relaxed);
		}
	}

	void MinionPanel::ClearSnapshot()
	{
		std::scoped_lock lock(s_mutex);
		s_rows.clear();
		s_rowCount.store(0, std::memory_order_relaxed);
	}
}
