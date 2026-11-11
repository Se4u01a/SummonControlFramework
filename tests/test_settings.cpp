#include "Core/Settings.h"
#include "check.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace scf::core;

TEST(settings_default_values)
{
	Settings settings;
	const auto& data = settings.Get();

	CHECK(data.general.enabled == true);
	CHECK(data.general.logLevel == "info");
	CHECK(data.general.maxSummonsPerOwner == 1);

	CHECK_NEAR(data.timing.updateFrequencyHz, 15.0f, 0.001f);
	CHECK(data.timing.enableTimeSlicing == true);
	CHECK_NEAR(data.timing.commandTTL, 5.0f, 0.001f);

	CHECK_NEAR(data.movement.followMinDistance, 200.0f, 0.001f);
	CHECK_NEAR(data.movement.followMaxDistance, 450.0f, 0.001f);

	CHECK(data.combat.enableCombatLeash == true);
	CHECK_NEAR(data.combat.combatLeashRadius, 2000.0f, 0.001f);
	CHECK(data.combat.returnToOwnerWhenOutsideLeash == true);
	CHECK(data.combat.assistPlayerAttacks == true);
	CHECK_NEAR(data.combat.aggressionRadius, 1500.0f, 0.001f);
	CHECK_NEAR(data.combat.maxTargetRadius, 2000.0f, 0.001f);
	CHECK_NEAR(data.combat.followDistanceThreshold, 500.0f, 0.001f);
	CHECK_NEAR(data.combat.returnDistanceThreshold, 2500.0f, 0.001f);
	CHECK_NEAR(data.combat.engageDistanceThreshold, 300.0f, 0.001f);
	CHECK_NEAR(data.combat.targetLostDebounceTime, 1.5f, 0.001f);

	CHECK(data.recovery.enableCatchUpWarp == true);
	CHECK_NEAR(data.recovery.catchUpDistance, 3500.0f, 0.001f);

	CHECK(data.squad.maxMeleeSlotsPerTarget == 2);
	CHECK(data.squad.maxRangedSlotsPerTarget == 3);
	CHECK_NEAR(data.squad.formationSpreadRadius, 160.0f, 0.001f);

	CHECK(data.hotkeys.focusTargetKey == 0x4F);
	CHECK(data.hotkeys.commandReturnKey == 0x50);
	CHECK(data.hotkeys.toggleAggressionKey == 0x51);
	CHECK(data.hotkeys.moveToKey == 0x4B);
	CHECK(data.hotkeys.waitGuardKey == 0x4C);
	CHECK(data.hotkeys.followKey == 0x4D);
	CHECK(data.hotkeys.commandActionKey == 0x47);
	CHECK(data.hotkeys.minionPanelToggleKey == 0);
	CHECK(data.general.playerSummonLimit == 0);
	CHECK(data.movement.sprintDistance == 1200.0f);
	CHECK(data.recovery.stuckWarpSeconds == 3.0f);
	CHECK(data.squad.formationRingCapacity == 6);
	CHECK(!data.persistence.persistentSummons);
	CHECK(!data.minionPanel.enabled);
}

// Given the PoE2 sections in an INI
// When it is loaded and written back
// Then every new key round-trips, and out-of-range values are clamped.
TEST(settings_poe2_sections_round_trip)
{
	scf::core::Settings settings;
	CHECK(settings.LoadFromString(
		"[General]\nuPlayerSummonLimit = 64\n"
		"[Movement]\nfSprintDistance = 900\nfSprintSpeed = 999\n"
		"[Recovery]\nfStuckWarpSeconds = 4\n"
		"[Squad]\nuFormationRingCapacity = 5\n"
		"[Hotkeys]\nuMinionPanelToggleKey = 72\n"
		"[Persistence]\nbPersistentSummons = true\nbPersistVanillaSpellsOnly = true\nfReviveDelaySeconds = 9\n"
		"sBlacklistedSpells = Skyrim.esm|0x000640B6\n"
		"[MinionPanel]\nbShowMinionPanel = true\nfMinionPanelX = 77\n"));
	const auto& data = settings.Get();
	CHECK(data.general.playerSummonLimit == 32);
	CHECK(data.movement.sprintDistance == 900.0f);
	CHECK(data.movement.sprintSpeed == 500.0f);
	CHECK(data.recovery.stuckWarpSeconds == 4.0f);
	CHECK(data.squad.formationRingCapacity == 5);
	CHECK(data.hotkeys.minionPanelToggleKey == 72);
	CHECK(data.persistence.persistentSummons);
	CHECK(data.persistence.vanillaSpellsOnly);
	CHECK(data.persistence.reviveDelaySeconds == 9.0f);
	CHECK(data.persistenceBlacklist == "Skyrim.esm|0x000640B6");
	CHECK(data.minionPanel.enabled);
	CHECK(data.minionPanel.posX == 77.0f);

	scf::core::Settings copy;
	CHECK(copy.LoadFromString(settings.Serialize()));
	CHECK(copy.Get().persistence.persistentSummons);
	CHECK(copy.Get().persistenceBlacklist == "Skyrim.esm|0x000640B6");
	CHECK(copy.Get().minionPanel.posX == 77.0f);
	CHECK(copy.Get().squad.formationRingCapacity == 5);
}

