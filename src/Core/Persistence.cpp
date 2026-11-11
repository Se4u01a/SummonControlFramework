#include "Core/Persistence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace scf::core
{
	namespace
	{
		constexpr std::array<std::uint8_t, 4> kMagic{ 'S', 'C', 'F', 'P' };
		constexpr std::uint16_t kCodecVersion{ 1 };
		constexpr std::size_t kHeaderBytes{ 4 + 2 + 4 + 2 };  // magic, version, reserve, slot count
		constexpr std::size_t kSlotBytes{ 4 + 1 + 4 + 2 + 1 + 1 };  // see EncodeState
		constexpr std::uint16_t kMaxDecodedSlots{ 128 };
		constexpr float kMaxReserve{ 1'000'000.0f };
		constexpr float kMaxRemainingSeconds{ 3600.0f };

		float ClampFinite(float a_value, float a_lo, float a_hi, float a_fallback) noexcept
		{
			if (!std::isfinite(a_value)) {
				return a_fallback;
			}
			return std::clamp(a_value, a_lo, a_hi);
		}

		void AppendU16(std::vector<std::uint8_t>& a_out, std::uint16_t a_value)
		{
			a_out.push_back(static_cast<std::uint8_t>(a_value & 0xFF));
			a_out.push_back(static_cast<std::uint8_t>((a_value >> 8) & 0xFF));
		}

		void AppendU32(std::vector<std::uint8_t>& a_out, std::uint32_t a_value)
		{
			a_out.push_back(static_cast<std::uint8_t>(a_value & 0xFF));
			a_out.push_back(static_cast<std::uint8_t>((a_value >> 8) & 0xFF));
			a_out.push_back(static_cast<std::uint8_t>((a_value >> 16) & 0xFF));
			a_out.push_back(static_cast<std::uint8_t>((a_value >> 24) & 0xFF));
		}

		void AppendF32(std::vector<std::uint8_t>& a_out, float a_value)
		{
			std::uint32_t bits = 0;
			static_assert(sizeof(bits) == sizeof(a_value));
			std::memcpy(&bits, &a_value, sizeof(bits));
			AppendU32(a_out, bits);
		}

		class Reader
		{
		public:
			Reader(const std::uint8_t* a_data, std::size_t a_size) :
				_data(a_data), _size(a_size) {}

			[[nodiscard]] bool U8(std::uint8_t& a_out)
			{
				if (_cursor + 1 > _size) {
					return false;
				}
				a_out = _data[_cursor++];
				return true;
			}

			[[nodiscard]] bool U16(std::uint16_t& a_out)
			{
				if (_cursor + 2 > _size) {
					return false;
				}
				a_out = static_cast<std::uint16_t>(_data[_cursor]) |
				        static_cast<std::uint16_t>(_data[_cursor + 1]) << 8;
				_cursor += 2;
				return true;
			}

			[[nodiscard]] bool U32(std::uint32_t& a_out)
			{
				if (_cursor + 4 > _size) {
					return false;
				}
				a_out = static_cast<std::uint32_t>(_data[_cursor]) |
				        static_cast<std::uint32_t>(_data[_cursor + 1]) << 8 |
				        static_cast<std::uint32_t>(_data[_cursor + 2]) << 16 |
				        static_cast<std::uint32_t>(_data[_cursor + 3]) << 24;
				_cursor += 4;
				return true;
			}

			[[nodiscard]] bool F32(float& a_out)
			{
				std::uint32_t bits = 0;
				if (!U32(bits)) {
					return false;
				}
				std::memcpy(&a_out, &bits, sizeof(a_out));
				return true;
			}

			[[nodiscard]] bool Magic(const std::array<std::uint8_t, 4>& a_expected)
			{
				if (_cursor + a_expected.size() > _size) {
					return false;
				}
				for (std::size_t i = 0; i < a_expected.size(); ++i) {
					if (_data[_cursor + i] != a_expected[i]) {
						return false;
					}
				}
				_cursor += a_expected.size();
				return true;
			}

			[[nodiscard]] std::size_t Consumed() const noexcept { return _cursor; }

		private:
			const std::uint8_t* _data{ nullptr };
			std::size_t _size{ 0 };
			std::size_t _cursor{ 0 };
		};

		bool ValidOrder(std::uint8_t a_value) noexcept
		{
			return a_value <= static_cast<std::uint8_t>(SummonOrder::MoveTo);
		}

		bool ValidAggression(std::uint8_t a_value) noexcept
		{
			return a_value <= static_cast<std::uint8_t>(AggressionMode::FocusTarget);
		}
	}

	PersistenceSettings PersistenceSettings::Sanitized() const noexcept
	{
		PersistenceSettings s = *this;
		s.reviveDelaySeconds = ClampFinite(s.reviveDelaySeconds, 1.0f, 120.0f, 7.0f);
		s.recentDeathWindowSeconds = ClampFinite(s.recentDeathWindowSeconds, 0.0f, 300.0f, 10.0f);
		s.magickaReservePerSummon = ClampFinite(s.magickaReservePerSummon, 0.0f, 2000.0f, 50.0f);
		s.minMagickaFraction = ClampFinite(s.minMagickaFraction, 0.0f, 0.9f, 0.1f);
		s.persistentEffectSeconds = ClampFinite(s.persistentEffectSeconds, 60.0f, 604800.0f, 86400.0f);
		s.effectExtendBelowSeconds = ClampFinite(s.effectExtendBelowSeconds, 5.0f, 3600.0f, 300.0f);
		s.reviveCastTimeoutSeconds = ClampFinite(s.reviveCastTimeoutSeconds, 1.0f, 30.0f, 6.0f);
		s.maxReviveAttempts = std::clamp<std::uint32_t>(s.maxReviveAttempts, 1, 10);
		s.maxTrackedSlots = std::clamp<std::size_t>(s.maxTrackedSlots, 1, 64);
		std::erase(s.blacklistedSpellIds, 0u);
		return s;
	}

	bool PersistenceSettings::IsSpellAllowed(std::uint32_t a_spellFormId) const noexcept
	{
		if (a_spellFormId == 0) {
			return false;
		}
		return std::find(blacklistedSpellIds.begin(), blacklistedSpellIds.end(), a_spellFormId) == blacklistedSpellIds.end();
	}

	const char* ToString(PersistentSlotState a_state) noexcept
	{
		switch (a_state) {
		case PersistentSlotState::Alive:    return "Alive";
		case PersistentSlotState::Dead:     return "Dead";
		case PersistentSlotState::Reviving: return "Reviving";
		default:                            return "Unknown";
		}
	}

	std::vector<std::uint8_t> EncodeState(const SavedState& a_state)
	{
		std::vector<std::uint8_t> out;
		const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(a_state.slots.size(), kMaxDecodedSlots));
		out.reserve(kHeaderBytes + static_cast<std::size_t>(count) * kSlotBytes);

		out.insert(out.end(), kMagic.begin(), kMagic.end());
		AppendU16(out, kCodecVersion);
		AppendF32(out, ClampFinite(a_state.reserveApplied, 0.0f, kMaxReserve, 0.0f));
		AppendU16(out, count);

		for (std::size_t i = 0; i < count; ++i) {
			const auto& slot = a_state.slots[i];
			AppendU32(out, slot.spellFormId);
			out.push_back(slot.state == PersistentSlotState::Alive ? 0 : 1);
			AppendF32(out, ClampFinite(slot.remainingReviveSeconds, 0.0f, kMaxRemainingSeconds, 0.0f));
			AppendU16(out, static_cast<std::uint16_t>(std::min<std::uint32_t>(slot.reviveAttempts, 0xFFFF)));
			out.push_back(static_cast<std::uint8_t>(slot.intent.order));
			out.push_back(static_cast<std::uint8_t>(slot.intent.aggression));
		}
		return out;
	}

	std::optional<SavedState> DecodeState(const std::uint8_t* a_data, std::size_t a_size)
	{
		if (!a_data || a_size < kHeaderBytes) {
			return std::nullopt;
		}

		Reader reader(a_data, a_size);
		if (!reader.Magic(kMagic)) {
			return std::nullopt;
		}

		std::uint16_t version = 0;
		if (!reader.U16(version) || version != kCodecVersion) {
			return std::nullopt;
		}

		SavedState state;
		if (!reader.F32(state.reserveApplied)) {
			return std::nullopt;
		}
		state.reserveApplied = ClampFinite(state.reserveApplied, 0.0f, kMaxReserve, 0.0f);

		std::uint16_t count = 0;
		if (!reader.U16(count) || count > kMaxDecodedSlots) {
			return std::nullopt;
		}
		if (a_size != kHeaderBytes + static_cast<std::size_t>(count) * kSlotBytes) {
			return std::nullopt;
		}

		state.slots.reserve(count);
		for (std::uint16_t i = 0; i < count; ++i) {
			SavedSlot slot;
			std::uint8_t stateByte = 0;
			std::uint16_t attempts = 0;
			std::uint8_t order = 0;
			std::uint8_t aggression = 0;

			if (!reader.U32(slot.spellFormId) || !reader.U8(stateByte) || !reader.F32(slot.remainingReviveSeconds) ||
				!reader.U16(attempts) || !reader.U8(order) || !reader.U8(aggression)) {
				return std::nullopt;
			}
			if (stateByte > 1 || !ValidOrder(order) || !ValidAggression(aggression)) {
				return std::nullopt;
			}

			slot.state = stateByte == 0 ? PersistentSlotState::Alive : PersistentSlotState::Dead;
			slot.remainingReviveSeconds = ClampFinite(slot.remainingReviveSeconds, 0.0f, kMaxRemainingSeconds, 0.0f);
			slot.reviveAttempts = std::min<std::uint32_t>(attempts, 100);
			slot.intent.order = static_cast<SummonOrder>(order);
			slot.intent.aggression = static_cast<AggressionMode>(aggression);
			state.slots.push_back(slot);
		}

		if (reader.Consumed() != a_size) {
			return std::nullopt;
		}
		return state;
	}

	std::optional<SavedState> DecodeState(const std::vector<std::uint8_t>& a_data)
	{
		return DecodeState(a_data.data(), a_data.size());
	}

	bool NeedsPersistentEffectRefresh(float a_remainingSeconds, const PersistenceSettings& a_settings) noexcept
	{
		if (!std::isfinite(a_remainingSeconds)) {
			return true;
		}
		return a_remainingSeconds < a_settings.effectExtendBelowSeconds;
	}

	bool ShouldTrackSummonSpell(
		bool a_isSummonCreature,
		bool a_castByPlayer,
		bool a_originIsOfficialMaster,
		bool a_blacklisted,
		const PersistenceSettings& a_settings) noexcept
	{
		if (!a_isSummonCreature || !a_castByPlayer || a_blacklisted) {
			return false;
		}
		return !a_settings.vanillaSpellsOnly || a_originIsOfficialMaster;
	}

	float ComputeMagickaReserve(std::size_t a_trackedSummons, float a_maxMagicka, const PersistenceSettings& a_settings) noexcept
	{
		if (a_trackedSummons == 0 || a_settings.magickaReservePerSummon <= 0.0f || !(a_maxMagicka > 0.0f)) {
			return 0.0f;
		}
		const float requested = a_settings.magickaReservePerSummon * static_cast<float>(a_trackedSummons);
		const float floor = std::max(0.0f, a_settings.minMagickaFraction * a_maxMagicka);
		const float limit = std::max(0.0f, a_maxMagicka - floor);
		return std::min(std::max(requested, 0.0f), limit);
	}

	void PersistenceState::SetSettings(const PersistenceSettings& a_settings)
	{
		_settings = a_settings.Sanitized();
	}

	PersistentSlot* PersistenceState::FindSlot(EntityID a_slotId) noexcept
	{
		if (a_slotId == InvalidEntityID) {
			return nullptr;
		}
		for (auto& slot : _slots) {
			if (slot.slotId == a_slotId) {
				return &slot;
			}
		}
		return nullptr;
	}

	const PersistentSlot* PersistenceState::FindSlot(EntityID a_slotId) const noexcept
	{
		return const_cast<PersistenceState*>(this)->FindSlot(a_slotId);
	}

	PersistentSlot* PersistenceState::FindByActor(EntityID a_actorId) noexcept
	{
		if (a_actorId == InvalidEntityID) {
			return nullptr;
		}
		for (auto& slot : _slots) {
			if (slot.actorId == a_actorId) {
				return &slot;
			}
		}
		return nullptr;
	}

	const PersistentSlot* PersistenceState::FindByActor(EntityID a_actorId) const noexcept
	{
		return const_cast<PersistenceState*>(this)->FindByActor(a_actorId);
	}

	PersistentSlot& PersistenceState::AppendSlot(std::uint32_t a_spellFormId)
	{
		PersistentSlot slot;
		slot.slotId = ++_nextSlotId;
		slot.spellFormId = a_spellFormId;
		_slots.push_back(slot);
		return _slots.back();
	}

	PersistentSlot* PersistenceState::TrackAlive(EntityID a_actorId, std::uint32_t a_spellFormId, float a_now)
	{
		if (a_actorId == InvalidEntityID) {
			return nullptr;
		}
		(void)a_now;

		if (auto* existing = FindByActor(a_actorId)) {
			if (existing->state != PersistentSlotState::Alive) {
				existing->state = PersistentSlotState::Alive;
				existing->diedAt = 0.0f;
				existing->reviveAt = 0.0f;
				existing->castAt = 0.0f;
				existing->reviveAttempts = 0;
			}
			if (a_spellFormId != 0) {
				existing->spellFormId = a_spellFormId;
			}
			return existing;
		}

		if (_slots.size() >= _settings.maxTrackedSlots) {
			return nullptr;
		}

		auto& slot = AppendSlot(a_spellFormId);
		slot.actorId = a_actorId;
		slot.state = PersistentSlotState::Alive;
		return &slot;
	}

	bool PersistenceState::BindAliveUnbound(std::uint32_t a_spellFormId, EntityID a_actorId, float a_now)
	{
		if (a_spellFormId == 0 || a_actorId == InvalidEntityID) {
			return false;
		}
		(void)a_now;
		for (auto& slot : _slots) {
			if (slot.state == PersistentSlotState::Alive && slot.actorId == InvalidEntityID && slot.spellFormId == a_spellFormId) {
				slot.actorId = a_actorId;
				slot.intentPending = true;
				return true;
			}
		}
		return false;
	}

	bool PersistenceState::BindReviving(std::uint32_t a_spellFormId, EntityID a_actorId, float a_now)
	{
		if (a_spellFormId == 0 || a_actorId == InvalidEntityID) {
			return false;
		}
		(void)a_now;
		for (auto& slot : _slots) {
			if (slot.state != PersistentSlotState::Reviving || slot.spellFormId != a_spellFormId) {
				continue;
			}
			// The previous incarnation is alive again: its death must not gate the next
			// revive of this very slot (the actor FormID changes on every revival).
			std::erase_if(_deaths, [&](const auto& a_entry) { return a_entry.first == slot.actorId; });

			slot.actorId = a_actorId;
			slot.state = PersistentSlotState::Alive;
			slot.diedAt = 0.0f;
			slot.reviveAt = 0.0f;
			slot.castAt = 0.0f;
			slot.reviveAttempts = 0;
			slot.intentPending = true;
			return true;
		}
		return false;
	}

	void PersistenceState::Remove(EntityID a_slotId)
	{
		std::erase_if(_slots, [&](const PersistentSlot& a_slot) { return a_slot.slotId == a_slotId; });
	}

	void PersistenceState::RemoveByActor(EntityID a_actorId)
	{
		if (a_actorId == InvalidEntityID) {
			return;
		}
		// Only a summon that is still alive can leave the roster; a Dead slot must keep
		// waiting for its revive, and a Reviving slot still carries the old actor id.
		std::erase_if(_slots, [&](const PersistentSlot& a_slot) {
			return a_slot.actorId == a_actorId && a_slot.state == PersistentSlotState::Alive;
		});
	}

	void PersistenceState::Clear()
	{
		_slots.clear();
		_deaths.clear();
	}

	bool PersistenceState::RecordDeath(EntityID a_summonId, float a_now)
	{
		if (a_summonId == InvalidEntityID) {
			return false;
		}
		PruneDeaths(a_now);
		for (const auto& [id, at] : _deaths) {
			if (id == a_summonId) {
				(void)at;
				return false;
			}
		}
		_deaths.emplace_back(a_summonId, a_now);
		return true;
	}

	bool PersistenceState::MarkDead(EntityID a_actorId, float a_now)
	{
		auto* slot = FindByActor(a_actorId);
		if (!slot || slot->state != PersistentSlotState::Alive) {
			return false;
		}

		RecordDeath(a_actorId, a_now);
		slot->state = PersistentSlotState::Dead;
		slot->diedAt = a_now;
		slot->reviveAt = a_now + _settings.reviveDelaySeconds;
		slot->castAt = 0.0f;
		slot->reviveAttempts = 0;
		slot->intentPending = false;
		return true;
	}

	std::optional<float> PersistenceState::LatestOtherDeath(EntityID a_excludedSummonId, float a_now) const
	{
		std::optional<float> latest;
		for (const auto& [id, at] : _deaths) {
			if (id == a_excludedSummonId) {
				continue;
			}
			if (at > a_now) {
				continue;  // clock restarted (new game / load): ignore future timestamps
			}
			if (!latest || at > *latest) {
				latest = at;
			}
		}
		return latest;
	}

	bool PersistenceState::IsRevivePaused(EntityID a_slotId, float a_now) const
	{
		if (_settings.recentDeathWindowSeconds <= 0.0f) {
			return false;
		}
		const auto* slot = FindSlot(a_slotId);
		if (!slot) {
			return false;
		}
		const auto latest = LatestOtherDeath(slot->actorId, a_now);
		return latest && (a_now - *latest) < _settings.recentDeathWindowSeconds;
	}

	std::vector<EntityID> PersistenceState::CollectDueRevives(float a_now)
	{
		PruneDeaths(a_now);

		std::vector<EntityID> due;
		for (auto& slot : _slots) {
			if (slot.state != PersistentSlotState::Dead) {
				continue;
			}

			if (_settings.recentDeathWindowSeconds > 0.0f) {
				if (const auto latest = LatestOtherDeath(slot.actorId, a_now)) {
					const float pauseUntil = *latest + _settings.recentDeathWindowSeconds;
					if (a_now < pauseUntil) {
						// PoE2 restarts the reviving timer when another minion dies.
						slot.reviveAt = std::max(slot.reviveAt, pauseUntil);
						continue;
					}
				}
			}

			if (a_now < slot.reviveAt) {
				continue;
			}
			due.push_back(slot.slotId);
		}
		return due;
	}

	void PersistenceState::MarkReviveCast(EntityID a_slotId, float a_now)
	{
		auto* slot = FindSlot(a_slotId);
		if (!slot || slot->state != PersistentSlotState::Dead) {
			return;
		}
		slot->state = PersistentSlotState::Reviving;
		slot->castAt = a_now;
	}

	std::vector<EntityID> PersistenceState::ExpireReviveCasts(float a_now)
	{
		std::vector<EntityID> expired;
		for (const auto& slot : _slots) {
			if (slot.state == PersistentSlotState::Reviving && a_now - slot.castAt >= _settings.reviveCastTimeoutSeconds) {
				expired.push_back(slot.slotId);
			}
		}
		return expired;
	}

	bool PersistenceState::MarkReviveFailed(EntityID a_slotId, float a_now)
	{
		auto* slot = FindSlot(a_slotId);
		if (!slot) {
			return false;
		}

		slot->castAt = 0.0f;
		if (slot->reviveAttempts + 1 >= _settings.maxReviveAttempts) {
			Remove(a_slotId);
			return false;
		}

		++slot->reviveAttempts;
		slot->state = PersistentSlotState::Dead;
		slot->reviveAt = a_now + _settings.reviveDelaySeconds;
		return true;
	}

	std::size_t PersistenceState::ReservedSummonCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& slot : _slots) {
			if (slot.state != PersistentSlotState::Alive || slot.actorId != InvalidEntityID) {
				++count;
			}
		}
		return count;
	}

	void PersistenceState::SetAppliedReserve(float a_reserve) noexcept
	{
		_appliedReserve = ClampFinite(a_reserve, 0.0f, kMaxReserve, 0.0f);
	}

	float PersistenceState::DesiredReserve(float a_maxMagicka) const noexcept
	{
		return ComputeMagickaReserve(ReservedSummonCount(), a_maxMagicka, _settings);
	}

	float PersistenceState::PendingReserveDelta(float a_maxMagicka) const noexcept
	{
		return DesiredReserve(a_maxMagicka) - _appliedReserve;
	}

	void PersistenceState::UpdateIntentFromModel(EntityID a_actorId, const SummonModel& a_model)
	{
		auto* slot = FindByActor(a_actorId);
		if (!slot || slot->state != PersistentSlotState::Alive || slot->intentPending) {
			return;
		}
		slot->intent.order = a_model.order;
		slot->intent.aggression = a_model.aggression;
		slot->intent.focusTargetId = a_model.currentTargetId;
	}

	std::optional<SummonIntent> PersistenceState::TakePendingIntent(EntityID a_actorId)
	{
		auto* slot = FindByActor(a_actorId);
		if (!slot || !slot->intentPending) {
			return std::nullopt;
		}
		slot->intentPending = false;
		return slot->intent;
	}

	SavedState PersistenceState::Save(float a_now) const
	{
		SavedState saved;
		saved.reserveApplied = _appliedReserve;
		saved.slots.reserve(_slots.size());

		for (const auto& slot : _slots) {
			SavedSlot entry;
			entry.spellFormId = slot.spellFormId;
			entry.intent = slot.intent;
			entry.reviveAttempts = slot.reviveAttempts;
			switch (slot.state) {
			case PersistentSlotState::Alive:
				entry.state = PersistentSlotState::Alive;
				entry.remainingReviveSeconds = 0.0f;
				break;
			case PersistentSlotState::Dead:
				entry.state = PersistentSlotState::Dead;
				entry.remainingReviveSeconds = std::max(0.0f, slot.reviveAt - a_now);
				break;
			case PersistentSlotState::Reviving:
				// The cast cannot be resumed after a load: retry like a fresh death.
				entry.state = PersistentSlotState::Dead;
				entry.remainingReviveSeconds = _settings.reviveDelaySeconds;
				break;
			}
			saved.slots.push_back(entry);
		}
		return saved;
	}

	void PersistenceState::Restore(const SavedState& a_saved, float a_now)
	{
		Clear();

		_appliedReserve = ClampFinite(a_saved.reserveApplied, 0.0f, kMaxReserve, 0.0f);

		for (const auto& saved : a_saved.slots) {
			if (_slots.size() >= _settings.maxTrackedSlots) {
				break;
			}
			if (saved.spellFormId == 0) {
				continue;
			}

			auto& slot = AppendSlot(saved.spellFormId);
			slot.intent = saved.intent;
			slot.reviveAttempts = std::min<std::uint32_t>(saved.reviveAttempts, 10);

			if (saved.state == PersistentSlotState::Alive) {
				// The actor FormID is not stable across a load: the adapter matches a live
				// summon of the same spell to this slot (BindAliveUnbound).
				slot.state = PersistentSlotState::Alive;
				slot.actorId = InvalidEntityID;
				slot.intentPending = true;
			} else {
				slot.state = PersistentSlotState::Dead;
				slot.actorId = InvalidEntityID;
				slot.reviveAt = a_now + ClampFinite(saved.remainingReviveSeconds, 0.0f, kMaxRemainingSeconds, 0.0f);
			}
		}
	}

	void PersistenceState::PruneDeaths(float a_now) noexcept
	{
		// The gate only reads `recentDeathWindowSeconds` back; a minute of slack keeps a
		// pause that was already scheduled and bounds the list.
		const float keep = std::max(_settings.recentDeathWindowSeconds, 10.0f) + 60.0f;
		std::erase_if(_deaths, [&](const auto& a_entry) { return a_now - a_entry.second > keep; });
	}
}
