#include "check.h"
#include "Core/Persistence.h"

#include <cstdint>
#include <limits>
#include <vector>

using namespace scf::core;

namespace
{
	PersistenceSettings Settings(float a_delay = 7.0f, float a_window = 10.0f)
	{
		PersistenceSettings s;
		s.persistentSummons = true;
		s.autoRevive = true;
		s.reviveDelaySeconds = a_delay;
		s.recentDeathWindowSeconds = a_window;
		s.magickaReservePerSummon = 50.0f;
		s.minMagickaFraction = 0.1f;
		return s;
	}

	SummonIntent Intent(SummonOrder a_order, AggressionMode a_aggression, EntityID a_focus = InvalidEntityID)
	{
		SummonIntent intent;
		intent.order = a_order;
		intent.aggression = a_aggression;
		intent.focusTargetId = a_focus;
		return intent;
	}
}

TEST(TestTrackAliveIsIdempotentAndIgnoresInvalidActor)
{
	PersistenceState state;
	state.SetSettings(Settings());

	auto* first = state.TrackAlive(0x100, 0x200, 1.0f);
	CHECK(first != nullptr);
	CHECK(state.GetSlotCount() == 1);
	CHECK(first->actorId == 0x100);
	CHECK(first->spellFormId == 0x200);
	CHECK(first->state == PersistentSlotState::Alive);

	auto* second = state.TrackAlive(0x100, 0x200, 2.0f);
	CHECK(second == first);
	CHECK(state.GetSlotCount() == 1);

	CHECK(state.TrackAlive(InvalidEntityID, 0x200, 3.0f) == nullptr);
	CHECK(state.GetSlotCount() == 1);
}

TEST(TestMaxTrackedSlotsRefusesExtraSummons)
{
	PersistenceSettings settings = Settings();
	settings.maxTrackedSlots = 1;
	PersistenceState state;
	state.SetSettings(settings);

	CHECK(state.TrackAlive(0x100, 0x200, 0.0f) != nullptr);
	CHECK(state.TrackAlive(0x101, 0x200, 0.0f) == nullptr);
	CHECK(state.GetSlotCount() == 1);
}

TEST(TestReviveTimerFiresAfterDelayAndDeathIsIdempotent)
{
	PersistenceState state;
	state.SetSettings(Settings(7.0f, 0.0f));

	state.TrackAlive(0x100, 0x200, 0.0f);
	CHECK(state.MarkDead(0x100, 100.0f));
	CHECK(!state.MarkDead(0x100, 101.0f));  // the adapter reports the corpse every tick
	CHECK(state.FindByActor(0x100)->state == PersistentSlotState::Dead);

	// 6.99 s after death: not yet.
	CHECK(state.CollectDueRevives(106.99f).empty());
	// Exactly 7 s: due.
	const auto due = state.CollectDueRevives(107.0f);
	CHECK(due.size() == 1);
	CHECK(due[0] == state.FindByActor(0x100)->slotId);
}