TEST(settings_load_from_string_prefixed)
{
	const char* ini = R"(
; Global configuration
[General]
bEnabled = false
sLogLevel = debug
uMaxSummonsPerOwner = 3

[Timing]
fUpdateFrequencyHz = 20.0
bEnableTimeSlicing = false
fCommandTTL = 10.0

[Movement]
fFollowMinDistance = 180.0
fFollowMaxDistance = 500.0

[Combat]
bEnableCombatLeash = true
fCombatLeashRadius = 2200.0
bReturnToOwnerWhenOutsideLeash = true
fMaxTargetRadius = 3000.0
fFollowDistanceThreshold = 600.0
fReturnDistanceThreshold = 3500.0
fEngageDistanceThreshold = 400.0
ftargetLostDebounceTime = 2.0

[Recovery]
bEnableCatchUpWarp = true
fCatchUpDistance = 4000.0

[Squad]
uMaxMeleeSlotsPerTarget = 4
uMaxRangedSlotsPerTarget = 6
fFormationSpreadRadius = 200.0

[Hotkeys]
uFocusTargetKey = 45
uCommandReturnKey = 46
uToggleAggressionKey = 47
)";

	Settings settings;
	const bool loaded = settings.LoadFromString(ini);
	CHECK(loaded);

	const auto& data = settings.Get();
	CHECK(data.general.enabled == false);
	CHECK(data.general.logLevel == "debug");
	CHECK(data.general.maxSummonsPerOwner == 3);

	CHECK_NEAR(data.timing.updateFrequencyHz, 20.0f, 0.001f);
	CHECK(data.timing.enableTimeSlicing == false);
	CHECK_NEAR(data.timing.commandTTL, 10.0f, 0.001f);

	CHECK_NEAR(data.movement.followMinDistance, 180.0f, 0.001f);
	CHECK_NEAR(data.movement.followMaxDistance, 500.0f, 0.001f);

	CHECK(data.combat.enableCombatLeash == true);
	CHECK_NEAR(data.combat.combatLeashRadius, 2200.0f, 0.001f);
	CHECK(data.combat.returnToOwnerWhenOutsideLeash == true);
	CHECK_NEAR(data.combat.maxTargetRadius, 3000.0f, 0.001f);
	CHECK_NEAR(data.combat.followDistanceThreshold, 600.0f, 0.001f);
	CHECK_NEAR(data.combat.returnDistanceThreshold, 3500.0f, 0.001f);
	CHECK_NEAR(data.combat.engageDistanceThreshold, 400.0f, 0.001f);
	CHECK_NEAR(data.combat.targetLostDebounceTime, 2.0f, 0.001f);

	CHECK(data.recovery.enableCatchUpWarp == true);
	CHECK_NEAR(data.recovery.catchUpDistance, 4000.0f, 0.001f);

	CHECK(data.squad.maxMeleeSlotsPerTarget == 4);
	CHECK(data.squad.maxRangedSlotsPerTarget == 6);
	CHECK_NEAR(data.squad.formationSpreadRadius, 200.0f, 0.001f);

	CHECK(data.hotkeys.focusTargetKey == 45);
	CHECK(data.hotkeys.commandReturnKey == 46);
	CHECK(data.hotkeys.toggleAggressionKey == 47);
}

