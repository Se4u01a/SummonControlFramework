#pragma once

namespace scf::friendly_fire
{
	class ProjectileHook
	{
	public:
		ProjectileHook() = default;

		static bool Install() noexcept;
	};
}
