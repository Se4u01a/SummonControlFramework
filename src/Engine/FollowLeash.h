#pragma once

#include "Core/EntityID.h"

#include <unordered_set>

namespace RE
{
	class Actor;
}

namespace scf::engine::leash
{
	// The vanilla "summoned creature follows its caster" behaviour, and how SCF switches
	// it off while an order needs the summon away from the player.
	//
	// Engine facts (SkyrimSE 1.5.97, verified 2026-11-11):
	//   * function id 36410 (+5DBA80) is the ONLY reader of the GMSTs
	//     fSummonedCreatureMinFollowDist / fSummonedCreatureMaxFollowDist. It builds the
	//     summon's runtime package through +5DD030 -> +43D460, which instantiates the
	//     package TEMPLATE named "FollowTo" (string at +160D9F8) with the two GMST values
	//     as its float inputs, and puts it on the actor (Character vfunc 0xDF,
	//     PutCreatedPackage).
	//   * Skyrim.esm PACK 00025E6D "FollowTo": inputs SingleRef (uid 2, "Target to
	//     Follow"), Float (uid 4, "Min Radius"), Float (uid 5, "Max Radius:"),
	//     Location (uid 8), Bool (uid 6).
	//   * BGSPackageDataFloat (vtable id 252945 -> +1605F40): the value is the float at
	//     +0x10 (Assign, slot 3, `mov eax,[rax+0x10]; mov [rbx+0x10],eax`; LoadBuffer,
	//     slot 8, reads 4 bytes into [rcx+0x10]).
	// So a summon's own package keeps pulling it back to within Min Radius of the player.
	// During MoveTo that is the "stops and looks at me" the field reported; during an
	// attack approach it drags the summon off its target.
	//
	// While released, the summon's FollowTo instance gets Min/Max Radius = kReleasedRadius
	// (it is always "close enough" and generates no locomotion); restoring writes the
	// current GMST values back. Only an actor whose running package is a FollowTo
	// instance is touched, and only managed summons are passed in. Main thread only.

	//
	// Recognition (field log 2026-11-11 00:08: the instance has templateParent = 0):
	// the creation seat (SE 1.5.97: call site id 36410 + 0x84 -> id 36422) records the
	// package built for each summon and builds it already released when the summon is
	// under an away order; otherwise the package is recognised by its shape
	// (core::IsSummonFollowPackageShape).

	// kDataLoaded. Installs the creation seat on SE 1.5.97; false elsewhere (shape only).
	bool Install();

	void UpdateForSummon(RE::Actor* a_summon, bool a_released);

	void RetainOnly(const std::unordered_set<core::EntityID>& a_liveSummons);
	void ResetAll();
}
