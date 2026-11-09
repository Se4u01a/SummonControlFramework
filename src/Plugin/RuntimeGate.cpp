#include "PCH.h"
#include "Plugin/RuntimeGate.h"

namespace scf::plugin
{
	bool RuntimeGate::ValidateRuntime() noexcept
	{
		const auto& module = REL::Module::get();
		const auto version = module.version();

		logger::info("Validating runtime environment...");
		logger::info("Detected game version: {}", version.string("."));

#if defined(ENABLE_SKYRIM_AE)
		logger::info("Build configuration: Skyrim AE (1.6.x) support enabled");
#endif
#if defined(ENABLE_SKYRIM_SE)
		logger::info("Build configuration: Skyrim SE (1.5.97) support enabled");
#endif

		// Primary target in PLAN.md is Steam AE 1.6.1170, with SE 1.5.97 supported via Address Library
		if (version == REL::Version{ 1, 6, 1170, 0 }) {
			logger::info("Exact match: Primary test target Steam AE 1.6.1170 confirmed.");
			return true;
		}

		if (version == REL::Version{ 1, 5, 97, 0 }) {
			logger::info("Exact match: Skyrim SE 1.5.97 confirmed.");
			return true;
		}

		logger::warn("Game version {} is running under Address Library compatibility mode.", version.string("."));
		return true;
	}
}
