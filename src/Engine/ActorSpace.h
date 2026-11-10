#pragma once

#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectREFR.h>

namespace scf::engine
{
	// True when two references are in the same loaded space: the same interior cell, or
	// two loaded exterior cells of the same worldspace.
	//
	// A plain `GetParentCell() ==` test is wrong outdoors: the loaded exterior grid is
	// several cells wide, and a summon trailing the player across a cell border (or an
	// enemy standing one cell over) is in a DIFFERENT cell while fully loaded and
	// processed. That test used to drop a FocusTarget the moment the enemy stood across
	// a border and froze all movement orders while the summon followed one cell behind.
	inline bool ShareLoadedSpace(const RE::TESObjectREFR* a_lhs, const RE::TESObjectREFR* a_rhs)
	{
		if (!a_lhs || !a_rhs) {
			return false;
		}
		auto* lhsCell = a_lhs->GetParentCell();
		auto* rhsCell = a_rhs->GetParentCell();
		if (!lhsCell || !rhsCell) {
			return false;
		}
		if (lhsCell == rhsCell) {
			return true;
		}
		if (!lhsCell->IsExteriorCell() || !rhsCell->IsExteriorCell()) {
			return false;
		}
		auto* worldspace = a_lhs->GetWorldspace();
		return worldspace && worldspace == a_rhs->GetWorldspace() && a_lhs->Is3DLoaded() && a_rhs->Is3DLoaded();
	}
}
