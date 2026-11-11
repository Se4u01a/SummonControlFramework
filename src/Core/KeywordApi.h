#pragma once

#include "Core/PassThrough.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace scf::core
{
	// Keyword API for other mods (no SCF plugin file, no code): a KYWD record with one of
	// these EditorIDs, defined in ANY plugin, on an actor's base or race or on the base
	// effect (MGEF) of an active magic effect on the actor, applies to that actor -
	// any actor, not only SCF summons. EditorIDs compare case-insensitively.
	inline constexpr std::string_view kKeywordPassThroughFriends = "SCF_PassThroughFriends";
	inline constexpr std::string_view kKeywordPassThroughAll = "SCF_PassThroughAll";
	inline constexpr std::string_view kKeywordUntargetable = "SCF_Untargetable";

	// The PassThroughFlag a keyword EditorID stands for, 0 if none.
	std::uint8_t FlagForKeywordEditorId(std::string_view a_editorId) noexcept;

	// One entry of a CombatGroup's target list, as the selector seat sees it.
	struct SelectorCandidate
	{
		std::uint32_t handle{ 0 };
		bool untargetable{ false };
		bool lost{ false };
	};

	// SCF_Untargetable in an enemy's target selection: keep the engine's answer unless it
	// names an untargetable actor; then the first group target that is neither
	// untargetable nor lost; else 0 (no target - the main-thread sweep ends that fight).
	std::uint32_t ChooseTargetAvoidingUntargetable(std::uint32_t a_engineAnswer, bool a_answerUntargetable,
		const std::vector<SelectorCandidate>& a_candidates) noexcept;
}
