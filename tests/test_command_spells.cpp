#include "Core/CombatPolicy.h"
#include "Core/CommandSpells.h"
#include "Core/StateMachine.h"
#include "check.h"

using namespace scf::core;

namespace
{
	CommandSpellIds MakeIds()
	{
		CommandSpellIds ids{};
		ids.holdPositionSpell = 0xFE012811;
		ids.followSpell = 0xFE012812;
		ids.attackTargetSpell = 0xFE012813;
		ids.attackTargetEffect = 0xFE012803;
		return ids;
	}
}

TEST(command_spells_classification_and_loaded_state)
{
	CommandSpellIds empty{};
	CHECK(!empty.IsLoaded());
	CHECK(ClassifyCastSpell(0xFE012811, empty) == CommandSpellKind::None);

	const auto ids = MakeIds();
	CHECK(ids.IsLoaded());
	CHECK(ClassifyCastSpell(ids.holdPositionSpell, ids) == CommandSpellKind::HoldPosition);
	CHECK(ClassifyCastSpell(ids.followSpell, ids) == CommandSpellKind::Follow);
	CHECK(ClassifyCastSpell(ids.attackTargetSpell, ids) == CommandSpellKind::AttackTarget);
	CHECK(ClassifyCastSpell(0x00012FD0, ids) == CommandSpellKind::None);
	CHECK(ClassifyCastSpell(InvalidEntityID, ids) == CommandSpellKind::None);
}

TEST(command_spells_self_cast_hold_and_follow)
{
	const auto ids = MakeIds();
	constexpr EntityID kPlayer = 0x14;

	const auto hold = ResolveSelfCommandCast(kPlayer, kPlayer, ids.holdPositionSpell, ids, 12.5f);
	CHECK(hold.Accepted());
	CHECK(hold.kind == CommandSpellKind::HoldPosition);
	CHECK(hold.rejectReason == CommandSpellRejectReason::None);
	CHECK(hold.command->type == CommandType::SetOrder);
	CHECK(hold.command->order == SummonOrder::WaitGuard);
	CHECK(hold.command->priority == CommandPriority::PlayerDirect);
	CHECK(hold.command->summonId == InvalidEntityID);
	CHECK(hold.command->ownerId == kPlayer);
	CHECK_NEAR(hold.command->timestamp, 12.5f, 0.001f);

	// Follow spell issues Return first so CombatPolicy forbids combat re-entry and
	// pulls engaged summons back to the player before handing off to Follow.
	const auto follow = ResolveSelfCommandCast(kPlayer, kPlayer, ids.followSpell, ids, 15.0f);
	CHECK(follow.Accepted());
	CHECK(follow.kind == CommandSpellKind::Follow);
	CHECK(follow.command->type == CommandType::SetOrder);
	CHECK(follow.command->order == SummonOrder::Return);
	CHECK(follow.command->priority == CommandPriority::PlayerDirect);
	CHECK(follow.command->ownerId == kPlayer);

	// AttackTarget spell cast itself does not issue an immediate order; impact resolves it.
	const auto attackCast = ResolveSelfCommandCast(kPlayer, kPlayer, ids.attackTargetSpell, ids, 16.0f);
	CHECK(!attackCast.Accepted());
	CHECK(attackCast.kind == CommandSpellKind::AttackTarget);
	CHECK(attackCast.rejectReason == CommandSpellRejectReason::None);

	// Non-owner cast is rejected.
	const auto npcCast = ResolveSelfCommandCast(0x00055555, kPlayer, ids.followSpell, ids, 17.0f);
	CHECK(!npcCast.Accepted());
	CHECK(npcCast.rejectReason == CommandSpellRejectReason::NotFromOwner);

	// Unknown spell is rejected.
	const auto unknown = ResolveSelfCommandCast(kPlayer, kPlayer, 0x0004DBA4, ids, 18.0f);
	CHECK(!unknown.Accepted());
	CHECK(unknown.rejectReason == CommandSpellRejectReason::UnknownSpell);
}