TEST(TestRevivePreservesOrderAggressionAndFocus)
{
	PersistenceState state;
	state.SetSettings(Settings(7.0f, 0.0f));

	state.TrackAlive(0x100, 0x200, 0.0f);
	SummonModel model;
	model.order = SummonOrder::WaitGuard;
	model.aggression = AggressionMode::Aggressive;
	model.currentTargetId = 0x55;
	state.UpdateIntentFromModel(0x100, model);

	CHECK(state.MarkDead(0x100, 100.0f));
	const auto due = state.CollectDueRevives(107.0f);
	CHECK(due.size() == 1);

	state.MarkReviveCast(due[0], 107.0f);
	CHECK(state.FindSlot(due[0])->state == PersistentSlotState::Reviving);

	// The revive cast produced a new actor with a new FormID.
	CHECK(state.BindReviving(0x200, 0x777, 108.0f));
	CHECK(state.GetSlotCount() == 1);
	const auto* slot = state.FindByActor(0x777);
	CHECK(slot != nullptr);
	CHECK(slot->state == PersistentSlotState::Alive);
	CHECK(slot->intentPending);

	const auto intent = state.TakePendingIntent(0x777);
	CHECK(intent.has_value());
	CHECK(intent->order == SummonOrder::WaitGuard);
	CHECK(intent->aggression == AggressionMode::Aggressive);
	CHECK(intent->focusTargetId == 0x55);

	// Applied once only: the model is captured again from now on.
	CHECK(!state.TakePendingIntent(0x777).has_value());
	SummonModel updated;
	updated.order = SummonOrder::Follow;
	updated.aggression = AggressionMode::Passive;
	state.UpdateIntentFromModel(0x777, updated);
	CHECK(state.FindByActor(0x777)->intent.order == SummonOrder::Follow);
	CHECK(state.FindByActor(0x777)->intent.aggression == AggressionMode::Passive);
}

TEST(TestRecentDeathRulePausesAndRestartsTheTimer)
{
	PersistenceState state;
	state.SetSettings(Settings(7.0f, 10.0f));

	state.TrackAlive(0x100, 0x200, 0.0f);
	state.TrackAlive(0x101, 0x201, 0.0f);

	CHECK(state.MarkDead(0x100, 100.0f));  // would revive at 107
	CHECK(state.MarkDead(0x101, 103.0f));  // another minion died: A's timer moves to 113

	// A is paused by B's death; B's own death does not pause B.
	CHECK(state.CollectDueRevives(107.0f).empty());
	CHECK(state.IsRevivePaused(state.FindByActor(0x100)->slotId, 107.0f));
	CHECK(!state.IsRevivePaused(state.FindByActor(0x101)->slotId, 110.0f));

	// B revives first (its own timer), A waits for B's death + window.
	const auto at110 = state.CollectDueRevives(110.0f);
	CHECK(at110.size() == 1);
	const auto* b = state.FindByActor(0x101);
	CHECK(b != nullptr && at110[0] == b->slotId);

	state.MarkReviveCast(at110[0], 110.0f);
	CHECK(state.BindReviving(0x201, 0x888, 111.0f));

	// The revived B is a new summon id: its old death no longer gates A.
	const auto at113 = state.CollectDueRevives(113.0f);
	CHECK(at113.size() == 1);
	CHECK(at113[0] == state.FindByActor(0x100)->slotId);
}

TEST(TestRecentDeathRuleCanBeDisabled)
{
	PersistenceState state;
	state.SetSettings(Settings(7.0f, 0.0f));  // window 0 = rule off

	state.TrackAlive(0x100, 0x200, 0.0f);
	state.TrackAlive(0x101, 0x201, 0.0f);
	CHECK(state.MarkDead(0x100, 100.0f));
	CHECK(state.MarkDead(0x101, 103.0f));

	// A still revives at its own 107 despite B dying at 103; B is due at 110.
	const auto due = state.CollectDueRevives(107.0f);
	CHECK(due.size() == 1);
	CHECK(due[0] == state.FindByActor(0x100)->slotId);
}

TEST(TestUntrackedDeathFeedsTheRule)
{
	PersistenceState state;
	state.SetSettings(Settings(7.0f, 10.0f));

	CHECK(state.RecordDeath(0x999, 100.0f));   // a non-persistent minion died
	CHECK(!state.RecordDeath(0x999, 101.0f));  // repeated report is ignored
	CHECK(state.LatestOtherDeath(0x100, 105.0f).has_value());
	CHECK_NEAR(*state.LatestOtherDeath(0x100, 105.0f), 100.0f, 0.001f);

	state.TrackAlive(0x100, 0x200, 0.0f);
	CHECK(state.MarkDead(0x100, 101.0f));  // own timer would revive at 108
	// 0x999 died at 100, so the slot is paused until 110.
	CHECK(state.CollectDueRevives(107.0f).empty());
	CHECK(state.CollectDueRevives(109.0f).empty());
	CHECK(state.CollectDueRevives(110.0f).size() == 1);
}

