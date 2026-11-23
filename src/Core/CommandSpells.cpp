#include "Core/CommandSpells.h"

namespace scf::core
{
	const char* ToString(CommandSpellKind a_kind) noexcept
	{
		switch (a_kind) {
		case CommandSpellKind::HoldPosition:
			return "HoldPosition";
		case CommandSpellKind::Follow:
			return "Follow";
		case CommandSpellKind::AttackTarget:
			return "AttackTarget";
		case CommandSpellKind::None:
		default:
			return "None";
		}
	}

	const char* ToString(CommandSpellRejectReason a_reason) noexcept
	{
		switch (a_reason) {
		case CommandSpellRejectReason::None:
			return "None";
		case CommandSpellRejectReason::UnknownSpell:
			return "UnknownSpell";
		case CommandSpellRejectReason::NotFromOwner:
			return "NotFromOwner";
		case CommandSpellRejectReason::SquadPassive:
			return "SquadPassive";
		case CommandSpellRejectReason::InvalidTarget:
			return "InvalidTarget";
		case CommandSpellRejectReason::TargetIsOwner:
			return "TargetIsOwner";
		case CommandSpellRejectReason::TargetIsDead:
			return "TargetIsDead";
		case CommandSpellRejectReason::TargetIsAllyOrSummon:
			return "TargetIsAllyOrSummon";
		default:
			return "Unknown";
		}
	}

	CommandSpellKind ClassifyCastSpell(
		EntityID a_spellFormId,
		const CommandSpellIds& a_ids) noexcept
	{
		if (a_spellFormId == InvalidEntityID) {
			return CommandSpellKind::None;
		}
		if (a_ids.holdPositionSpell != InvalidEntityID && a_spellFormId == a_ids.holdPositionSpell) {
			return CommandSpellKind::HoldPosition;
		}
		if (a_ids.followSpell != InvalidEntityID && a_spellFormId == a_ids.followSpell) {
			return CommandSpellKind::Follow;
		}
		if (a_ids.attackTargetSpell != InvalidEntityID && a_spellFormId == a_ids.attackTargetSpell) {
			return CommandSpellKind::AttackTarget;
		}
		return CommandSpellKind::None;
	}

	CommandSpellResolution ResolveSelfCommandCast(
		EntityID a_casterId,
		EntityID a_ownerId,
		EntityID a_spellFormId,
		const CommandSpellIds& a_ids,
		float a_timestamp) noexcept
	{
		CommandSpellResolution result{};
		result.kind = ClassifyCastSpell(a_spellFormId, a_ids);
		if (result.kind == CommandSpellKind::None) {
			result.rejectReason = CommandSpellRejectReason::UnknownSpell;
			return result;
		}

		if (a_casterId == InvalidEntityID || a_ownerId == InvalidEntityID || a_casterId != a_ownerId) {
			result.rejectReason = CommandSpellRejectReason::NotFromOwner;
			return result;
		}

		if (result.kind == CommandSpellKind::AttackTarget) {
			// Aimed projectile resolves on effect impact, not on cast start.
			result.rejectReason = CommandSpellRejectReason::None;
			return result;
		}

		Command cmd{};
		cmd.type = CommandType::SetOrder;
		cmd.priority = CommandPriority::PlayerDirect;
		cmd.summonId = InvalidEntityID;
		cmd.ownerId = a_ownerId;
		cmd.timestamp = a_timestamp;
		cmd.order = (result.kind == CommandSpellKind::HoldPosition) ? SummonOrder::WaitGuard : SummonOrder::Follow;

		result.command = cmd;
		return result;
	}

	CommandSpellResolution ResolveAttackTargetHit(
		EntityID a_casterId,
		EntityID a_ownerId,
		EntityID a_effectFormId,
		const CommandSpellIds& a_ids,
		const TargetActorCandidate& a_target,
		bool a_squadIsPassive,
		float a_timestamp) noexcept
	{
		CommandSpellResolution result{};
		if (a_effectFormId == InvalidEntityID || a_ids.attackTargetEffect == InvalidEntityID ||
		    a_effectFormId != a_ids.attackTargetEffect) {
			result.rejectReason = CommandSpellRejectReason::UnknownSpell;
			return result;
		}

		result.kind = CommandSpellKind::AttackTarget;

		if (a_casterId == InvalidEntityID || a_ownerId == InvalidEntityID || a_casterId != a_ownerId) {
			result.rejectReason = CommandSpellRejectReason::NotFromOwner;
			return result;
		}

		if (a_squadIsPassive) {
			result.rejectReason = CommandSpellRejectReason::SquadPassive;
			return result;
		}

		if (a_target.targetId == InvalidEntityID) {
			result.rejectReason = CommandSpellRejectReason::InvalidTarget;
			return result;
		}

		if (a_target.isOwner || a_target.targetId == a_ownerId) {
			result.rejectReason = CommandSpellRejectReason::TargetIsOwner;
			return result;
		}

		if (a_target.isDead) {
			result.rejectReason = CommandSpellRejectReason::TargetIsDead;
			return result;
		}

		if (a_target.isAllyOrOwnSummon) {
			result.rejectReason = CommandSpellRejectReason::TargetIsAllyOrSummon;
			return result;
		}

		Command cmd{};
		cmd.type = CommandType::SetFocusTarget;
		cmd.priority = CommandPriority::PlayerDirect;
		cmd.summonId = InvalidEntityID;
		cmd.ownerId = a_ownerId;
		cmd.focusTargetId = a_target.targetId;
		cmd.timestamp = a_timestamp;

		result.command = cmd;
		return result;
	}
}
