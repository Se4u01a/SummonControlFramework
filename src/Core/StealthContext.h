#pragma once

namespace scf::core
{
	struct StealthContext
	{
		bool playerSneaking{ false };
		bool playerDetected{ false };
		bool explicitAttackOrder{ false };
	};
}
