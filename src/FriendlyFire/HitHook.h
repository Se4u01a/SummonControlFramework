#pragma once

namespace scf::friendly_fire
{
	class HitHook
	{
	public:
		HitHook() = default;

		static bool Install() noexcept;
	};
}
