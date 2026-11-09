#pragma once

namespace scf::plugin
{
	class RuntimeGate
	{
	public:
		[[nodiscard]] static bool ValidateRuntime() noexcept;
	};
}
