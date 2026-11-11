#include "Core/KeywordApi.h"

#include <cctype>

namespace scf::core
{
	namespace
	{
		bool EqualsNoCase(std::string_view a_lhs, std::string_view a_rhs) noexcept
		{
			if (a_lhs.size() != a_rhs.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a_lhs.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(a_lhs[i])) != std::tolower(static_cast<unsigned char>(a_rhs[i]))) {
					return false;
				}
			}
			return true;
		}
	}

	std::uint8_t FlagForKeywordEditorId(std::string_view a_editorId) noexcept
	{
		if (EqualsNoCase(a_editorId, kKeywordPassThroughFriends)) {
			return kPassThroughFriends;
		}
		if (EqualsNoCase(a_editorId, kKeywordPassThroughAll)) {
			return kPassThroughAll;
		}
		if (EqualsNoCase(a_editorId, kKeywordUntargetable)) {
			return kUntargetable;
		}
		return 0;
	}

	std::uint32_t ChooseTargetAvoidingUntargetable(std::uint32_t a_engineAnswer, bool a_answerUntargetable,
		const std::vector<SelectorCandidate>& a_candidates) noexcept
	{
		if (a_engineAnswer == 0 || !a_answerUntargetable) {
			return a_engineAnswer;
		}
		for (const auto& candidate : a_candidates) {
			if (candidate.handle != 0 && candidate.handle != a_engineAnswer && !candidate.untargetable && !candidate.lost) {
				return candidate.handle;
			}
		}
		return 0;
	}
}
