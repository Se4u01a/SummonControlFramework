#include "Engine/SummonMovement.h"
#include "Engine/ActorSpace.h"
#include "Engine/FollowLeash.h"
#include "Core/CatchUp.h"
#include "Core/Formation.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/A/ActorState.h>
#include <RE/B/BSAtomic.h>
#include <RE/B/bhkPickData.h>
#include <RE/B/bhkWorld.h>
#include <RE/C/CFilter.h>
#include <RE/C/CollisionLayers.h>
#include <RE/H/hkpCollidable.h>
#include <RE/N/NiPoint3.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/ProcessLists.h>
#include <RE/T/TES.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectREFR.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace scf::engine::movement
{
	namespace
	{
		// ------------------------------------------------------------------
		// Engine primitives.
		//
		// None of these three functions is declared by the CommonLibSSE-NG revision
		// this project builds against, so they are resolved through the Address
		// Library here. Ids and signature were checked against the unpacked 1.5.97
		// image and cross-checked with APMF's verified address table
		// (APMF/Docs/ADDRESS-TABLE-2026-09-15.md, rows MovementDeny.SetDontMove /
		// KeepOffsetFromActor / ClearKeepOffsetFromActor):
		//
		//   * id 36870 -> SkyrimSE.exe+603990. The function loads
		//     Actor::movementController (Actor+0x148, `BSTSmartPointer
		//     <MovementControllerNPC>`), RTTI-casts it to
		//     IMovementSetKeepOffsetFromActor (the type descriptor is present in the
		//     image) and calls that interface's slot 1 with the nine-field record
		//     {ActorHandle target, NiPoint3 offset, NiPoint3 angle, float catchUpRadius,
		//     float followRadius}. That record is exactly Papyrus
		//     Actor.KeepOffsetFromActor's argument list.
		//   * id 36871 -> SkyrimSE.exe+603AC0. Same interface, slot 0: clear.
		//   * id 36490 -> SkyrimSE.exe+5E3330. Actor::SetDontMove(bool).
		//
		// Offset semantics (CK wiki / Papyrus Index for KeepOffsetFromActor): the
		// offsets are in the TARGET's body space (+X right, +Y forward, +Z up);
		// afCatchUpRadius is the radius beyond which the actor runs to catch up, and
		// afFollowRadius is the radius inside which it will not move.
		// ------------------------------------------------------------------
		namespace native
		{
			void SetDontMove(RE::Actor* a_actor, bool a_dontMove)
			{
				using func_t = void (*)(RE::Actor*, bool);
				static REL::Relocation<func_t> func{ REL::RelocationID(36490, 37489) };
				func(a_actor, a_dontMove);
			}

			void KeepOffsetFromActor(
				RE::Actor* a_actor,
				const RE::ActorHandle& a_target,
				const RE::NiPoint3& a_offset,
				const RE::NiPoint3& a_angle,
				float a_catchUpRadius,
				float a_followRadius)
			{
				using func_t = void (*)(RE::Actor*, const RE::ActorHandle&, const RE::NiPoint3&, const RE::NiPoint3&, float, float);
				static REL::Relocation<func_t> func{ REL::RelocationID(36870, 37894) };
				func(a_actor, a_target, a_offset, a_angle, a_catchUpRadius, a_followRadius);
			}

			void ClearKeepOffsetFromActor(RE::Actor* a_actor)
			{
				using func_t = void (*)(RE::Actor*);
				static REL::Relocation<func_t> func{ REL::RelocationID(36871, 37895) };
				func(a_actor);
			}
		}

		// ------------------------------------------------------------------
		// Tunables. All are documented behaviour constants, not user settings:
		//   * a primitive is re-issued if the desired goal moved by more than
		//     kOffsetEpsilon, and re-asserted every kReassertSeconds so a movement
		//     goal the engine rewrote on its own is taken back (the vanilla
		//     summoned-creature follow may rewrite it).
		//   * the catch-up recall itself - when it fires, its hysteresis, its cooldown and
		//     the landing rules - is decided by Core/CatchUp.h. This file gathers the
		//     landing facts (space, ground, clearance, occupants, water) and performs the
		//     one position write; it keeps no threshold of its own.
		//   * MoveTo cancels (guard in place) after kMoveToTimeoutSeconds.
		// ------------------------------------------------------------------
		constexpr float kOffsetEpsilon = 8.0f;
		constexpr float kOffsetEpsilonSq = kOffsetEpsilon * kOffsetEpsilon;
		constexpr float kFacingEpsilonDegrees = 10.0f;
		constexpr float kReassertSeconds = 2.0f;
		constexpr float kMoveToTimeoutSeconds = 45.0f;

		// Follow pace (PoE2 stage M1). kDefaultSpeedMult is the engine's normal movement
		// pace, i.e. the SpeedMult value a summon carries without help; the pace is applied
		// as the difference to it, as a temporary modifier that is rolled back. It is never
		// a base actor value and never re-scaled against the current total, so another
		// writer (a buff spell, the Command Rush bonus) keeps its own points instead of
		// being fought over. A modifier the engine drops on its own is re-issued on the
		// next pace change.
		constexpr float kDefaultSpeedMult = 100.0f;
		constexpr float kSpeedEpsilon = 5.0f;

		// A summon whose own position barely changes is the one that may be recalled as
		// "stuck": one that trails a galloping player is moving fast and must keep walking
		// instead of being warped every window (horizontal units per second below this
		// count as standing still).
		constexpr float kMovingSpeedThreshold = 25.0f;

		// Landing probe geometry of the catch-up recall. The rays follow APMF's
		// field-proven space query (APMF/native/core/SpaceQuery.cpp): a downward cast for
		// walkable ground, a ring at knee height for clearance and four samples for a ledge.
		constexpr float kProbeStartHeight = 64.0f;   // the down ray starts this far above the candidate
		constexpr float kProbeDropDepth = 600.0f;    // ...and searches this far below it
		constexpr float kKneeHeight = 32.0f;         // clearance rays run this far above the ground
		constexpr float kLedgeStep = 48.0f;          // a ring sample must find ground within this
		constexpr float kOccupantHeight = 256.0f;    // an actor above/below this cannot occupy the spot
		constexpr std::size_t kMaxLandingProbes = 3; // candidate spots probed per recall

		// How long an explicit FocusTarget order is given to put the summon into combat
		// before the driver decides the engagement never started.
		// Long enough for a summon to run ~2500 units to a target it has not detected yet
		// (StartCombat on an undetected foe is refused until the summon gets close).
		constexpr float kAttackGraceSeconds = 15.0f;
		constexpr float kAttackReengageSeconds = 1.0f;

		// The focused-target charge stops this far from the target (melee reach); inside
		// it the keep-offset goal is released so the combat AI swings.
		constexpr float kAttackApproachRange = 150.0f;

		// Even while the engine is "fighting" the focus, a summon farther than this is
		// still driven at it: a fight against an undetected or distant target is the
		// engine's SEARCH behaviour, not an approach (the 2026-11-10 session showed a wolf
		// "in combat" with its focus at 1300-1600 units that never closed in).
		constexpr float kAttackHandoffRange = 400.0f;

		// Radius the position of a "parked" summon is held inside (APMF's proven
		// full stand-still values: catchUp 20, follow 10).
		constexpr float kParkCatchUpRadius = 20.0f;
		constexpr float kParkFollowRadius = 10.0f;

		struct AppliedMovement
		{
			core::MovementMode mode{ core::MovementMode::None };
			core::SummonOrder lastOrder{ core::SummonOrder::Follow };
			core::Vec3 localOffset{};
			float facingOffsetDegrees{ 0.0f };
			bool keepOffsetActive{ false };
			RE::FormID keepOffsetAnchor{ 0 };
			bool translationLocked{ false };
			// SetDontMove is persistent on the actor and survives a save/load, so the
			// first tick after (re)discovery must push the desired value even when it
			// happens to equal the default `false`.
			bool translationKnown{ false };
			float modeEnteredAt{ 0.0f };
			float orderEnteredAt{ 0.0f };
			float lastIssuedAt{ 0.0f };
			float lastReengageAt{ -100.0f };
			// Catch-up recall: the pure rules' memory (progress clock, best lag, hysteresis
			// gate, cooldown) plus the refusal already reported, so one refusing condition
			// is logged and counted once, not once per tick.
			core::CatchUpMemory catchUp{};
			core::CatchUpRefusal lastCatchUpRefusal{ core::CatchUpRefusal::None };
			// Follow pace: the SpeedMult modifier this driver currently owns, so the exact
			// amount can be rolled back and another writer's value is never touched.
			core::FollowPace pace{ core::FollowPace::Walk };
			bool paceKnown{ false };
			float appliedSpeedDelta{ 0.0f };
			// Position sample used to tell "standing still / wedged" from "running after a
			// player that keeps the gap open".
			core::Vec3 lastSamplePosition{};
			float positionSampledAt{ 0.0f };
		};

		std::unordered_map<core::EntityID, AppliedMovement> s_applied;

		// Session statistics behind the status line (declared in SummonMovement.h).
		core::CatchUpStats s_catchUpStats{};

		core::FormationLayout FormationLayoutFrom(const core::SettingsData& a_settings)
		{
			core::FormationLayout layout{};
			layout.outerRingCapacity = a_settings.squad.formationRingCapacity;
			layout.innerRingScale = a_settings.squad.formationInnerRingScale;
			layout.minSlotDistance = a_settings.squad.formationMinSlotDistance;
			layout.maxExpansion = a_settings.squad.formationMaxExpansion;
			return layout;
		}

		// Stricter than ShareLoadedSpace: the recall is a teleport inside ONE loaded space,
		// so both cells must be attached (a cell that is detaching mid-frame has no usable
		// havok world), both references must have their 3D loaded, and an interior/exterior
		// boundary is never crossed. Crossing a cell or a worldspace is the engine's own
		// summoned-creature snap's job (fSummonedCreatureMaxFollowDist).
		bool SameLoadedSpaceForWarp(RE::Actor* a_owner, RE::Actor* a_summon)
		{
			if (!a_owner || !a_summon) {
				return false;
			}
			auto* ownerCell = a_owner->GetParentCell();
			auto* summonCell = a_summon->GetParentCell();
			if (!ownerCell || !summonCell) {
				return false;
			}
			if (!ownerCell->IsAttached() || !summonCell->IsAttached()) {
				return false;
			}
			if (ownerCell->IsInteriorCell() != summonCell->IsInteriorCell()) {
				return false;
			}
			return ShareLoadedSpace(a_summon, a_owner) && a_owner->Is3DLoaded() && a_summon->Is3DLoaded();
		}

		core::Vec3 ToCore(const RE::NiPoint3& a_point) noexcept
		{
			return { a_point.x, a_point.y, a_point.z };
		}

		float HorizontalDistanceSq(const core::Vec3& a_lhs, const core::Vec3& a_rhs) noexcept
		{
			const float dx = a_lhs.x - a_rhs.x;
			const float dy = a_lhs.y - a_rhs.y;
			return dx * dx + dy * dy;
		}

		void ApplyTranslationLock(RE::Actor* a_summon, core::EntityID a_id, bool a_locked, AppliedMovement& a_state)
		{
			if (a_state.translationKnown && a_state.translationLocked == a_locked) {
				return;
			}

			native::SetDontMove(a_summon, a_locked);
			a_state.translationLocked = a_locked;
			a_state.translationKnown = true;
			logger::info("[Movement] Summon 0x{:08X} SetDontMove({})", a_id, a_locked);
		}

		void ClearKeepOffset(RE::Actor* a_summon, core::EntityID a_id, AppliedMovement& a_state)
		{
			if (!a_state.keepOffsetActive) {
				return;
			}

			native::ClearKeepOffsetFromActor(a_summon);
			a_state.keepOffsetActive = false;
			logger::info("[Movement] Summon 0x{:08X} keep-offset cleared ({})", a_id, core::ToString(a_state.mode));
		}

		void ApplyKeepOffset(
			RE::Actor* a_summon,
			RE::Actor* a_anchor,
			const char* a_anchorLabel,
			core::EntityID a_id,
			const core::MovementIntent& a_intent,
			AppliedMovement& a_state,
			float a_now)
		{
			const bool sameGoal = a_state.keepOffsetActive &&
			                      a_state.keepOffsetAnchor == a_anchor->GetFormID() &&
			                      a_state.localOffset.DistanceSquaredTo(a_intent.localOffset) <= kOffsetEpsilonSq &&
			                      std::abs(a_state.facingOffsetDegrees - a_intent.facingOffsetDegrees) <= kFacingEpsilonDegrees;
			const bool reassert = a_state.keepOffsetActive && (a_now - a_state.lastIssuedAt) >= kReassertSeconds;

			if (sameGoal && !reassert) {
				return;
			}

			const RE::NiPoint3 offset{ a_intent.localOffset.x, a_intent.localOffset.y, a_intent.localOffset.z };
			const RE::NiPoint3 angles{ 0.0f, 0.0f, a_intent.facingOffsetDegrees };
			native::KeepOffsetFromActor(a_summon, a_anchor->GetHandle(), offset, angles, a_intent.catchUpRadius, a_intent.followRadius);

			a_state.keepOffsetActive = true;
			a_state.keepOffsetAnchor = a_anchor->GetFormID();
			a_state.localOffset = a_intent.localOffset;
			a_state.facingOffsetDegrees = a_intent.facingOffsetDegrees;
			a_state.lastIssuedAt = a_now;

			if (!sameGoal) {
				logger::info("[Movement] Summon 0x{:08X} {} keep-offset ({:.0f}, {:.0f}, {:.0f}) facing={:.0f} follow={:.0f} catchUp={:.0f} target={}",
					a_id,
					core::ToString(a_intent.mode),
					a_intent.localOffset.x,
					a_intent.localOffset.y,
					a_intent.localOffset.z,
					a_intent.facingOffsetDegrees,
					a_intent.followRadius,
					a_intent.catchUpRadius,
					a_anchorLabel);
			} else {
				logger::debug("[Movement] Summon 0x{:08X} keep-offset re-asserted ({})", a_id, core::ToString(a_intent.mode));
			}
		}

		void NudgeSpeed(RE::Actor* a_actor)
		{
			// The engine recomputes locomotion when carry weight changes; a net-zero nudge
			// makes a SpeedMult change take effect without waiting for the next equip (the
			// same trick Engine/CommandActions.cpp uses for a Rush).
			auto* values = a_actor->AsActorValueOwner();
			values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.01f);
			values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.01f);
		}

		// Applies the follow pace as a temporary SpeedMult modifier that this driver owns
		// (`appliedSpeedDelta`), so it can be handed back exactly.
		void ApplyFollowSpeed(
			RE::Actor* a_summon,
			core::EntityID a_id,
			const core::FollowMotion& a_motion,
			float a_lag,
			AppliedMovement& a_state)
		{
			const float desiredDelta = a_motion.speed - kDefaultSpeedMult;
			const float change = desiredDelta - a_state.appliedSpeedDelta;
			if (std::abs(change) <= kSpeedEpsilon) {
				return;
			}

			auto* values = a_summon->AsActorValueOwner();
			if (!values) {
				return;
			}

			values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, change);
			NudgeSpeed(a_summon);

			const bool paceChanged = !a_state.paceKnown || a_state.pace != a_motion.pace;
			a_state.pace = a_motion.pace;
			a_state.paceKnown = true;
			a_state.appliedSpeedDelta = desiredDelta;

			if (paceChanged) {
				logger::info("[Movement] Summon 0x{:08X} follow pace {} (lag {:.0f}, SpeedMult {:+.0f})",
					a_id, core::ToString(a_motion.pace), a_lag, change);
			} else {
				logger::debug("[Movement] Summon 0x{:08X} follow pace {} re-issued (SpeedMult {:+.0f})",
					a_id, core::ToString(a_motion.pace), change);
			}
		}

		// Gives the actor back exactly the SpeedMult modifier this driver added.
		void RestoreFollowSpeed(RE::Actor* a_summon, core::EntityID a_id, AppliedMovement& a_state)
		{
			const float owned = a_state.appliedSpeedDelta;
			const bool modified = a_state.paceKnown && std::abs(owned) > kSpeedEpsilon;
			a_state.paceKnown = false;
			a_state.pace = core::FollowPace::Walk;
			a_state.appliedSpeedDelta = 0.0f;

			if (!modified) {
				return;
			}

			if (auto* values = a_summon->AsActorValueOwner()) {
				values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -owned);
				NudgeSpeed(a_summon);
			}
			logger::info("[Movement] Summon 0x{:08X} follow pace released (SpeedMult {:+.0f})", a_id, -owned);
		}

		// A summon that leaves the roster (dispel, death, displacement) is dropped from the
		// bookkeeping without an actor pointer, so its leftover SpeedMult modifier is
		// rolled back here through the form table while the actor is still alive.
		void ReleaseLeftoverSpeed(core::EntityID a_id, const AppliedMovement& a_state)
		{
			if (!a_state.paceKnown || std::abs(a_state.appliedSpeedDelta) <= kSpeedEpsilon) {
				return;
			}

			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
			if (!actor || actor->IsDeleted() || actor->IsDead()) {
				logger::debug("[Movement] Summon 0x{:08X} left the roster with SpeedMult {:+.0f}; actor gone, nothing to restore",
					a_id, a_state.appliedSpeedDelta);
				return;
			}

			if (auto* values = actor->AsActorValueOwner()) {
				values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kSpeedMult, -a_state.appliedSpeedDelta);
				NudgeSpeed(actor);
			}
			logger::info("[Movement] Summon 0x{:08X} left the roster: follow pace released (SpeedMult {:+.0f})",
				a_id, -a_state.appliedSpeedDelta);
		}

		// Drops the bookkeeping without writing actor values (a dead summon is about to
		// leave the roster and must not be touched).
		void ForgetFollowSpeed(AppliedMovement& a_state)
		{
			a_state.paceKnown = false;
			a_state.pace = core::FollowPace::Walk;
			a_state.appliedSpeedDelta = 0.0f;
		}

		// Samples how far the actor actually travelled since the previous tick. Core's
		// progress rule needs it because "standing still / wedged" and "running after a
		// player who keeps the gap open" must not be confused: one of them is a recall
		// candidate, the other is normal following.
		bool SampleFollowMovement(AppliedMovement& a_state, const core::Vec3& a_position, float a_now)
		{
			bool moving = false;
			const float sinceSample = a_now - a_state.positionSampledAt;
			if (sinceSample > 0.0f) {
				const float travelled = std::sqrt(HorizontalDistanceSq(a_position, a_state.lastSamplePosition));
				moving = (travelled / sinceSample) >= kMovingSpeedThreshold;
			}
			a_state.lastSamplePosition = a_position;
			a_state.positionSampledAt = a_now;
			return moving;
		}

		// ------------------------------------------------------------------
		// Landing facts of the catch-up recall.
		//
		// Everything below OBSERVES: it writes nothing to the world and lets Core decide.
		// The ray recipe is the one APMF's space query proved in the field
		// (APMF/native/core/SpaceQuery.cpp): bhkWorld::PickObject (vtable slot 0x33) cast
		// under the world's read lock, positions multiplied by bhkWorld::GetWorldScale(),
		// and the ray layer set to kCharController so static / terrain / ground / stair
		// geometry is hit like the engine's own char-controller picks do. The summon's own
		// capsule is skipped by repeating its system group, which is what keeps the ground
		// cast from landing on the actor being moved.
		// ------------------------------------------------------------------
		bool IsGroundLayer(RE::COL_LAYER a_layer) noexcept
		{
			return a_layer == RE::COL_LAYER::kStatic || a_layer == RE::COL_LAYER::kTerrain ||
			       a_layer == RE::COL_LAYER::kGround || a_layer == RE::COL_LAYER::kStairHelper;
		}

		// Ray filter: the char-controller layer plus the summon's collision system group,
		// so the pick ignores the actor's own capsule (same group) and still hits the world.
		std::uint32_t RayFilter(RE::Actor* a_summon)
		{
			std::uint32_t filter = static_cast<std::uint32_t>(RE::COL_LAYER::kCharController);
			if (a_summon) {
				if (auto* controller = a_summon->GetCharController()) {
					RE::CFilter info{};
					controller->GetCollisionFilterInfo(info);
					filter |= (info.filter >> 16) << 16;
				}
			}
			return filter;
		}

		struct RayProbe
		{
			bool hit{ false };
			bool skipped{ false }; // the engine's early-out: the ray was NOT cast, so it proves nothing
			float fraction{ 1.0f };
			RE::COL_LAYER layer{ RE::COL_LAYER::kUnidentified };
		};

		// One closest-hit ray. The caller holds the world's read lock.
		RayProbe CastRay(RE::bhkWorld* a_world, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, std::uint32_t a_filter)
		{
			const float scale = RE::bhkWorld::GetWorldScale();

			RE::bhkPickData pick;
			pick.rayInput.from = a_from * scale;
			pick.rayInput.to = a_to * scale;
			pick.rayInput.enableShapeCollectionFilter = false;
			pick.rayInput.filterInfo.filter = a_filter;
			a_world->PickObject(pick);

			RayProbe probe{};
			if (pick.pickFailed) {
				probe.skipped = true;
				return probe;
			}
			if (pick.rayOutput.HasHit()) {
				probe.hit = true;
				probe.fraction = pick.rayOutput.hitFraction;
				if (pick.rayOutput.rootCollidable) {
					probe.layer = pick.rayOutput.rootCollidable->GetCollisionLayer();
				}
			}
			return probe;
		}

		// Does the candidate point resolve in the owner's loaded space? An interior
		// candidate must be the owner's own cell; an exterior one must belong to the
		// owner's worldspace (a neighbouring loaded cell is fine - the summon routinely
		// trails the player across a border).
		bool IsSameSpaceCell(const RE::TESObjectCELL* a_candidateCell, const RE::Actor* a_owner)
		{
			const auto* ownerCell = a_owner ? a_owner->GetParentCell() : nullptr;
			if (!a_candidateCell || !ownerCell) {
				return false;
			}
			if (a_candidateCell->IsInteriorCell() != ownerCell->IsInteriorCell()) {
				return false;
			}
			if (a_candidateCell->IsInteriorCell()) {
				return a_candidateCell == ownerCell;
			}
			auto* worldspace = a_candidateCell->GetRuntimeData().worldSpace;
			return worldspace && worldspace == a_owner->GetWorldspace();
		}

		// Horizontal distance from the point to the nearest living actor other than the
		// summon being moved (the owner included: a recall must never land on top of it).
		// Clamped to the probe radius: anything at or beyond it is "nobody there".
		float NearestActorDistance(RE::Actor* a_ignore, const RE::NiPoint3& a_point, float a_radius)
		{
			float best = a_radius;
			const auto test = [&](RE::Actor* a_actor) {
				if (!a_actor || a_actor == a_ignore || a_actor->IsDead() || a_actor->IsDisabled()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto position = a_actor->GetPosition();
				if (std::abs(position.z - a_point.z) > kOccupantHeight) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const float dx = position.x - a_point.x;
				const float dy = position.y - a_point.y;
				const float distance = std::sqrt(dx * dx + dy * dy);
				if (distance < best) {
					best = distance;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			};

			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				lists->ForEachHighActor(test);
			}
			// The player is not guaranteed to be in the high list; a second test is harmless.
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				test(player);
			}
			return best;
		}

		// The downward cast and the clearance tests of one candidate. Returns true when
		// walkable ground was found (a_outGroundRise is then valid); a_outBlocked reports a
		// spot that has ground but is closed in by geometry or sits on a ledge.
		bool ProbeGround(
			RE::Actor* a_summon,
			RE::TESObjectCELL* a_cell,
			const RE::NiPoint3& a_point,
			float a_clearance,
			float& a_outGroundRise,
			bool& a_outBlocked)
		{
			auto* world = a_cell ? a_cell->GetbhkWorld() : nullptr;
			if (!world) {
				return false;
			}

			const std::uint32_t filter = RayFilter(a_summon);
			RE::BSReadLockGuard guard(world->worldLock);

			const RE::NiPoint3 from{ a_point.x, a_point.y, a_point.z + kProbeStartHeight };
			const RE::NiPoint3 to{ a_point.x, a_point.y, a_point.z - kProbeDropDepth };
			const RayProbe ground = CastRay(world, from, to, filter);
			if (ground.skipped || !ground.hit || !IsGroundLayer(ground.layer)) {
				return false;
			}

			const RE::NiPoint3 landed{ a_point.x, a_point.y, from.z + (to.z - from.z) * ground.fraction };
			a_outGroundRise = landed.z - a_point.z;

			// Clearance: four level rays at knee height. A wall inside the landing radius
			// would put the actor into geometry, so the spot is refused.
			const RE::NiPoint3 knee{ landed.x, landed.y, landed.z + kKneeHeight };
			constexpr float kHalfPi = 1.57079633f;
			for (int i = 0; i < 4; ++i) {
				const float angle = static_cast<float>(i) * kHalfPi;
				const RE::NiPoint3 reach{ knee.x + std::sin(angle) * a_clearance, knee.y + std::cos(angle) * a_clearance, knee.z };
				const RayProbe wall = CastRay(world, knee, reach, filter);
				// An unmeasured ray proves nothing, so it refuses the spot instead of
				// silently accepting it (the caller logs the refusal).
				if (wall.skipped || wall.hit) {
					a_outBlocked = true;
					return true;
				}
			}

			// Ledge: all four samples on the clearance ring must meet ground at (roughly)
			// the same height. A drop at any of them means the point is on an edge.
			for (int i = 0; i < 4; ++i) {
				const float angle = static_cast<float>(i) * kHalfPi;
				const float x = landed.x + std::sin(angle) * a_clearance;
				const float y = landed.y + std::cos(angle) * a_clearance;
				const RE::NiPoint3 sampleFrom{ x, y, landed.z + kLedgeStep };
				const RE::NiPoint3 sampleTo{ x, y, landed.z - kLedgeStep };
				const RayProbe ring = CastRay(world, sampleFrom, sampleTo, filter);
				if (ring.skipped || !ring.hit || !IsGroundLayer(ring.layer)) {
					a_outBlocked = true;
					return true;
				}
			}
			return true;
		}

		// Collects the facts Core needs to accept or reject one landing candidate.
		core::LandingObservation ProbeLanding(
			RE::Actor* a_summon,
			RE::Actor* a_owner,
			const core::LandingCandidate& a_candidate,
			const core::CatchUpTuning& a_tuning)
		{
			core::LandingObservation observation{};

			const RE::NiPoint3 point{ a_candidate.position.x, a_candidate.position.y, a_candidate.position.z };
			auto* tes = RE::TES::GetSingleton();
			auto* cell = tes ? tes->GetCell(point) : nullptr;
			observation.hasSpace = IsSameSpaceCell(cell, a_owner);
			if (!observation.hasSpace) {
				return observation;
			}

			observation.groundFound = ProbeGround(a_summon, cell, point, a_tuning.landingClearance, observation.groundRise, observation.blocked);

			// Water: the ground must sit above the local surface. GetWaterHeight returns
			// false where there is no water at all, which leaves waterRise at "dry".
			float waterHeight = 0.0f;
			if (cell->GetWaterHeight(point, waterHeight)) {
				observation.waterRise = (point.z + observation.groundRise) - waterHeight;
			}

			observation.nearestActor = NearestActorDistance(a_summon, point, a_tuning.landingClearance);
			return observation;
		}

		// One emergency relocation. The decision (trigger, gates, cooldown, hysteresis) was
		// made by Core; this probes at most kMaxLandingProbes candidates, lets Core pick the
		// first safe one, and writes the actor there ONCE. Never a continuous movement:
		// the follow race resumes from that point on the next tick.
		void TryCatchUpWarp(
			RE::Actor* a_summon,
			RE::Actor* a_owner,
			core::EntityID a_id,
			const core::SummonModel& a_model,
			const core::CatchUpDecision& a_decision,
			const core::CatchUpObservation& a_observation,
			const core::CatchUpTuning& a_tuning,
			const core::SettingsData& a_settings,
			std::size_t a_slotIndex,
			std::size_t a_squadSize,
			AppliedMovement& a_state,
			float a_now)
		{
			// The cooldown anchors on the attempt, not the outcome: a spot with no safe
			// landing must not be re-probed on every tick.
			core::MarkCatchUpAttempt(a_state.catchUp, a_now);
			// The measurements called for a recall: counted once per attempt, whatever the
			// landing probe then decides.
			core::RecordCatchUpTrigger(s_catchUpStats, a_decision.trigger);

			const core::Vec3 ownerPoint{ a_owner->GetPosition().x, a_owner->GetPosition().y, a_owner->GetPosition().z };

			std::array<core::LandingCandidate, core::kMaxLandingCandidates> candidates{};
			std::array<core::LandingObservation, core::kMaxLandingCandidates> observations{};
			const std::size_t built = core::BuildLandingCandidates(
				ownerPoint,
				a_owner->GetAngleZ(),
				a_slotIndex,
				a_squadSize,
				FormationLayoutFrom(a_settings),
				a_tuning,
				candidates.data(),
				candidates.size());

			const std::size_t probed = std::min(built, kMaxLandingProbes);
			for (std::size_t i = 0; i < probed; ++i) {
				observations[i] = ProbeLanding(a_summon, a_owner, candidates[i], a_tuning);
			}

			const core::LandingSelection selection = core::SelectLandingSpot(candidates.data(), observations.data(), probed, a_tuning);
			if (!selection.found) {
				a_state.lastCatchUpRefusal = core::CatchUpRefusal::NoSafeSpot;
				core::RecordCatchUpRefusal(s_catchUpStats, core::CatchUpRefusal::NoSafeSpot);
				logger::warn("[Movement] Summon 0x{:08X} catch-up warp refused: NoSafeSpot ({} order, {} candidates probed, lag {:.0f}, owner {:.0f})",
					a_id, core::ToString(a_model.order), probed, a_observation.lagDistance, a_observation.distanceToOwner);
				return;
			}

			// The one write. MoveToNearestNavmesh then resolves the exact navmesh height
			// under the chosen point, so the actor lands on walkable ground rather than on
			// the bare coordinate; the ground probe above already checked that there is one.
			a_summon->SetPosition(RE::NiPoint3{ selection.position.x, selection.position.y, selection.position.z }, true);
			if (!a_summon->MoveToNearestNavmesh(0.0f)) {
				// Not hidden behind the success log: the actor is at the probed point, but
				// the engine found no navmesh to snap it to.
				logger::warn("[Movement] Summon 0x{:08X} catch-up warp landed at ({:.0f}, {:.0f}, {:.0f}) without a navmesh snap",
					a_id, selection.position.x, selection.position.y, selection.position.z);
			}

			// The recall moved the actor: re-sample so the next tick's movement check does
			// not read the teleport as travel.
			a_state.lastSamplePosition = ToCore(a_summon->GetPosition());
			a_state.positionSampledAt = a_now;

			core::MarkCatchUpWarped(a_state.catchUp, a_now);
			core::RecordCatchUpWarp(s_catchUpStats, a_decision.trigger, a_now);
			a_state.lastCatchUpRefusal = core::CatchUpRefusal::None;

			logger::info("[Movement] Summon 0x{:08X} catch-up warp applied ({}, {} order, spot {}/{}, lag was {:.0f}, owner {:.0f}, threshold {:.0f}, ground rise {:.0f}, nearest actor {:.0f})",
				a_id,
				core::ToString(a_decision.trigger),
				core::ToString(a_model.order),
				selection.index,
				probed,
				a_observation.lagDistance,
				a_observation.distanceToOwner,
				a_tuning.catchUpDistance,
				observations[selection.index].groundRise,
				observations[selection.index].nearestActor);
		}

		// Reports a refusal once per event: one line and one counter per changing reason,
		// not one per tick while the condition holds.
		void NoteCatchUpRefusal(
			core::EntityID a_id,
			const core::SummonModel& a_model,
			const core::CatchUpDecision& a_decision,
			const core::CatchUpObservation& a_observation,
			AppliedMovement& a_state)
		{
			const core::CatchUpRefusal refusal = a_decision.refusal;
			if (refusal == core::CatchUpRefusal::None) {
				a_state.lastCatchUpRefusal = core::CatchUpRefusal::None;
				return;
			}
			if (a_state.lastCatchUpRefusal == refusal) {
				return;
			}
			a_state.lastCatchUpRefusal = refusal;
			core::RecordCatchUpTrigger(s_catchUpStats, a_decision.trigger);
			core::RecordCatchUpRefusal(s_catchUpStats, refusal);
			logger::info("[Movement] Summon 0x{:08X} catch-up warp refused: {} ({}, {} order, lag {:.0f}, owner {:.0f})",
				a_id, core::ToString(refusal), core::ToString(a_decision.trigger), core::ToString(a_model.order), a_observation.lagDistance, a_observation.distanceToOwner);
		}
	}

	void UpdateForSummon(
		RE::PlayerCharacter* a_owner,
		RE::Actor* a_summon,
		core::SummonModel& a_model,
		core::StateMachine& a_fsm,
		const core::SettingsData& a_settings,
		std::size_t a_slotIndex,
		std::size_t a_squadSize,
		float a_now,
		bool a_combatPermitted)
	{
		if (!a_owner || !a_summon) {
			return;
		}

		const auto id = static_cast<core::EntityID>(a_summon->GetFormID());
		auto& state = s_applied[id];

		if (a_summon->IsDead()) {
			ApplyTranslationLock(a_summon, id, false, state);
			ClearKeepOffset(a_summon, id, state);
			ForgetFollowSpeed(state);
			return;
		}

		// A summon outside the owner's loaded space (another interior, an unloaded
		// exterior cell) has a suspended process; issuing movement primitives against it
		// would address an unloaded reference. A neighbouring LOADED exterior cell is
		// fine: the summon routinely trails the player across a cell border.
		if (!ShareLoadedSpace(a_summon, a_owner)) {
			return;
		}

		const core::Vec3 summonPosition = ToCore(a_summon->GetPosition());
		const core::Vec3 ownerPosition = ToCore(a_owner->GetPosition());
		const float distanceToOwner = std::sqrt(HorizontalDistanceSq(summonPosition, ownerPosition));

		// Order changes start the clock for the time-based rules below.
		if (a_model.order != state.lastOrder) {
			state.lastOrder = a_model.order;
			state.orderEnteredAt = a_now;
		}

		// 1. Order transitions that need engine distances.
		if (a_model.order == core::SummonOrder::Attack) {
			// An explicit FocusTarget that no longer exists (dead, unloaded, or never put
			// the summon into combat) must not pin the summon to "attack" forever: hand
			// it back to the follow order.
			const bool targetGone = a_model.currentTargetId == core::InvalidEntityID;
			const bool neverEngaged = (a_now - state.orderEnteredAt) >= kAttackGraceSeconds && !a_summon->IsInCombat();
			if (targetGone || neverEngaged) {
				a_model.order = core::SummonOrder::Follow;
				a_fsm.TransitionTo(a_model, core::SummonState::FollowOwner, "Attack order finished");
				logger::info("[Movement] Summon 0x{:08X} attack order finished ({}) -> Follow",
					id, targetGone ? "target gone" : "combat never started");
			}
		} else if (a_model.order == core::SummonOrder::Return) {
			// The walking itself is the ReturnToOwner intent below; its recall and its
			// pace are decided together with Follow (step 2c).
			if (distanceToOwner <= a_settings.movement.followMinDistance) {
				a_model.order = core::SummonOrder::Follow;
				a_fsm.TransitionTo(a_model, core::SummonState::FollowOwner, "Returned to owner range");
				logger::info("[Movement] Summon 0x{:08X} reached owner ({:.0f}) -> Follow", id, distanceToOwner);
			}
		} else if (a_model.order == core::SummonOrder::MoveTo && a_model.hasMoveTarget) {
			const float toDestination = std::sqrt(HorizontalDistanceSq(a_model.moveTargetPosition, ownerPosition));

			if (core::IsWithinArrivalRadius(summonPosition, a_model.moveTargetPosition)) {
				a_model.order = core::SummonOrder::WaitGuard;
				a_model.hasMoveTarget = false;
				a_fsm.TransitionTo(a_model, core::SummonState::Guard, "Arrived at move point");
				logger::info("[Movement] Summon 0x{:08X} arrived at MoveTo point ({:.0f}, {:.0f}, {:.0f}) -> Guard",
					id, a_model.moveTargetPosition.x, a_model.moveTargetPosition.y, a_model.moveTargetPosition.z);
			} else if (toDestination > core::kMaxMoveToDistance) {
				// The destination was left behind by a cell change or a teleport; walking to
				// it is meaningless (and used to generate absurd keep-offset goals).
				a_model.order = core::SummonOrder::Follow;
				a_model.hasMoveTarget = false;
				a_fsm.TransitionTo(a_model, core::SummonState::FollowOwner, "MoveTo destination abandoned");
				logger::warn("[Movement] Summon 0x{:08X} MoveTo destination is {:.0f} units from the owner (limit {:.0f}); abandoned -> Follow",
					id, toDestination, core::kMaxMoveToDistance);
			} else if (state.mode == core::MovementMode::MoveToPoint && (a_now - state.modeEnteredAt) >= kMoveToTimeoutSeconds) {
				a_model.order = core::SummonOrder::WaitGuard;
				a_model.hasMoveTarget = false;
				a_fsm.TransitionTo(a_model, core::SummonState::Guard, "MoveTo timed out");
				logger::warn("[Movement] Summon 0x{:08X} MoveTo point not reached in {:.0f}s -> Guard in place",
					id, kMoveToTimeoutSeconds);
			}
		}

		// 2. Translate the order into a movement intent.
		core::MovementIntent intent = core::BuildMovementIntent(
			a_model,
			a_summon->IsInCombat(),
			a_combatPermitted,
			a_settings,
			ownerPosition,
			summonPosition,
			a_owner->GetAngleZ(),
			a_slotIndex,
			a_squadSize);

		RE::Actor* approachAnchor = nullptr;

		// 2b. Explicit attack. While the engine's combat AI is fighting the focused actor
		// (StartCombat + the CombatGate target pin), it owns locomotion, approach range
		// and attack choice: a ranged or caster summon keeps its distance, a melee one
		// closes in. Our keep-offset charge is only an assist for the gap before that,
		// i.e. while the engine is not (yet) fighting the focus — otherwise two movement
		// goals compete, which is the "two behaviour patterns at once" the field reported.
		if (a_model.order == core::SummonOrder::Attack && a_combatPermitted &&
			a_model.currentTargetId != core::InvalidEntityID) {
			auto* attackTarget = RE::TESForm::LookupByID<RE::Actor>(a_model.currentTargetId);
			if (attackTarget && !attackTarget->IsDead() && ShareLoadedSpace(attackTarget, a_summon)) {
				const auto targetPosition = ToCore(attackTarget->GetPosition());
				const bool inCombat = a_summon->IsInCombat();
				const auto engineTarget = a_summon->GetActorRuntimeData().currentCombatTarget.get();
				const bool fightingFocus = inCombat && engineTarget && engineTarget.get() == attackTarget;

				const float targetDistanceSq = HorizontalDistanceSq(summonPosition, targetPosition);
				const bool closeEnough = targetDistanceSq <= kAttackHandoffRange * kAttackHandoffRange;

				// Keep the engagement alive: if the engine dropped combat, re-start it
				// against the same target; if it is fighting someone else, StartCombat on
				// an actor already in combat adds the focus to its group's targets, which
				// is what the target pin needs (the seat permits exactly this target).
				if (!fightingFocus && (a_now - state.lastReengageAt) >= kAttackReengageSeconds) {
					state.lastReengageAt = a_now;
					const bool started = a_summon->StartCombat(attackTarget, nullptr);
					logger::info("[Movement] Summon 0x{:08X} {} StartCombat(0x{:08X}) -> {} (dist {:.0f}, engine target 0x{:08X})",
						id, inCombat ? "add-target" : "re-engage", attackTarget->GetFormID(), started ? "true" : "false",
						std::sqrt(HorizontalDistanceSq(summonPosition, targetPosition)),
						engineTarget ? engineTarget->GetFormID() : 0u);
				}

				// The charge is anchored on the TARGET itself (offset 0 in its body space),
				// not on the owner: an owner-relative goal moved with every step and turn of
				// the player, which dragged the summon around instead of at the enemy.
				if (!(fightingFocus && closeEnough) &&
					targetDistanceSq > kAttackApproachRange * kAttackApproachRange &&
					targetDistanceSq <= core::kMaxMoveToDistance * core::kMaxMoveToDistance) {
					intent.mode = core::MovementMode::AttackApproach;
					intent.localOffset = core::Vec3{ 0.0f, 0.0f, 0.0f };
					intent.followRadius = kAttackApproachRange;
					intent.catchUpRadius = kAttackApproachRange * 2.0f;
					intent.facingOffsetDegrees = core::FacingOffsetDegrees(summonPosition, targetPosition, attackTarget->GetAngleZ());
					intent.lockTranslation = false;
					approachAnchor = attackTarget;
				}
			}
		}

		if (intent.mode != state.mode) {
			state.modeEnteredAt = a_now;
			// A new movement mode starts the recall measurement over; the warp cooldown
			// survives (Core::ResetCatchUpProgress), so an order switch cannot cut a
			// teleport short.
			core::ResetCatchUpProgress(state.catchUp, a_now);
		}

		// 2c. Follow race (PoE2 stage M1) and the emergency recall (stage 7). Only the two
		//     modes that bring the summon back to the owner take part; every other mode
		//     keeps the actor's own pace (a Command Rush bonus must survive) and is never
		//     teleported.
		if (intent.mode == core::MovementMode::FollowSlot || intent.mode == core::MovementMode::ReturnToOwner) {
			const core::FollowTuning paceTuning = core::FollowTuningFrom(a_settings);
			const core::CatchUpTuning catchUpTuning = core::CatchUpTuningFrom(a_settings);

			const core::Vec3 goalPoint = intent.mode == core::MovementMode::FollowSlot
				? core::OwnerLocalToWorldPoint(intent.localOffset, ownerPosition, a_owner->GetAngleZ())
				: ownerPosition;
			const float lag = std::sqrt(HorizontalDistanceSq(summonPosition, goalPoint));

			// The measurements are distance and real progress, never line of sight.
			core::CatchUpObservation observation{};
			observation.now = a_now;
			observation.lagDistance = lag;
			observation.distanceToOwner = distanceToOwner;
			observation.holdRadius = intent.followRadius;
			observation.isMoving = SampleFollowMovement(state, summonPosition, a_now);
			core::ObserveCatchUp(state.catchUp, observation, catchUpTuning);

			const core::FollowMotion motion = core::DecideFollowMotion(lag, paceTuning);
			ApplyFollowSpeed(a_summon, id, motion, lag, state);

			core::CatchUpContext context{};
			context.order = a_model.order;
			context.summonInCombat = a_summon->IsInCombat();
			context.combatPermitted = a_combatPermitted;
			context.ownerInCombat = a_owner->IsInCombat();
			context.sameSpace = SameLoadedSpaceForWarp(a_owner, a_summon);

			context.ownerSwimming = a_owner->IsInWater();
			if (auto* ownerState = a_owner->AsActorState()) {
				context.ownerAirborne = ownerState->IsFlying();
				context.ownerSwimming = context.ownerSwimming || ownerState->IsSwimming();
			}
			context.ownerMounted = a_owner->IsOnMount();

			const core::CatchUpDecision decision = core::DecideCatchUp(context, observation, state.catchUp, catchUpTuning);
			if (decision.warp) {
				TryCatchUpWarp(a_summon, a_owner, id, a_model, decision, observation, catchUpTuning, a_settings, a_slotIndex, a_squadSize, state, a_now);
			} else {
				NoteCatchUpRefusal(id, a_model, decision, observation, state);
			}
		} else {
			RestoreFollowSpeed(a_summon, id, state);
		}

		// 3. Apply it.
		switch (intent.mode) {
		case core::MovementMode::None:
			ApplyTranslationLock(a_summon, id, false, state);
			ClearKeepOffset(a_summon, id, state);
			break;

		case core::MovementMode::HoldPosition:
			{
				// Park the movement goal on the summon itself so the planner produces no
				// locomotion, and lock translation: a clean stand-still (APMF's proven
				// full movement block), while attacks still resolve in place.
				core::MovementIntent park = intent;
				park.followRadius = kParkFollowRadius;
				park.catchUpRadius = kParkCatchUpRadius;

				ApplyTranslationLock(a_summon, id, true, state);
				ApplyKeepOffset(a_summon, a_summon, "self", id, park, state, a_now);
			}
			break;

		default:
			ApplyTranslationLock(a_summon, id, false, state);
			if (approachAnchor) {
				ApplyKeepOffset(a_summon, approachAnchor, "focus", id, intent, state, a_now);
			} else {
				ApplyKeepOffset(a_summon, a_owner, "owner", id, intent, state, a_now);
			}
			break;
		}

		state.mode = intent.mode;

		// 4. The vanilla FollowTo package pulls a summon back to within Min Radius of the
		// player; orders that keep it away from the player release that leash.
		leash::UpdateForSummon(a_summon, core::ReleasesFollowLeash(a_model));
	}

	void RetainOnly(const std::unordered_set<core::EntityID>& a_liveSummons)
	{
		std::erase_if(s_applied, [&a_liveSummons](const auto& a_entry) {
			if (a_liveSummons.find(a_entry.first) != a_liveSummons.end()) {
				return false;
			}
			ReleaseLeftoverSpeed(a_entry.first, a_entry.second);
			return true;
		});
		leash::RetainOnly(a_liveSummons);
	}

	void ResetAll()
	{
		// Hand every SpeedMult boost back before the bookkeeping goes away: a load or a new
		// game drops the records, not the actors, and a summon that is still alive would
		// otherwise keep running at its last follow pace for the rest of its life.
		//
		// Known limitation (the same one the Rush in Engine/CommandActions.cpp carries): the
		// actor's modifier is saved with the actor, but this module's record of it is not,
		// so a summon that was running at a boosted pace when the player saved comes back
		// carrying a modifier this module no longer has a record of. Its amount is unknown, so it cannot be taken
		// back; the summon keeps the leftover points for the rest of its life (our own pace
		// writes are relative and never subtract them). The same applies when the engine
		// drops the modifier behind our back.
		for (const auto& [id, state] : s_applied) {
			ReleaseLeftoverSpeed(id, state);
		}

		s_applied.clear();
		leash::ResetAll();
		// The recall counters describe this session, not the previous one.
		core::ResetCatchUpStats(s_catchUpStats);
		logger::info("[Movement] Movement state reset.");
	}

	const core::CatchUpStats& CatchUpStatistics() noexcept
	{
		return s_catchUpStats;
	}

	std::string DescribeCatchUpStatistics()
	{
		return core::FormatCatchUpStats(s_catchUpStats);
	}
}