TEST(TestModeOffAndDispelCancelTheRevive)
{
	PersistenceState state;
	state.SetSettings(Settings(7.0f, 0.0f));

	// Mode off: Clear() drops both live and dead slots and the reserve.
	state.TrackAlive(0x100, 0x200, 0.0f);
	state.MarkDead(0x100, 100.0f);
	CHECK(state.ReservedSummonCount() == 1);
	state.Clear();
	CHECK(state.GetSlotCount() == 0);
	CHECK(state.ReservedSummonCount() == 0);
	CHECK(state.CollectDueRevives(200.0f).empty());

	// Dispel while alive: the slot is gone.
	state.TrackAlive(0x101, 0x201, 0.0f);
	state.RemoveByActor(0x101);
	CHECK(state.GetSlotCount() == 0);

	// A death report does not let RemoveByActor cancel a pending revive: only the model
	// leaving the roster after a death is a dead slot, and it must keep waiting.
	state.TrackAlive(0x102, 0x202, 0.0f);
	state.MarkDead(0x102, 100.0f);
	state.RemoveByActor(0x102);
	CHECK(state.GetSlotCount() == 1);
	CHECK(state.CollectDueRevives(107.0f).size() == 1);

	// A Dispel command on the dead slot (or a cap overflow) removes it explicitly.
	const auto slotId = state.GetSlots().front().slotId;
	state.Remove(slotId);
	CHECK(state.GetSlotCount() == 0);
}

TEST(TestReviveCastTimeoutRetriesThenGivesUp)
{
	PersistenceSettings settings = Settings(7.0f, 0.0f);
	settings.reviveCastTimeoutSeconds = 6.0f;
	settings.maxReviveAttempts = 2;
	PersistenceState state;
	state.SetSettings(settings);

	state.TrackAlive(0x100, 0x200, 0.0f);
	state.MarkDead(0x100, 0.0f);

	const auto first = state.CollectDueRevives(7.0f);
	CHECK(first.size() == 1);
	state.MarkReviveCast(first[0], 7.0f);

	// No actor appeared: the cast expires after 6 s and the slot is retried.
	const auto expired = state.ExpireReviveCasts(13.0f);
	CHECK(expired.size() == 1);
	CHECK(state.MarkReviveFailed(expired[0], 13.0f));
	CHECK(state.FindSlot(expired[0])->state == PersistentSlotState::Dead);
	CHECK(state.FindSlot(expired[0])->reviveAttempts == 1);

	const auto second = state.CollectDueRevives(20.0f);
	CHECK(second.size() == 1);
	state.MarkReviveCast(second[0], 20.0f);

	const auto expiredAgain = state.ExpireReviveCasts(26.0f);
	CHECK(expiredAgain.size() == 1);
	CHECK(!state.MarkReviveFailed(expiredAgain[0], 26.0f));  // second failure = give up
	CHECK(state.GetSlotCount() == 0);
	CHECK(state.ReservedSummonCount() == 0);
}

TEST(TestReserveCountsTrackedAndDeadSlots)
{
	PersistenceState state;
	state.SetSettings(Settings());

	CHECK_NEAR(state.DesiredReserve(200.0f), 0.0f, 0.001f);

	state.TrackAlive(0x100, 0x200, 0.0f);
	CHECK(state.ReservedSummonCount() == 1);
	CHECK_NEAR(state.DesiredReserve(200.0f), 50.0f, 0.001f);

	state.TrackAlive(0x101, 0x201, 0.0f);
	CHECK(state.ReservedSummonCount() == 2);
	CHECK_NEAR(state.DesiredReserve(200.0f), 100.0f, 0.001f);

	// The reserve is held while the summon waits for its revive.
	CHECK(state.MarkDead(0x100, 100.0f));
	CHECK(state.ReservedSummonCount() == 2);

	// ... and released once the slot is gone (revive given up or dispel).
	state.RemoveByActor(0x101);
	CHECK(state.ReservedSummonCount() == 1);
	state.Clear();
	CHECK(state.ReservedSummonCount() == 0);
	CHECK_NEAR(state.DesiredReserve(200.0f), 0.0f, 0.001f);
}

