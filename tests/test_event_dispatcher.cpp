#include "Core/EventDispatcher.h"
#include "Core/SummonRegistry.h"
#include "check.h"

#include <string>
#include <vector>

using namespace scf::core;

TEST(event_dispatcher_active_effect_apply_and_remove)
{
	std::vector<std::string> logs;
	EventDispatcher dispatcher([&logs](const char* msg) {
		logs.emplace_back(msg);
	});

	SummonRegistry registry;

	// 1. Apply summon effect: Caster 0x14 (Player) -> Target 0x10001 (Familiar)
	ActiveEffectEvent applyEvt;
	applyEvt.casterId = 0x14;
	applyEvt.targetId = 0x10001;
	applyEvt.activeEffectUniqueId = 42;
	applyEvt.isApplied = true;

	const bool registered = dispatcher.HandleActiveEffect(applyEvt, registry, 10.0f);
	CHECK(registered);
	CHECK(registry.IsRegistered(0x10001));
	CHECK(registry.IsActive(0x10001));

	// 2. Reject self-buff (Caster == Target)
	ActiveEffectEvent selfEvt;
	selfEvt.casterId = 0x14;
	selfEvt.targetId = 0x14;
	selfEvt.isApplied = true;
	CHECK(!dispatcher.HandleActiveEffect(selfEvt, registry, 11.0f));

	// 3. Remove summon effect
	ActiveEffectEvent removeEvt;
	removeEvt.casterId = 0x14;
	removeEvt.targetId = 0x10001;
	removeEvt.isApplied = false;

	const bool removed = dispatcher.HandleActiveEffect(removeEvt, registry, 20.0f);
	CHECK(removed);
	const auto* record = registry.FindRecord(0x10001);
	CHECK(record != nullptr);
	CHECK(record->lifecycle == SummonLifecycleState::Dispatched);

	CHECK(!logs.empty());
}

TEST(event_dispatcher_recast_displacement)
{
	EntityID displacedOut = InvalidEntityID;
	EventDispatcher dispatcher(
		nullptr,
		[&displacedOut](EntityID id) {
			displacedOut = id;
		});

	SummonRegistry registry;
	registry.SetDefaultMaxSummonsPerOwner(1);

	// Summon 1
	ActiveEffectEvent evt1{ 0x14, 0x10001, 1, true };
	CHECK(dispatcher.HandleActiveEffect(evt1, registry, 10.0f));
	CHECK(displacedOut == InvalidEntityID);

	// Summon 2 (exceeds limit 1 -> oldest 0x10001 displaced)
	ActiveEffectEvent evt2{ 0x14, 0x10002, 2, true };
	CHECK(dispatcher.HandleActiveEffect(evt2, registry, 15.0f));
	CHECK(displacedOut == 0x10001);

	const auto* rec1 = registry.FindRecord(0x10001);
	const auto* rec2 = registry.FindRecord(0x10002);
	CHECK(rec1->lifecycle == SummonLifecycleState::Displaced);
	CHECK(rec2->lifecycle == SummonLifecycleState::Active);
}

TEST(event_dispatcher_death_event)
{
	EventDispatcher dispatcher;
	SummonRegistry registry;

	// Register summon
	ActiveEffectEvent applyEvt{ 0x14, 0x20001, 10, true };
	dispatcher.HandleActiveEffect(applyEvt, registry, 5.0f);

	// Death event for registered summon
	DeathEvent deathEvt;
	deathEvt.dyingId = 0x20001;
	deathEvt.killerId = 0x99999;
	deathEvt.isDead = true;

	const bool handled = dispatcher.HandleDeath(deathEvt, registry);
	CHECK(handled);

	const auto* rec = registry.FindRecord(0x20001);
	CHECK(rec != nullptr);
	CHECK(rec->isDead == true);
	CHECK(rec->lifecycle == SummonLifecycleState::Dead);

	// Death event for unregistered entity is safely ignored
	DeathEvent randomDeath{ 0x88888, 0x14, true };
	CHECK(!dispatcher.HandleDeath(randomDeath, registry));

	// Non-dead event (isDead = false) is ignored
	DeathEvent falseAlarm{ 0x20001, 0, false };
	CHECK(!dispatcher.HandleDeath(falseAlarm, registry));
}

TEST(event_dispatcher_cell_attach_detach_lifecycle)
{
	EventDispatcher dispatcher;
	SummonRegistry registry;

	// Register summon
	ActiveEffectEvent applyEvt{ 0x14, 0x30001, 20, true };
	dispatcher.HandleActiveEffect(applyEvt, registry, 1.0f);
	CHECK(registry.IsActive(0x30001));

	// 1. Cell detach event (e.g. loading door transition)
	CellAttachDetachEvent detachEvt;
	detachEvt.entityId = 0x30001;
	detachEvt.isAttached = false;

	CHECK(dispatcher.HandleCellAttachDetach(detachEvt, registry));
	const auto* rec = registry.FindRecord(0x30001);
	CHECK(rec->lifecycle == SummonLifecycleState::Suspended);
	CHECK(!registry.IsActive(0x30001));

	// 2. Cell attach event (cell finished loading)
	CellAttachDetachEvent attachEvt;
	attachEvt.entityId = 0x30001;
	attachEvt.isAttached = true;

	CHECK(dispatcher.HandleCellAttachDetach(attachEvt, registry));
	CHECK(rec->lifecycle == SummonLifecycleState::Active);
	CHECK(registry.IsActive(0x30001));

	// 3. Unregistered entity cell transition
	CellAttachDetachEvent strangerEvt{ 0x77777, false };
	CHECK(!dispatcher.HandleCellAttachDetach(strangerEvt, registry));
}
