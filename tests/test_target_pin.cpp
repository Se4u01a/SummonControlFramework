#include "check.h"
#include "Core/TargetPin.h"

using namespace scf::core;

namespace
{
	constexpr std::uint32_t kEnginePick = 0x00100A01;
	constexpr std::uint32_t kFocus = 0x00200B02;
}

// Given a summon fighting with a FocusTarget that the group tracks
// When the engine's selector picks a different enemy
// Then the selector answers with the focused target
TEST(TestTargetPinReplacesEnginePick)
{
	CHECK(ChooseSelectorAnswer(kEnginePick, kFocus, true, PinMembership::Held) == kFocus);
}

// Given no FocusTarget, the engine's own pick always stands.
TEST(TestTargetPinWithoutPinKeepsEnginePick)
{
	CHECK(ChooseSelectorAnswer(kEnginePick, 0, true, PinMembership::Held) == kEnginePick);
}

// Given the engine holds no target at all, the pin never starts or revives a fight.
TEST(TestTargetPinNeverRevivesAFight)
{
	CHECK(ChooseSelectorAnswer(0, kFocus, true, PinMembership::Held) == 0);
}

// Given the focused target is not (yet) a combat target of the summon's group,
// the engine's pick stands until StartCombat adds it.
TEST(TestTargetPinAbsentFromGroupKeepsEnginePick)
{
	CHECK(ChooseSelectorAnswer(kEnginePick, kFocus, true, PinMembership::Absent) == kEnginePick);
}

// Given the group flagged the focused target lost, the engine's pick stands.
TEST(TestTargetPinLostTargetKeepsEnginePick)
{
	CHECK(ChooseSelectorAnswer(kEnginePick, kFocus, true, PinMembership::Lost) == kEnginePick);
}

// Given the focused target died or unloaded, the engine's pick stands.
TEST(TestTargetPinDeadTargetKeepsEnginePick)
{
	CHECK(ChooseSelectorAnswer(kEnginePick, kFocus, false, PinMembership::Held) == kEnginePick);
}

// Given the engine already picked the focused target, the answer is unchanged.
TEST(TestTargetPinAgreeingPickIsUnchanged)
{
	CHECK(ChooseSelectorAnswer(kFocus, kFocus, true, PinMembership::Held) == kFocus);
}