TEST(TestReserveNeverDropsMaxMagickaBelowTheFloor)
{
	PersistenceState state;
	PersistenceSettings settings = Settings();
	settings.magickaReservePerSummon = 100.0f;
	settings.minMagickaFraction = 0.25f;
	state.SetSettings(settings);

	for (std::uint32_t i = 0; i < 4; ++i) {
		state.TrackAlive(0x100 + i, 0x200, 0.0f);
	}

	// 400 requested, but no more than 75% of a 200 max Magicka.
	CHECK_NEAR(state.DesiredReserve(200.0f), 150.0f, 0.001f);

	state.SetAppliedReserve(100.0f);
	CHECK_NEAR(state.PendingReserveDelta(200.0f), 50.0f, 0.001f);

	// Zero per-summon reserve or a dead owner: nothing is reserved.
	PersistenceSettings noReserve = settings;
	noReserve.magickaReservePerSummon = 0.0f;
	state.SetSettings(noReserve);
	CHECK_NEAR(state.DesiredReserve(200.0f), 0.0f, 0.001f);
	state.SetSettings(settings);
	CHECK_NEAR(state.DesiredReserve(0.0f), 0.0f, 0.001f);
}

TEST(TestRestoredAliveSlotReservesOnlyAfterItIsBound)
{
	SavedState saved;
	SavedSlot slot;
	slot.spellFormId = 0x200;
	slot.state = PersistentSlotState::Alive;
	slot.intent = Intent(SummonOrder::Return, AggressionMode::Passive);
	saved.slots.push_back(slot);

	PersistenceState state;
	state.SetSettings(Settings());
	state.Restore(saved, 100.0f);

	CHECK(state.GetSlotCount() == 1);
	const auto* restored = &state.GetSlots().front();
	CHECK(restored->actorId == InvalidEntityID);
	CHECK(restored->state == PersistentSlotState::Alive);
	CHECK(restored->intentPending);
	// An unmatched restored slot most likely belongs to a summon that is gone from the
	// loaded save: it must not hold Magicka until an actor is matched.
	CHECK(state.ReservedSummonCount() == 0);

	CHECK(state.BindAliveUnbound(0x200, 0x777, 101.0f));
	CHECK(state.FindByActor(0x777) != nullptr);
	CHECK(state.ReservedSummonCount() == 1);
	const auto intent = state.TakePendingIntent(0x777);
	CHECK(intent.has_value());
	CHECK(intent->order == SummonOrder::Return);
	CHECK(intent->aggression == AggressionMode::Passive);
}

TEST(TestSettingsAreSanitizedAndBlacklistBlocksSpells)
{
	PersistenceSettings settings;
	settings.reviveDelaySeconds = std::numeric_limits<float>::quiet_NaN();
	settings.recentDeathWindowSeconds = -5.0f;
	settings.magickaReservePerSummon = 1.0e9f;
	settings.minMagickaFraction = 5.0f;
	settings.maxReviveAttempts = 100;
	settings.maxTrackedSlots = 0;
	settings.blacklistedSpellIds = { 0x123, 0, 0x456 };

	const auto clean = settings.Sanitized();
	CHECK_NEAR(clean.reviveDelaySeconds, 7.0f, 0.001f);
	CHECK_NEAR(clean.recentDeathWindowSeconds, 0.0f, 0.001f);
	CHECK_NEAR(clean.magickaReservePerSummon, 2000.0f, 0.001f);
	CHECK_NEAR(clean.minMagickaFraction, 0.9f, 0.001f);
	CHECK(clean.maxReviveAttempts == 10);
	CHECK(clean.maxTrackedSlots == 1);
	CHECK(clean.blacklistedSpellIds.size() == 2);  // the 0 entry is dropped

	CHECK(!clean.IsSpellAllowed(0));
	CHECK(!clean.IsSpellAllowed(0x123));
	CHECK(!clean.IsSpellAllowed(0x456));
	CHECK(clean.IsSpellAllowed(0x789));
}

