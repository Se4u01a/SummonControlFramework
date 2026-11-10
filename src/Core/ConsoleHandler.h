#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace scf::core
{
	class FrameworkContext;

	class ConsoleHandler
	{
	public:
		using OutputCallback = std::function<void(const std::string&)>;

		static void Execute(
			std::string_view a_commandLine,
			FrameworkContext& a_context,
			OutputCallback a_output,
			const std::string& a_configFilePath = "Data/SKSE/Plugins/SummonControlFramework.ini");
	};
}
