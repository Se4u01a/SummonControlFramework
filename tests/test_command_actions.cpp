#include "check.h"
#include "Core/CommandActions.h"

using namespace scf::core;

// Given the shipped default file
// When it is parsed
// Then every line is accepted: atronachs cast, frost atronach rushes, the rest rush.
TEST(TestCommandCatalogDefaultsParse)
{
	const auto catalog = ParseCommandCatalog(DefaultCommandCatalogText());
	CHECK(catalog.warnings.empty());
	CHECK(catalog.byBase.size() == 6);
	CHECK(catalog.fallback.has_value());
	CHECK(catalog.fallback->action == CommandAction::Rush);
	CHECK(catalog.fallback->cooldownSeconds == 8.0f);

	const auto& flame = catalog.byBase[0];
	CHECK(flame.first.plugin == "Skyrim.esm");
	CHECK(flame.first.localId == 0x000204C0);
	CHECK(flame.second.action == CommandAction::CastAtTarget);
	CHECK(flame.second.spell.has_value());
	CHECK(flame.second.spell->localId == 0x0001C789);
	CHECK(flame.second.cooldownSeconds == 10.0f);
	CHECK(catalog.rushSpeedBonus == 60.0f);
	CHECK(catalog.rushSeconds == 3.0f);
}

// Given lines with mistakes, each is skipped with a warning and the rest still load.
TEST(TestCommandCatalogRejectsBadLines)
{
	const auto catalog = ParseCommandCatalog(
		"[Commands]\n"
		"Skyrim.esm|0x000204C0 = CastAtTarget, , 10\n"       // cast without a spell
		"Skyrim.esm|0x000204C1 = Explode, , 10\n"             // unknown action
		"Skyrim.esm|nothex = Rush\n"                          // bad form
		"Skyrim.esm|0x000204C2 = Rush, , 0\n"                 // cooldown out of range
		"Skyrim.esm|0x000204C3\n"                             // no '='
		"Mod.esp|0x801 = Detonate, Mod.esp|0x802, 30 ; ok\n"
		"[General]\n"
		"fRushSeconds = 99\n");                               // out of range
	CHECK(catalog.warnings.size() == 6);
	CHECK(catalog.byBase.size() == 1);
	CHECK(catalog.byBase[0].first.plugin == "Mod.esp");
	CHECK(catalog.byBase[0].second.action == CommandAction::Detonate);
	CHECK(catalog.byBase[0].second.cooldownSeconds == 30.0f);
	CHECK(!catalog.fallback.has_value());
	CHECK(catalog.rushSeconds == 3.0f);
}

// Given the same summon listed twice, the last line wins.
TEST(TestCommandCatalogLastLineWins)
{
	const auto catalog = ParseCommandCatalog(
		"[Commands]\n"
		"Skyrim.esm|0x204C0 = Rush\n"
		"skyrim.esm|0x204C0 = Rush, , 20\n"
		"Skyrim.esm|0x204C0 = CastAtTarget, Skyrim.esm|0x1C789, 12\n");
	CHECK(catalog.warnings.empty());
	CHECK(catalog.byBase.size() == 1);  // plugin names compare case-insensitively
	CHECK(catalog.byBase[0].second.action == CommandAction::CastAtTarget);
	CHECK(catalog.byBase[0].second.cooldownSeconds == 12.0f);
}

// Given form keys in the accepted spellings.
TEST(TestFormKeyParse)
{
	CHECK(ParseFormKey("Skyrim.esm|0x0001C789")->localId == 0x1C789);
	CHECK(ParseFormKey(" Dawnguard.esm | 1C789 ")->plugin == "Dawnguard.esm");
	CHECK(!ParseFormKey("Skyrim.esm").has_value());
	CHECK(!ParseFormKey("|0x10").has_value());
	CHECK(!ParseFormKey("Skyrim.esm|0x0").has_value());
	CHECK(!ParseFormKey("Skyrim.esm|0xZZ").has_value());
}

// Given a summon that used its command, it waits out its own cooldown; others are free.
TEST(TestCommandCooldowns)
{
	CommandCooldowns cooldowns;
	CHECK(cooldowns.IsReady(1, 0.0f));
	cooldowns.Start(1, 10.0f, 8.0f);
	CHECK(!cooldowns.IsReady(1, 12.0f));
	CHECK(cooldowns.Remaining(1, 12.0f) == 6.0f);
	CHECK(cooldowns.IsReady(2, 12.0f));
	CHECK(cooldowns.IsReady(1, 18.0f));
	cooldowns.Start(1, 20.0f, 8.0f);
	cooldowns.Forget(1);
	CHECK(cooldowns.IsReady(1, 21.0f));
}
