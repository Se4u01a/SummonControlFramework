#pragma once

#include "Core/CommandQueue.h"
#include "Core/EntityID.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace scf::core
{
	inline constexpr std::string_view kCommandSpellsPluginName = "SummonControlFramework.esp";

	// Local FormIDs inside SummonControlFramework.esp (ESL range 0x800..0xFFF).
	inline constexpr std::uint32_t kAttackTargetProjLocalId = 0x000800;
	inline constexpr std::uint32_t kHoldPositionEffectLocalId = 0x000801;
	inline constexpr std::uint32_t kFollowEffectLocalId = 0x000802;
	inline constexpr std::uint32_t kAttackTargetEffectLocalId = 0x000803;
	inline constexpr std::uint32_t kHoldPositionSpellLocalId = 0x000811;
	inline constexpr std::uint32_t kFollowSpellLocalId = 0x000812;
	inline constexpr std::uint32_t kAttackTargetSpellLocalId = 0x000813;

	// EditorIDs matching SummonControlFramework.esp.
	inline constexpr std::string_view kHoldPositionSpellEditorId = "SCF_CmdHoldPosition";
	inline constexpr std::string_view kFollowSpellEditorId = "SCF_CmdFollow";
	inline constexpr std::string_view kAttackTargetSpellEditorId = "SCF_CmdAttackTarget";
	inline constexpr std::string_view kAttackTargetEffectEditorId = "SCF_CmdAttackTarget_Effect";

	enum class CommandSpellKind : std::uint8_t
	{
		None = 0,
		HoldPosition,
		Follow,
		AttackTarget
	};

	enum class CommandSpellRejectReason : std::uint8_t
	{
		None = 0,
		UnknownSpell,
		NotFromOwner,
		SquadPassive,
		InvalidTarget,
		TargetIsOwner,
		TargetIsDead,
		TargetIsAllyOrSummon
	};

	struct CommandSpellIds
	{
		EntityID holdPositionSpell{ InvalidEntityID };
		EntityID followSpell{ InvalidEntityID };
		EntityID attackTargetSpell{ InvalidEntityID };
		EntityID attackTargetEffect{ InvalidEntityID };

		[[nodiscard]] bool IsLoaded() const noexcept
		{
			return holdPositionSpell != InvalidEntityID &&
			       followSpell != InvalidEntityID &&
			       attackTargetSpell != InvalidEntityID &&
			       attackTargetEffect != InvalidEntityID;
		}
	};

	struct TargetActorCandidate
	{
		EntityID targetId{ InvalidEntityID };
		bool isDead{ false };
		bool isOwner{ false };
		bool isAllyOrOwnSummon{ false };
	};

	struct CommandSpellResolution
	{
		CommandSpellKind kind{ CommandSpellKind::None };
		CommandSpellRejectReason rejectReason{ CommandSpellRejectReason::None };
		std::optional<Command> command{};

		[[nodiscard]] bool Accepted() const noexcept
		{
			return command.has_value();
		}
	};

	[[nodiscard]] const char* ToString(CommandSpellKind a_kind) noexcept;
	[[nodiscard]] const char* ToString(CommandSpellRejectReason a_reason) noexcept;

	[[nodiscard]] CommandSpellKind ClassifyCastSpell(
		EntityID a_spellFormId,
		const CommandSpellIds& a_ids) noexcept;

	// Resolves TESSpellCastEvent for self-cast command spells (HoldPosition, Follow).
	// AttackTarget is recognized with kind=AttackTarget and empty command because it
	// resolves when its aimed projectile hits an actor (TESMagicEffectApplyEvent).
	[[nodiscard]] CommandSpellResolution ResolveSelfCommandCast(
		EntityID a_casterId,
		EntityID a_ownerId,
		EntityID a_spellFormId,
		const CommandSpellIds& a_ids,
		float a_timestamp) noexcept;

	// Resolves TESMagicEffectApplyEvent when AttackTarget's aimed impulse hits an actor.
	[[nodiscard]] CommandSpellResolution ResolveAttackTargetHit(
		EntityID a_casterId,
		EntityID a_ownerId,
		EntityID a_effectFormId,
		const CommandSpellIds& a_ids,
		const TargetActorCandidate& a_target,
		bool a_squadIsPassive,
		float a_timestamp) noexcept;
}