TEST(settings_load_from_string_unprefixed_and_comments)
{
	const char* ini = R"(
# Alternative unprefixed keys
[general]
enabled = true
loglevel = "trace"
maxsummonsperowner = 5 # Inline comment

[timing]
updatefrequencyhz = 30.0 ; another comment
enabletimeslicing = 1
commandttl = 3.5

[movement]
followmindistance = 150.0
followmaxdistance = 400.0

[combat]
enablecombatleash = 1
combatleashradius = 1800.0
returntoownerwhenoutsideleash = 1
maxtargetradius = 1500.0
followdistancethreshold = 400.0
returndistancethreshold = 1800.0
engagedistancethreshold = 250.0
targetlostdebouncetime = 0.8

[recovery]
enablecatchupwarp = 1
catchupdistance = 3200.0

[squad]
maxmeleeslotspertarget = 3
maxrangedslotspertarget = 5
formationspreadradius = 120.0

[hotkeys]
focustargetkey = 18
commandreturnkey = 19
toggleaggressionkey = 20
)";

	Settings settings;
	CHECK(settings.LoadFromString(ini));

	const auto& data = settings.Get();
	CHECK(data.general.enabled == true);
	CHECK(data.general.logLevel == "trace");
	CHECK(data.general.maxSummonsPerOwner == 5);

	CHECK_NEAR(data.timing.updateFrequencyHz, 30.0f, 0.001f);
	CHECK(data.timing.enableTimeSlicing == true);
	CHECK_NEAR(data.timing.commandTTL, 3.5f, 0.001f);

	CHECK_NEAR(data.movement.followMinDistance, 150.0f, 0.001f);
	CHECK_NEAR(data.movement.followMaxDistance, 400.0f, 0.001f);

	CHECK(data.combat.enableCombatLeash == true);
	CHECK_NEAR(data.combat.combatLeashRadius, 1800.0f, 0.001f);
	CHECK(data.combat.returnToOwnerWhenOutsideLeash == true);
	CHECK_NEAR(data.combat.maxTargetRadius, 1500.0f, 0.001f);
	CHECK_NEAR(data.combat.followDistanceThreshold, 400.0f, 0.001f);
	CHECK_NEAR(data.combat.returnDistanceThreshold, 1800.0f, 0.001f);
	CHECK_NEAR(data.combat.engageDistanceThreshold, 250.0f, 0.001f);
	CHECK_NEAR(data.combat.targetLostDebounceTime, 0.8f, 0.001f);

	CHECK(data.recovery.enableCatchUpWarp == true);
	CHECK_NEAR(data.recovery.catchUpDistance, 3200.0f, 0.001f);

	CHECK(data.squad.maxMeleeSlotsPerTarget == 3);
	CHECK(data.squad.maxRangedSlotsPerTarget == 5);
	CHECK_NEAR(data.squad.formationSpreadRadius, 120.0f, 0.001f);

	CHECK(data.hotkeys.focusTargetKey == 18);
	CHECK(data.hotkeys.commandReturnKey == 19);
	CHECK(data.hotkeys.toggleAggressionKey == 20);
}

TEST(settings_validation_and_clamping)
{
	std::vector<std::string> logs;
	Settings settings([&logs](const char* msg) {
		logs.emplace_back(msg);
	});

	auto& mutableData = settings.GetMutable();
	mutableData.general.maxSummonsPerOwner = 0;       // Should clamp to 1
	mutableData.general.logLevel = "invalid_level";   // Should default to info
	mutableData.timing.updateFrequencyHz = 120.0f;    // Should clamp to 60.0
	mutableData.timing.commandTTL = 0.01f;           // Should clamp to 0.1
	mutableData.movement.followMinDistance = 10.0f;   // Should clamp to 50
	mutableData.movement.followMaxDistance = 20.0f;   // min (50) >= max (20), should adjust to min+100 = 150
	mutableData.combat.followDistanceThreshold = 800.0f;
	mutableData.combat.returnDistanceThreshold = 500.0f; // return <= follow! Should adjust
	mutableData.recovery.catchUpDistance = 100.0f;   // Should clamp to 500
	mutableData.squad.maxMeleeSlotsPerTarget = 50;   // Should clamp to 16

	settings.ValidateAndSanitize();

	const auto& data = settings.Get();
	CHECK(data.general.maxSummonsPerOwner == 1);
	CHECK(data.general.logLevel == "info");
	CHECK_NEAR(data.timing.updateFrequencyHz, 60.0f, 0.001f);
	CHECK_NEAR(data.timing.commandTTL, 0.1f, 0.001f);
	CHECK_NEAR(data.movement.followMinDistance, 50.0f, 0.001f);
	CHECK_NEAR(data.movement.followMaxDistance, 150.0f, 0.001f);
	CHECK(data.combat.returnDistanceThreshold > data.combat.followDistanceThreshold);
	CHECK_NEAR(data.combat.returnDistanceThreshold, 900.0f, 0.001f);
	CHECK_NEAR(data.recovery.catchUpDistance, 500.0f, 0.001f);
	CHECK(data.squad.maxMeleeSlotsPerTarget == 16);

	CHECK(!logs.empty());
}