TEST(TestSummonSpellScopeGuard)
{
	PersistenceSettings settings = Settings();
	CHECK(!settings.vanillaSpellsOnly);  // default: mod summon spells are managed too

	// Default scope: any kSummonCreature spell cast by the player, unless blacklisted.
	CHECK(ShouldTrackSummonSpell(true, true, false, false, settings));   // mod summon spell
	CHECK(ShouldTrackSummonSpell(true, true, true, false, settings));    // vanilla-origin spell
	CHECK(!ShouldTrackSummonSpell(false, true, true, false, settings));  // not a summon creature
	CHECK(!ShouldTrackSummonSpell(true, false, true, false, settings));  // cast by something else
	CHECK(!ShouldTrackSummonSpell(true, true, true, true, settings));    // blacklisted

	// bPersistVanillaSpellsOnly narrows the scope to the ORIGINAL source file of the
	// spell (a vanilla spell overridden by a patch still has the official origin).
	settings.vanillaSpellsOnly = true;
	CHECK(!ShouldTrackSummonSpell(true, true, false, false, settings));  // mod spell refused
	CHECK(ShouldTrackSummonSpell(true, true, true, false, settings));
	CHECK(!ShouldTrackSummonSpell(true, true, true, true, settings));    // blacklist still wins

	// The flag survives sanitizing and participates in settings equality: the adapter
	// calls SetSettings only when the snapshot actually changes.
	PersistenceSettings other = Settings();
	other.vanillaSpellsOnly = true;
	CHECK(!(Settings() == other));
	CHECK(Settings().Sanitized().vanillaSpellsOnly == false);
	CHECK(other.Sanitized().vanillaSpellsOnly);
}

TEST(TestPersistentEffectRefreshRule)
{
	const auto settings = Settings();
	CHECK(NeedsPersistentEffectRefresh(299.0f, settings));
	CHECK(!NeedsPersistentEffectRefresh(300.0f, settings));
	CHECK(!NeedsPersistentEffectRefresh(86000.0f, settings));
	CHECK(NeedsPersistentEffectRefresh(-1.0f, settings));
	CHECK(NeedsPersistentEffectRefresh(std::numeric_limits<float>::quiet_NaN(), settings));
}

