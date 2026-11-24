#include "PCH.h"
#include "Integrations/CombatRadiusMenu.h"

#include <format>
#include <mutex>

namespace scf::integrations::radius_menu
{
	namespace
	{
		struct ImVec2 { float x, y; };
		using Render = void(__stdcall*)();
		struct Api
		{
			void (*add)(const char*, Render){};
			void (*text)(const char*, const char*){};
			bool (*button)(const char*, ImVec2){};
			bool (*checkbox)(const char*, bool*){};
			bool (*intSlider)(const char*, int*, int, int, const char*, int){};
			bool (*floatSlider)(const char*, float*, float, float, const char*, int){};
		};
		Api api;
		std::mutex mutex;
		core::CombatRadiusSettings settings;
		std::vector<SummonView> rows;
		std::size_t enemies{};
		bool installed{};
		bool dirty{};
		bool reload{};
		bool reset{};

		template<class T>
		bool Bind(HMODULE a_module, const char* a_name, T& a_function)
		{
			a_function = reinterpret_cast<T>(GetProcAddress(a_module, a_name));
			return a_function != nullptr;
		}

		void __stdcall Status()
		{
			std::lock_guard lock(mutex);
			if (!settings.menuEnabled) {
				api.text("Disabled in SummonCombatRadius.ini", nullptr);
				return;
			}
			const auto summary = std::format("Tagged summons: {} | Nearby enemies: {}", rows.size(), enemies);
			api.text(summary.c_str(), nullptr);
			for (const auto& row : rows) {
				const auto target = row.target ? std::format("{:08X} at {:.0f}", row.target, row.targetDistanceToPlayer) : "-";
				const auto line = std::format("{} [{:08X}] | {} | R {:.0f} | Owner {:.0f} | Target {} | In zone {} | {}",
					row.name, row.id, row.state, row.radius, row.distanceToPlayer, target, row.enemiesInRadius, row.package);
				api.text(line.c_str(), nullptr);
			}
		}

		void __stdcall Settings()
		{
			std::lock_guard lock(mutex);
			if (settings.menuEnabled) {
				dirty |= api.checkbox("Summon Combat Radius enabled", &settings.enabled);
				dirty |= api.checkbox("Combat zone enabled", &settings.combatEnabled);
				dirty |= api.checkbox("Character collision bypass", &settings.collisionEnabled);
				dirty |= api.checkbox("Verbose log", &settings.verbose);
				dirty |= api.intSlider("Retarget cooldown (ms)", &settings.retargetCooldownMs, 250, 60000, "%d", 0);
				dirty |= api.floatSlider("Retention margin", &settings.hysteresis, 0.0f, 300.0f, "%.0f", 0);
				for (const auto& keyword : settings.keywords) {
					const auto line = std::format("{} = {:.0f}", keyword.editorId, keyword.radius);
					api.text(line.c_str(), nullptr);
				}
				reset |= api.button("Reset summon tracking", { 0, 0 });
			}
			reload |= api.button("Reload INI", { 0, 0 });
		}
	}

	void Configure(const core::CombatRadiusSettings& a_settings)
	{
		{
			std::lock_guard lock(mutex);
			settings = a_settings;
			dirty = false;
		}
		if (installed || !a_settings.menuEnabled) {
			return;
		}
		const auto module = GetModuleHandleW(L"SKSEMenuFramework.dll");
		if (module && Bind(module, "AddSectionItem", api.add) && Bind(module, "igTextUnformatted", api.text) &&
		    Bind(module, "igButton", api.button) && Bind(module, "igCheckbox", api.checkbox) &&
		    Bind(module, "igSliderInt", api.intSlider) && Bind(module, "igSliderFloat", api.floatSlider)) {
			api.add("Summon Combat Radius/Status", Status);
			api.add("Summon Combat Radius/Settings", Settings);
			installed = true;
			logger::info("[CombatRadius] Registered SKSE Menu Framework panels.");
		}
	}

	bool Available()
	{
		return installed;
	}

	Changes Consume()
	{
		std::lock_guard lock(mutex);
		return { std::exchange(dirty, false) ? std::optional(settings) : std::nullopt,
			std::exchange(reload, false), std::exchange(reset, false) };
	}

	void Publish(const std::vector<SummonView>& a_summons, std::size_t a_enemies)
	{
		std::lock_guard lock(mutex);
		rows = a_summons;
		enemies = a_enemies;
	}
}