TEST(settings_serialization_roundtrip)
{
	Settings original;
	auto& mod = original.GetMutable();
	mod.general.enabled = false;
	mod.general.logLevel = "warn";
	mod.general.maxSummonsPerOwner = 7;
	mod.timing.updateFrequencyHz = 25.0f;
	mod.movement.followMinDistance = 175.0f;
	mod.movement.followMaxDistance = 425.0f;
	mod.combat.enableCombatLeash = true;
	mod.combat.combatLeashRadius = 2500.0f;
	mod.combat.returnToOwnerWhenOutsideLeash = true;
	mod.combat.maxTargetRadius = 4500.0f;
	mod.combat.assistPlayerAttacks = false;
	mod.combat.aggressionRadius = 2200.0f;
	mod.recovery.enableCatchUpWarp = true;
	mod.recovery.catchUpDistance = 3800.0f;
	mod.squad.formationSpreadRadius = 250.0f;
	mod.hotkeys.focusTargetKey = 99;

	const std::string serialized = original.Serialize();
	CHECK(!serialized.empty());

	Settings parsed;
	CHECK(parsed.LoadFromString(serialized));

	const auto& d = parsed.Get();
	CHECK(d.general.enabled == false);
	CHECK(d.general.logLevel == "warn");
	CHECK(d.general.maxSummonsPerOwner == 7);
	CHECK_NEAR(d.timing.updateFrequencyHz, 25.0f, 0.001f);
	CHECK_NEAR(d.movement.followMinDistance, 175.0f, 0.001f);
	CHECK_NEAR(d.movement.followMaxDistance, 425.0f, 0.001f);
	CHECK(d.combat.enableCombatLeash == true);
	CHECK_NEAR(d.combat.combatLeashRadius, 2500.0f, 0.001f);
	CHECK(d.combat.returnToOwnerWhenOutsideLeash == true);
	CHECK_NEAR(d.combat.maxTargetRadius, 4500.0f, 0.001f);
	CHECK(d.combat.assistPlayerAttacks == false);
	CHECK_NEAR(d.combat.aggressionRadius, 2200.0f, 0.001f);
	CHECK(d.recovery.enableCatchUpWarp == true);
	CHECK_NEAR(d.recovery.catchUpDistance, 3800.0f, 0.001f);
	CHECK_NEAR(d.squad.formationSpreadRadius, 250.0f, 0.001f);
	CHECK(d.hotkeys.focusTargetKey == 99);
}

TEST(settings_file_save_and_load)
{
	const std::string testFile = "scf_settings_temp_test.ini";

	Settings writer;
	writer.GetMutable().general.maxSummonsPerOwner = 12;
	writer.GetMutable().combat.followDistanceThreshold = 750.0f;
	writer.GetMutable().combat.returnDistanceThreshold = 3000.0f;

	const bool saved = writer.SaveToFile(testFile);
	CHECK(saved);

	Settings reader;
	const bool loaded = reader.LoadFromFile(testFile);
	CHECK(loaded);

	CHECK(reader.Get().general.maxSummonsPerOwner == 12);
	CHECK_NEAR(reader.Get().combat.followDistanceThreshold, 750.0f, 0.001f);
	CHECK_NEAR(reader.Get().combat.returnDistanceThreshold, 3000.0f, 0.001f);

	// Test non-existent file
	Settings failReader;
	CHECK(!failReader.LoadFromFile("non_existent_file_definitely_not_here.ini"));

	// Cleanup
	std::error_code ec;
	std::filesystem::remove(testFile, ec);
}