TEST(command_spells_attack_target_hit_validation)
{
	const auto ids = MakeIds();
	constexpr EntityID kPlayer = 0x14;
	constexpr EntityID kBandit = 0x00099999;

	// Happy path: living enemy hit by player's aimed AttackTarget effect.
	const auto ok = ResolveAttackTargetHit(
		kPlayer, kPlayer, ids.attackTargetEffect, ids,
		TargetActorCandidate{ kBandit, false, false, false },
		false, 42.0f);
	CHECK(ok.Accepted());
	CHECK(ok.kind == CommandSpellKind::AttackTarget);
	CHECK(ok.rejectReason == CommandSpellRejectReason::None);
	CHECK(ok.command->type == CommandType::SetFocusTarget);
	CHECK(ok.command->focusTargetId == kBandit);
	CHECK(ok.command->priority == CommandPriority::PlayerDirect);
	CHECK(ok.command->summonId == InvalidEntityID);
	CHECK(ok.command->ownerId == kPlayer);
	CHECK_NEAR(ok.command->timestamp, 42.0f, 0.001f);

	// Rejections:
	CHECK(ResolveAttackTargetHit(kPlayer, kPlayer, 0x1234, ids, { kBandit, false, false, false }, false, 1.0f).rejectReason
		== CommandSpellRejectReason::UnknownSpell);
	CHECK(ResolveAttackTargetHit(0x777, kPlayer, ids.attackTargetEffect, ids, { kBandit, false, false, false }, false, 1.0f).rejectReason
		== CommandSpellRejectReason::NotFromOwner);
	CHECK(ResolveAttackTargetHit(kPlayer, kPlayer, ids.attackTargetEffect, ids, { kBandit, false, false, false }, true, 1.0f).rejectReason
		== CommandSpellRejectReason::SquadPassive);
	CHECK(ResolveAttackTargetHit(kPlayer, kPlayer, ids.attackTargetEffect, ids, { InvalidEntityID, false, false, false }, false, 1.0f).rejectReason
		== CommandSpellRejectReason::InvalidTarget);
	CHECK(ResolveAttackTargetHit(kPlayer, kPlayer, ids.attackTargetEffect, ids, { kPlayer, false, true, false }, false, 1.0f).rejectReason
		== CommandSpellRejectReason::TargetIsOwner);
	CHECK(ResolveAttackTargetHit(kPlayer, kPlayer, ids.attackTargetEffect, ids, { kBandit, true, false, false }, false, 1.0f).rejectReason
		== CommandSpellRejectReason::TargetIsDead);
	CHECK(ResolveAttackTargetHit(kPlayer, kPlayer, ids.attackTargetEffect, ids, { kBandit, false, false, true }, false, 1.0f).rejectReason
		== CommandSpellRejectReason::TargetIsAllyOrSummon);
}

TEST(command_spells_drive_state_machine_transitions)
{
	const auto ids = MakeIds();
	constexpr EntityID kPlayer = 0x14;
	constexpr EntityID kBandit = 0x00088888;

	CommandQueue queue;
	StateMachine fsm;
	SummonModel model{};
	model.id = 0x00033333;
	model.ownerId = kPlayer;
	model.state = SummonState::FollowOwner;
	model.order = SummonOrder::Follow;

	// 1. Cast Hold Position -> Guard
	const auto hold = ResolveSelfCommandCast(kPlayer, kPlayer, ids.holdPositionSpell, ids, 1.0f);
	CHECK(hold.Accepted());
	queue.ApplyToModel(*hold.command, model, &fsm);
	CHECK(model.order == SummonOrder::WaitGuard);
	CHECK(model.state == SummonState::Guard);

	// 2. Hit Bandit with Attack Target -> Attack + AcquireTarget (overrides Guard)
	const auto attack = ResolveAttackTargetHit(
		kPlayer, kPlayer, ids.attackTargetEffect, ids,
		{ kBandit, false, false, false }, false, 2.0f);
	CHECK(attack.Accepted());
	queue.ApplyToModel(*attack.command, model, &fsm);
	CHECK(model.order == SummonOrder::Attack);
	CHECK(model.state == SummonState::AcquireTarget);
	CHECK(model.currentTargetId == kBandit);

	// 3. Cast Follow -> Return + clears focus target + CombatPolicy forbids combat
	const auto follow = ResolveSelfCommandCast(kPlayer, kPlayer, ids.followSpell, ids, 3.0f);
	CHECK(follow.Accepted());
	queue.ApplyToModel(*follow.command, model, &fsm);
	CHECK(model.order == SummonOrder::Return);
	CHECK(model.state == SummonState::Return);
	CHECK(model.currentTargetId == InvalidEntityID);
	const auto policy = BuildCombatPolicy(model, false);
	CHECK(!policy.allowCombat);
	CHECK(policy.disableAttacks);
	CHECK(!policy.restrictToTarget);
}