TEST(TestCoSaveRoundTrip)
{
	PersistenceState state;
	state.SetSettings(Settings(7.0f, 0.0f));
	state.SetAppliedReserve(75.0f);

	state.TrackAlive(0x100, 0x200, 0.0f);
	SummonModel model;
	model.order = SummonOrder::MoveTo;
	model.aggression = AggressionMode::Aggressive;
	state.UpdateIntentFromModel(0x100, model);

	state.TrackAlive(0x101, 0x201, 0.0f);
	state.MarkDead(0x101, 997.0f);  // 4 s left at save time

	state.TrackAlive(0x102, 0x202, 0.0f);
	state.MarkDead(0x102, 990.0f);
	const auto due = state.CollectDueRevives(1000.0f);
	CHECK(due.size() == 1);
	state.MarkReviveCast(due[0], 1000.0f);  // pending cast at save time

	const auto saved = state.Save(1000.0f);
	CHECK(saved.slots.size() == 3);
	CHECK_NEAR(saved.reserveApplied, 75.0f, 0.001f);

	const auto bytes = EncodeState(saved);
	CHECK(bytes.size() == 12 + 3 * 13);
	CHECK(bytes[0] == 'S' && bytes[1] == 'C' && bytes[2] == 'F' && bytes[3] == 'P');

	const auto decoded = DecodeState(bytes);
	CHECK(decoded.has_value());
	CHECK(decoded->slots.size() == 3);
	CHECK_NEAR(decoded->reserveApplied, 75.0f, 0.001f);

	const auto& alive = decoded->slots[0];
	CHECK(alive.spellFormId == 0x200);
	CHECK(alive.state == PersistentSlotState::Alive);
	CHECK(alive.intent.order == SummonOrder::MoveTo);
	CHECK(alive.intent.aggression == AggressionMode::Aggressive);

	const auto& dead = decoded->slots[1];
	CHECK(dead.spellFormId == 0x201);
	CHECK(dead.state == PersistentSlotState::Dead);
	CHECK_NEAR(dead.remainingReviveSeconds, 4.0f, 0.001f);  // died at 997, reviveAt 1004

	const auto& castPending = decoded->slots[2];
	CHECK(castPending.spellFormId == 0x202);
	CHECK(castPending.state == PersistentSlotState::Dead);
	CHECK_NEAR(castPending.remainingReviveSeconds, 7.0f, 0.001f);  // retried after load

	// A load rebases the countdown on the new clock.
	PersistenceState restored;
	restored.SetSettings(Settings(7.0f, 0.0f));
	restored.Restore(*decoded, 5000.0f);
	CHECK(restored.GetSlotCount() == 3);
	CHECK_NEAR(restored.GetAppliedReserve(), 75.0f, 0.001f);
	CHECK(restored.GetSlots()[0].actorId == InvalidEntityID);  // matched by spell later
	CHECK(restored.GetSlots()[0].intentPending);

	CHECK(restored.CollectDueRevives(5003.9f).empty());
	CHECK(restored.CollectDueRevives(5004.0f).size() == 1);   // the slot with 4 s left
	CHECK(restored.CollectDueRevives(5007.0f).size() == 2);   // both dead slots are due

	const auto* restoredDead = restored.FindSlot(restored.GetSlots()[1].slotId);
	CHECK(restoredDead != nullptr);
	CHECK_NEAR(restoredDead->reviveAt, 5004.0f, 0.001f);
}

TEST(TestCoSaveDecoderRejectsBadPayloads)
{
	SavedState saved;
	SavedSlot slot;
	slot.spellFormId = 0x200;
	slot.state = PersistentSlotState::Dead;
	slot.remainingReviveSeconds = 3.0f;
	slot.intent = Intent(SummonOrder::WaitGuard, AggressionMode::Defensive);
	saved.slots.push_back(slot);

	const auto bytes = EncodeState(saved);
	CHECK(DecodeState(bytes).has_value());

	CHECK(!DecodeState(std::vector<std::uint8_t>{}).has_value());  // empty

	auto badMagic = bytes;
	badMagic[0] = 'X';
	CHECK(!DecodeState(badMagic).has_value());

	auto badVersion = bytes;
	badVersion[4] = 99;
	CHECK(!DecodeState(badVersion).has_value());

	auto truncated = bytes;
	truncated.pop_back();
	CHECK(!DecodeState(truncated).has_value());

	auto trailing = bytes;
	trailing.push_back(0);
	CHECK(!DecodeState(trailing).has_value());

	auto badState = bytes;
	badState[12 + 4] = 7;  // state byte of slot 0: only 0/1 are known
	CHECK(!DecodeState(badState).has_value());

	auto badOrder = bytes;
	badOrder[12 + 4 + 1 + 4 + 2] = 42;
	CHECK(!DecodeState(badOrder).has_value());

	// A slot count that is too large is rejected before allocating.
	auto badCount = bytes;
	badCount[10] = 0xFF;
	badCount[11] = 0xFF;
	CHECK(!DecodeState(badCount).has_value());
}
