#pragma once

namespace scf::integrations
{
	class PrecisionBackend
	{
	public:
		PrecisionBackend() = default;

		static bool Initialize() noexcept;
		static bool IsAvailable() noexcept;

	private:
		static inline bool s_initialized{ false };
	};
}
