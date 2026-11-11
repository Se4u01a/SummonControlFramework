#pragma once

namespace RE::BSScript
{
	class IVirtualMachine;
}

namespace scf::papyrus
{
	// Stage 9 - the script registration API (scripts/Source/SCF.psc).
	//
	// SCF.* are global native functions that let a mod script register an actor the engine
	// does not report as one of the player's commanded actors: a scripted summon, a modded
	// creature, anything SCF should track and drive. The registration is a flag on the
	// record (core::SummonRecord::explicitRegistration), which makes it roster-independent
	// (core::SummonRegistry::IsRosterIndependent): the tactical sync never drops it for
	// "not in commandedActors". It is released by UnregisterSummon, by the actor's death,
	// or when the form no longer exists.
	//
	// Owner policy of this stage: only the player. The tactical sync and the AI seats work
	// off the player's roster, so any other owner is refused with a log line and false.
	// A native called without an owner (the Papyrus default `None`) means the player.
	//
	// Call Register() once, from SKSEPluginLoad, with SKSE::GetPapyrusInterface(); it must
	// be the game thread (the load callback is; engine::update::MarkGameThread confirms it
	// on the first tactical tick anyway).
	bool Register(RE::BSScript::IVirtualMachine* a_vm);

	// Records the calling thread as the game thread - the one that owns the registry.
	// Called from the tactical tick (src/Engine/UpdateHook.cpp), which runs there, so the
	// natives do not depend on which thread happened to load the plugin.
	void MarkGameThread() noexcept;
}
