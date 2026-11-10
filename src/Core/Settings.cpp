#include "Core/Settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace scf::core
{
	namespace
	{
		std::string Trim(std::string_view a_str)
		{
			const auto first = a_str.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return "";
			}
			const auto last = a_str.find_last_not_of(" \t\r\n");
			return std::string(a_str.substr(first, last - first + 1));
		}

		std::string ToLower(std::string_view a_str)
		{
			std::string result(a_str);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return result;
		}

		bool ParseBool(std::string_view a_str, bool a_default)
		{
			const std::string lower = ToLower(Trim(a_str));
			if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
				return true;
			}
			if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
				return false;
			}
			return a_default;
		}

		float ParseFloat(std::string_view a_str, float a_default)
		{
			try {
				const std::string s = Trim(a_str);
				if (!s.empty()) {
					return std::stof(s);
				}
			} catch (...) {
			}
			return a_default;
		}

		std::size_t ParseSizeT(std::string_view a_str, std::size_t a_default)
		{
			try {
				const std::string s = Trim(a_str);
				if (!s.empty()) {
					return static_cast<std::size_t>(std::stoul(s));
				}
			} catch (...) {
			}
			return a_default;
		}

		std::uint32_t ParseUInt32(std::string_view a_str, std::uint32_t a_default)
		{
			try {
				const std::string s = Trim(a_str);
				if (!s.empty()) {
					return static_cast<std::uint32_t>(std::stoul(s));
				}
			} catch (...) {
			}
			return a_default;
		}

		std::string ParseString(std::string_view a_str, std::string_view a_default)
		{
			std::string s = Trim(a_str);
			if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
				s = s.substr(1, s.size() - 2);
			}
			return s.empty() ? std::string(a_default) : s;
		}
	}

	void Settings::ResetToDefaults() noexcept
	{
		_data = SettingsData{};
		if (_logCallback) {
			_logCallback("[Settings] Defaults restored.");
		}
	}

	void Settings::ValidateAndSanitize() noexcept
	{
		char logBuf[256];

		// General
		if (_data.general.maxSummonsPerOwner < 1) {
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: maxSummonsPerOwner (%zu) was < 1, clamping to 1.",
					_data.general.maxSummonsPerOwner);
				_logCallback(logBuf);
			}
			_data.general.maxSummonsPerOwner = 1;
		} else if (_data.general.maxSummonsPerOwner > 32) {
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: maxSummonsPerOwner (%zu) exceeds 32, clamping to 32.",
					_data.general.maxSummonsPerOwner);
				_logCallback(logBuf);
			}
			_data.general.maxSummonsPerOwner = 32;
		}

		const std::string lowerLevel = ToLower(_data.general.logLevel);
		if (lowerLevel != "trace" && lowerLevel != "debug" && lowerLevel != "info" &&
		    lowerLevel != "warn" && lowerLevel != "error" && lowerLevel != "critical") {
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: invalid logLevel '%s', defaulting to 'info'.",
					_data.general.logLevel.c_str());
				_logCallback(logBuf);
			}
			_data.general.logLevel = "info";
		}

		// Timing
		if (_data.timing.updateFrequencyHz < 1.0f || _data.timing.updateFrequencyHz > 60.0f) {
			const float clamped = std::clamp(_data.timing.updateFrequencyHz, 1.0f, 60.0f);
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: updateFrequencyHz (%.2f) out of range [1, 60], clamping to %.2f.",
					_data.timing.updateFrequencyHz, clamped);
				_logCallback(logBuf);
			}
			_data.timing.updateFrequencyHz = clamped;
		}

		if (_data.timing.commandTTL < 0.1f || _data.timing.commandTTL > 60.0f) {
			const float clamped = std::clamp(_data.timing.commandTTL, 0.1f, 60.0f);
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: commandTTL (%.2f) out of range [0.1, 60], clamping to %.2f.",
					_data.timing.commandTTL, clamped);
				_logCallback(logBuf);
			}
			_data.timing.commandTTL = clamped;
		}

		// Combat
		if (_data.combat.maxTargetRadius < 100.0f || _data.combat.maxTargetRadius > 10000.0f) {
			const float clamped = std::clamp(_data.combat.maxTargetRadius, 100.0f, 10000.0f);
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: maxTargetRadius (%.1f) out of range [100, 10000], clamping to %.1f.",
					_data.combat.maxTargetRadius, clamped);
				_logCallback(logBuf);
			}
			_data.combat.maxTargetRadius = clamped;
		}

		if (_data.combat.followDistanceThreshold < 50.0f || _data.combat.followDistanceThreshold > 2000.0f) {
			const float clamped = std::clamp(_data.combat.followDistanceThreshold, 50.0f, 2000.0f);
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: followDistanceThreshold (%.1f) out of range [50, 2000], clamping to %.1f.",
					_data.combat.followDistanceThreshold, clamped);
				_logCallback(logBuf);
			}
			_data.combat.followDistanceThreshold = clamped;
		}

		if (_data.combat.returnDistanceThreshold <= _data.combat.followDistanceThreshold) {
			const float adjusted = _data.combat.followDistanceThreshold + 100.0f;
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: returnDistanceThreshold (%.1f) <= followDistanceThreshold (%.1f), setting to %.1f.",
					_data.combat.returnDistanceThreshold, _data.combat.followDistanceThreshold, adjusted);
				_logCallback(logBuf);
			}
			_data.combat.returnDistanceThreshold = adjusted;
		}

		if (_data.combat.engageDistanceThreshold < 50.0f || _data.combat.engageDistanceThreshold > 1000.0f) {
			const float clamped = std::clamp(_data.combat.engageDistanceThreshold, 50.0f, 1000.0f);
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: engageDistanceThreshold (%.1f) out of range [50, 1000], clamping to %.1f.",
					_data.combat.engageDistanceThreshold, clamped);
				_logCallback(logBuf);
			}
			_data.combat.engageDistanceThreshold = clamped;
		}

		if (_data.combat.targetLostDebounceTime < 0.1f || _data.combat.targetLostDebounceTime > 10.0f) {
			const float clamped = std::clamp(_data.combat.targetLostDebounceTime, 0.1f, 10.0f);
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: targetLostDebounceTime (%.2f) out of range [0.1, 10], clamping to %.2f.",
					_data.combat.targetLostDebounceTime, clamped);
				_logCallback(logBuf);
			}
			_data.combat.targetLostDebounceTime = clamped;
		}

		// Squad
		if (_data.squad.maxMeleeSlotsPerTarget < 1 || _data.squad.maxMeleeSlotsPerTarget > 16) {
			const std::size_t clamped = std::clamp(_data.squad.maxMeleeSlotsPerTarget, std::size_t{ 1 }, std::size_t{ 16 });
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: maxMeleeSlotsPerTarget (%zu) out of range [1, 16], clamping to %zu.",
					_data.squad.maxMeleeSlotsPerTarget, clamped);
				_logCallback(logBuf);
			}
			_data.squad.maxMeleeSlotsPerTarget = clamped;
		}

		if (_data.squad.maxRangedSlotsPerTarget < 1 || _data.squad.maxRangedSlotsPerTarget > 16) {
			const std::size_t clamped = std::clamp(_data.squad.maxRangedSlotsPerTarget, std::size_t{ 1 }, std::size_t{ 16 });
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: maxRangedSlotsPerTarget (%zu) out of range [1, 16], clamping to %zu.",
					_data.squad.maxRangedSlotsPerTarget, clamped);
				_logCallback(logBuf);
			}
			_data.squad.maxRangedSlotsPerTarget = clamped;
		}

		if (_data.squad.formationSpreadRadius < 50.0f || _data.squad.formationSpreadRadius > 1000.0f) {
			const float clamped = std::clamp(_data.squad.formationSpreadRadius, 50.0f, 1000.0f);
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[Settings] Warning: formationSpreadRadius (%.1f) out of range [50, 1000], clamping to %.1f.",
					_data.squad.formationSpreadRadius, clamped);
				_logCallback(logBuf);
			}
			_data.squad.formationSpreadRadius = clamped;
		}
	}

	bool Settings::LoadFromString(std::string_view a_iniContent)
	{
		std::istringstream stream{ std::string(a_iniContent) };
		std::string line;
		std::string currentSection;

		while (std::getline(stream, line)) {
			// Strip comments starting with '#' or ';'
			const auto commentPos = line.find_first_of(";#");
			if (commentPos != std::string::npos) {
				line.erase(commentPos);
			}

			const std::string trimmed = Trim(line);
			if (trimmed.empty()) {
				continue;
			}

			// Check for section header [Section]
			if (trimmed.front() == '[' && trimmed.back() == ']') {
				currentSection = ToLower(Trim(trimmed.substr(1, trimmed.size() - 2)));
				continue;
			}

			// Key=Value
			const auto eqPos = trimmed.find('=');
			if (eqPos == std::string::npos) {
				continue;
			}

			const std::string key = ToLower(Trim(trimmed.substr(0, eqPos)));
			const std::string val = Trim(trimmed.substr(eqPos + 1));

			if (currentSection == "general") {
				if (key == "enabled" || key == "benabled") {
					_data.general.enabled = ParseBool(val, _data.general.enabled);
				} else if (key == "loglevel" || key == "sloglevel") {
					_data.general.logLevel = ParseString(val, _data.general.logLevel);
				} else if (key == "maxsummonsperowner" || key == "umaxsummonsperowner") {
					_data.general.maxSummonsPerOwner = ParseSizeT(val, _data.general.maxSummonsPerOwner);
				}
			} else if (currentSection == "collision") {
				if (key == "enablecollisionbypass" || key == "benablecollisionbypass") {
					_data.collision.enableCollisionBypass = ParseBool(val, _data.collision.enableCollisionBypass);
				} else if (key == "enablesummontosummonbypass" || key == "benablesummontosummonbypass") {
					_data.collision.enableSummonToSummonBypass = ParseBool(val, _data.collision.enableSummonToSummonBypass);
				}
			} else if (currentSection == "timing") {
				if (key == "updatefrequencyhz" || key == "fupdatefrequencyhz") {
					_data.timing.updateFrequencyHz = ParseFloat(val, _data.timing.updateFrequencyHz);
				} else if (key == "enabletimeslicing" || key == "benabletimeslicing") {
					_data.timing.enableTimeSlicing = ParseBool(val, _data.timing.enableTimeSlicing);
				} else if (key == "commandttl" || key == "fcommandttl") {
					_data.timing.commandTTL = ParseFloat(val, _data.timing.commandTTL);
				}
			} else if (currentSection == "combat") {
				if (key == "maxtargetradius" || key == "fmaxtargetradius") {
					_data.combat.maxTargetRadius = ParseFloat(val, _data.combat.maxTargetRadius);
				} else if (key == "followdistancethreshold" || key == "ffollowdistancethreshold") {
					_data.combat.followDistanceThreshold = ParseFloat(val, _data.combat.followDistanceThreshold);
				} else if (key == "returndistancethreshold" || key == "freturndistancethreshold") {
					_data.combat.returnDistanceThreshold = ParseFloat(val, _data.combat.returnDistanceThreshold);
				} else if (key == "engagedistancethreshold" || key == "fengagedistancethreshold") {
					_data.combat.engageDistanceThreshold = ParseFloat(val, _data.combat.engageDistanceThreshold);
				} else if (key == "targetlostdebouncetime" || key == "ftargetlostdebouncetime") {
					_data.combat.targetLostDebounceTime = ParseFloat(val, _data.combat.targetLostDebounceTime);
				}
			} else if (currentSection == "squad") {
				if (key == "maxmeleeslotspertarget" || key == "umaxmeleeslotspertarget") {
					_data.squad.maxMeleeSlotsPerTarget = ParseSizeT(val, _data.squad.maxMeleeSlotsPerTarget);
				} else if (key == "maxrangedslotspertarget" || key == "umaxrangedslotspertarget") {
					_data.squad.maxRangedSlotsPerTarget = ParseSizeT(val, _data.squad.maxRangedSlotsPerTarget);
				} else if (key == "formationspreadradius" || key == "fformationspreadradius") {
					_data.squad.formationSpreadRadius = ParseFloat(val, _data.squad.formationSpreadRadius);
				}
			} else if (currentSection == "hotkeys") {
				if (key == "focustargetkey" || key == "ufocustargetkey") {
					_data.hotkeys.focusTargetKey = ParseUInt32(val, _data.hotkeys.focusTargetKey);
				} else if (key == "commandreturnkey" || key == "ucommandreturnkey") {
					_data.hotkeys.commandReturnKey = ParseUInt32(val, _data.hotkeys.commandReturnKey);
				} else if (key == "toggleaggressionkey" || key == "utoggleaggressionkey") {
					_data.hotkeys.toggleAggressionKey = ParseUInt32(val, _data.hotkeys.toggleAggressionKey);
				} else if (key == "movetokey" || key == "umovetokey") {
					_data.hotkeys.moveToKey = ParseUInt32(val, _data.hotkeys.moveToKey);
				} else if (key == "waitguardkey" || key == "uwaitguardkey") {
					_data.hotkeys.waitGuardKey = ParseUInt32(val, _data.hotkeys.waitGuardKey);
				} else if (key == "followkey" || key == "ufollowkey") {
					_data.hotkeys.followKey = ParseUInt32(val, _data.hotkeys.followKey);
				}
			} else if (currentSection == "friendlyfire") {
				if (key == "enablesummonprotection" || key == "benablesummonprotection" || key == "enabled" || key == "benabled") {
					_data.friendlyFire.enabled = ParseBool(val, _data.friendlyFire.enabled);
				} else if (key == "protectplayerownedsummons" || key == "bprotectplayerownedsummons") {
					_data.friendlyFire.protectPlayerOwnedSummons = ParseBool(val, _data.friendlyFire.protectPlayerOwnedSummons);
				} else if (key == "protectallyownedsummons" || key == "bprotectallyownedsummons") {
					_data.friendlyFire.protectAllyOwnedSummons = ParseBool(val, _data.friendlyFire.protectAllyOwnedSummons);
				} else if (key == "filterplayerattacks" || key == "bfilterplayerattacks") {
					_data.friendlyFire.filterPlayerAttacks = ParseBool(val, _data.friendlyFire.filterPlayerAttacks);
				} else if (key == "filterplayerallies" || key == "bfilterplayerallies") {
					_data.friendlyFire.filterPlayerAllies = ParseBool(val, _data.friendlyFire.filterPlayerAllies);
				} else if (key == "filterplayerownedsummons" || key == "bfilterplayerownedsummons") {
					_data.friendlyFire.filterPlayerOwnedSummons = ParseBool(val, _data.friendlyFire.filterPlayerOwnedSummons);
				} else if (key == "filteralliedsummons" || key == "bfilteralliedsummons") {
					_data.friendlyFire.filterAlliedSummons = ParseBool(val, _data.friendlyFire.filterAlliedSummons);
				} else if (key == "blockmeleedamage" || key == "bblockmeleedamage") {
					_data.friendlyFire.blockMeleeDamage = ParseBool(val, _data.friendlyFire.blockMeleeDamage);
				} else if (key == "blockarrowdamage" || key == "bblockarrowdamage") {
					_data.friendlyFire.blockArrowDamage = ParseBool(val, _data.friendlyFire.blockArrowDamage);
				} else if (key == "blockphysicalprojectiledamage" || key == "bblockphysicalprojectiledamage") {
					_data.friendlyFire.blockPhysicalProjectileDamage = ParseBool(val, _data.friendlyFire.blockPhysicalProjectileDamage);
				} else if (key == "blockmagicprojectiledamage" || key == "bblockmagicprojectiledamage") {
					_data.friendlyFire.blockMagicProjectileDamage = ParseBool(val, _data.friendlyFire.blockMagicProjectileDamage);
				} else if (key == "blockconcentrationdamage" || key == "bblockconcentrationdamage") {
					_data.friendlyFire.blockConcentrationDamage = ParseBool(val, _data.friendlyFire.blockConcentrationDamage);
				} else if (key == "blockareamagicdamage" || key == "bblockareamagicdamage") {
					_data.friendlyFire.blockAreaMagicDamage = ParseBool(val, _data.friendlyFire.blockAreaMagicDamage);
				} else if (key == "blockshoutdamage" || key == "bblockshoutdamage") {
					_data.friendlyFire.blockShoutDamage = ParseBool(val, _data.friendlyFire.blockShoutDamage);
				} else if (key == "blockenchantmentdamage" || key == "bblockenchantmentdamage") {
					_data.friendlyFire.blockEnchantmentDamage = ParseBool(val, _data.friendlyFire.blockEnchantmentDamage);
				} else if (key == "meleepassthrough" || key == "bmeleepassthrough") {
					_data.friendlyFire.meleePassThrough = ParseBool(val, _data.friendlyFire.meleePassThrough);
				} else if (key == "arrowpassthrough" || key == "barrowpassthrough") {
					_data.friendlyFire.arrowPassThrough = ParseBool(val, _data.friendlyFire.arrowPassThrough);
				} else if (key == "physicalprojectilepassthrough" || key == "bphysicalprojectilepassthrough") {
					_data.friendlyFire.physicalProjectilePassThrough = ParseBool(val, _data.friendlyFire.physicalProjectilePassThrough);
				} else if (key == "magicprojectilepassthrough" || key == "bmagicprojectilepassthrough") {
					_data.friendlyFire.magicProjectilePassThrough = ParseBool(val, _data.friendlyFire.magicProjectilePassThrough);
				} else if (key == "suppressfriendlyaggression" || key == "bsuppressfriendlyaggression") {
					_data.friendlyFire.suppressFriendlyAggression = ParseBool(val, _data.friendlyFire.suppressFriendlyAggression);
				} else if (key == "suppressfriendlykillmoves" || key == "bsuppressfriendlykillmoves") {
					_data.friendlyFire.suppressFriendlyKillmoves = ParseBool(val, _data.friendlyFire.suppressFriendlyKillmoves);
				} else if (key == "suppressfriendlyhitreactions" || key == "bsuppressfriendlyhitreactions") {
					_data.friendlyFire.suppressFriendlyHitReactions = ParseBool(val, _data.friendlyFire.suppressFriendlyHitReactions);
				}
			}
		}

		ValidateAndSanitize();

		if (_logCallback) {
			_logCallback("[Settings] Settings loaded and validated successfully.");
		}

		return true;
	}

	bool Settings::LoadFromFile(const std::string& a_filePath)
	{
		std::ifstream file(a_filePath);
		if (!file.is_open()) {
			if (_logCallback) {
				char logBuf[512];
				std::snprintf(logBuf, sizeof(logBuf), "[Settings] Could not open file for reading: %s", a_filePath.c_str());
				_logCallback(logBuf);
			}
			return false;
		}

		std::stringstream buffer;
		buffer << file.rdbuf();

		const bool ok = LoadFromString(buffer.str());
		if (ok && _logCallback) {
			char logBuf[512];
			std::snprintf(logBuf, sizeof(logBuf), "[Settings] Successfully loaded configuration from: %s", a_filePath.c_str());
			_logCallback(logBuf);
		}
		return ok;
	}

	bool Settings::SaveToFile(const std::string& a_filePath) const
	{
		std::ofstream file(a_filePath);
		if (!file.is_open()) {
			if (_logCallback) {
				char logBuf[512];
				std::snprintf(logBuf, sizeof(logBuf), "[Settings] Could not open file for writing: %s", a_filePath.c_str());
				_logCallback(logBuf);
			}
			return false;
		}

		file << Serialize();
		if (!file.good()) {
			if (_logCallback) {
				char logBuf[512];
				std::snprintf(logBuf, sizeof(logBuf), "[Settings] Error writing configuration to: %s", a_filePath.c_str());
				_logCallback(logBuf);
			}
			return false;
		}

		if (_logCallback) {
			char logBuf[512];
			std::snprintf(logBuf, sizeof(logBuf), "[Settings] Successfully saved configuration to: %s", a_filePath.c_str());
			_logCallback(logBuf);
		}
		return true;
	}

	std::string Settings::Serialize() const
	{
		std::ostringstream out;

		out << "; =====================================================================\n";
		out << "; Summon Control Framework Configuration\n";
		out << "; =====================================================================\n\n";

		out << "[General]\n";
		out << "bEnabled = " << (_data.general.enabled ? "true" : "false") << "\n";
		out << "sLogLevel = " << _data.general.logLevel << "\n";
		out << "uMaxSummonsPerOwner = " << _data.general.maxSummonsPerOwner << "\n\n";

		out << "[Collision]\n";
		out << "bEnableCollisionBypass = " << (_data.collision.enableCollisionBypass ? "true" : "false") << "\n";
		out << "bEnableSummonToSummonBypass = " << (_data.collision.enableSummonToSummonBypass ? "true" : "false") << "\n\n";

		out << "[Timing]\n";
		out << "fUpdateFrequencyHz = " << _data.timing.updateFrequencyHz << "\n";
		out << "bEnableTimeSlicing = " << (_data.timing.enableTimeSlicing ? "true" : "false") << "\n";
		out << "fCommandTTL = " << _data.timing.commandTTL << "\n\n";

		out << "[Combat]\n";
		out << "fMaxTargetRadius = " << _data.combat.maxTargetRadius << "\n";
		out << "fFollowDistanceThreshold = " << _data.combat.followDistanceThreshold << "\n";
		out << "fReturnDistanceThreshold = " << _data.combat.returnDistanceThreshold << "\n";
		out << "fEngageDistanceThreshold = " << _data.combat.engageDistanceThreshold << "\n";
		out << "fTargetLostDebounceTime = " << _data.combat.targetLostDebounceTime << "\n\n";

		out << "[Squad]\n";
		out << "uMaxMeleeSlotsPerTarget = " << _data.squad.maxMeleeSlotsPerTarget << "\n";
		out << "uMaxRangedSlotsPerTarget = " << _data.squad.maxRangedSlotsPerTarget << "\n";
		out << "fFormationSpreadRadius = " << _data.squad.formationSpreadRadius << "\n\n";

		out << "[Hotkeys]\n";
		out << "uFocusTargetKey = " << _data.hotkeys.focusTargetKey << "\n";
		out << "uCommandReturnKey = " << _data.hotkeys.commandReturnKey << "\n";
		out << "uToggleAggressionKey = " << _data.hotkeys.toggleAggressionKey << "\n";
		out << "uMoveToKey = " << _data.hotkeys.moveToKey << "\n";
		out << "uWaitGuardKey = " << _data.hotkeys.waitGuardKey << "\n";
		out << "uFollowKey = " << _data.hotkeys.followKey << "\n\n";

		out << "[FriendlyFire]\n";
		out << "bEnableSummonProtection = " << (_data.friendlyFire.enabled ? "true" : "false") << "\n";
		out << "bProtectPlayerOwnedSummons = " << (_data.friendlyFire.protectPlayerOwnedSummons ? "true" : "false") << "\n";
		out << "bProtectAllyOwnedSummons = " << (_data.friendlyFire.protectAllyOwnedSummons ? "true" : "false") << "\n\n";
		out << "bFilterPlayerAttacks = " << (_data.friendlyFire.filterPlayerAttacks ? "true" : "false") << "\n";
		out << "bFilterPlayerAllies = " << (_data.friendlyFire.filterPlayerAllies ? "true" : "false") << "\n";
		out << "bFilterPlayerOwnedSummons = " << (_data.friendlyFire.filterPlayerOwnedSummons ? "true" : "false") << "\n";
		out << "bFilterAlliedSummons = " << (_data.friendlyFire.filterAlliedSummons ? "true" : "false") << "\n\n";
		out << "bBlockMeleeDamage = " << (_data.friendlyFire.blockMeleeDamage ? "true" : "false") << "\n";
		out << "bBlockArrowDamage = " << (_data.friendlyFire.blockArrowDamage ? "true" : "false") << "\n";
		out << "bBlockPhysicalProjectileDamage = " << (_data.friendlyFire.blockPhysicalProjectileDamage ? "true" : "false") << "\n";
		out << "bBlockMagicProjectileDamage = " << (_data.friendlyFire.blockMagicProjectileDamage ? "true" : "false") << "\n";
		out << "bBlockConcentrationDamage = " << (_data.friendlyFire.blockConcentrationDamage ? "true" : "false") << "\n";
		out << "bBlockAreaMagicDamage = " << (_data.friendlyFire.blockAreaMagicDamage ? "true" : "false") << "\n";
		out << "bBlockShoutDamage = " << (_data.friendlyFire.blockShoutDamage ? "true" : "false") << "\n";
		out << "bBlockEnchantmentDamage = " << (_data.friendlyFire.blockEnchantmentDamage ? "true" : "false") << "\n\n";
		out << "bMeleePassThrough = " << (_data.friendlyFire.meleePassThrough ? "true" : "false") << "\n";
		out << "bArrowPassThrough = " << (_data.friendlyFire.arrowPassThrough ? "true" : "false") << "\n";
		out << "bPhysicalProjectilePassThrough = " << (_data.friendlyFire.physicalProjectilePassThrough ? "true" : "false") << "\n";
		out << "bMagicProjectilePassThrough = " << (_data.friendlyFire.magicProjectilePassThrough ? "true" : "false") << "\n\n";
		out << "bSuppressFriendlyAggression = " << (_data.friendlyFire.suppressFriendlyAggression ? "true" : "false") << "\n";
		out << "bSuppressFriendlyKillmoves = " << (_data.friendlyFire.suppressFriendlyKillmoves ? "true" : "false") << "\n";
		out << "bSuppressFriendlyHitReactions = " << (_data.friendlyFire.suppressFriendlyHitReactions ? "true" : "false") << "\n";

		return out.str();
	}
}
