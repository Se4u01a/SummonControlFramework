#pragma once

namespace RE
{
	class Actor;
}

namespace scf::engine::wolf
{
	bool UsesWolfSkeleton(RE::Actor* a_actor);
	void InstallManeuverHooks();
	void SetFrameworkEnabled(bool a_enabled);
}
