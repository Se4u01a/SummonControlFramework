#include "Integrations/PrecisionBackend.h"
#include "Core/FrameworkContext.h"
#include "FriendlyFire/RelationshipClassifier.h"
#include "Integrations/PrecisionAPI.h"
#include "PCH.h"

#include <RE/P/PlayerCharacter.h>

namespace scf::integrations
{
	namespace
	{
		PRECISION_API::IVPrecision1* s_precisionAPI{ nullptr };

		PRECISION_API::PreHitCallbackReturn PrecisionPreHitHandler(const PRECISION_API::PrecisionHitData& a_hitData)
		{
			PRECISION_API::PreHitCallbackReturn ret{};

			if (!a_hitData.attacker || !a_hitData.target) {
				return ret;
			}

			auto* targetActor = a_hitData.target->As<RE::Actor>();
			if (!targetActor) {
				return ret;
			}

			auto& ctx = core::FrameworkContext::GetSingleton();
			const auto& settings = ctx.GetSettings().Get().friendlyFire;
			if (!settings.enabled) {
				return ret;
			}

			const auto attackerId = a_hitData.attacker->GetFormID();
			const auto targetId = targetActor->GetFormID();

			auto isAlly = [](core::EntityID a_id) -> bool {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
				if (!actor) {
					return false;
				}
				return actor->IsPlayerTeammate();
			};

			auto cmdChain = [](core::EntityID a_id) -> core::EntityID {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
				if (!actor) {
					return core::InvalidEntityID;
				}
				auto* commander = actor->GetCommandingActor().get();
				return commander ? commander->GetFormID() : core::InvalidEntityID;
			};

			const auto sourceKind = friendly_fire::RelationshipClassifier::ClassifySource(
				attackerId,
				ctx.GetRegistry(),
				0x14,
				isAlly,
				cmdChain);

			const bool isProtected = friendly_fire::RelationshipClassifier::IsProtectedTarget(
				targetId,
				ctx.GetRegistry(),
				settings,
				0x14,
				isAlly);

			if (!isProtected) {
				return ret;
			}

			const auto decision = friendly_fire::RelationshipClassifier::EvaluateAttack(
				sourceKind,
				friendly_fire::AttackKind::Melee,
				true,
				settings);

			if (decision.passThrough) {
				ret.bIgnoreHit = true;
				logger::debug("[PrecisionBackend] PreHit ignored friendly melee hit on summon 0x{:08X} from 0x{:08X}", targetId, attackerId);
			} else if (decision.blockDamage) {
				PRECISION_API::PreHitModifier dmgMod{};
				dmgMod.modifierType = PRECISION_API::PreHitModifier::ModifierType::Damage;
				dmgMod.modifierOperation = PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative;
				dmgMod.modifierValue = 0.0f;
				ret.modifiers.push_back(dmgMod);
			}

			return ret;
		}
	}

	bool PrecisionBackend::Initialize() noexcept
	{
		if (s_initialized) {
			return s_precisionAPI != nullptr;
		}

		// Try requesting interface versions starting from V4 down to V1
		void* api = PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V4);
		if (!api) {
			api = PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V3);
		}
		if (!api) {
			api = PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V2);
		}
		if (!api) {
			api = PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V1);
		}

		if (!api) {
			logger::info("[PrecisionBackend] Precision not detected. Precision-specific melee pass-through callback inactive.");
			s_initialized = true;
			return false;
		}

		s_precisionAPI = static_cast<PRECISION_API::IVPrecision1*>(api);
		const auto res = s_precisionAPI->AddPreHitCallback(SKSE::GetPluginHandle(), PrecisionPreHitHandler);
		if (res == PRECISION_API::APIResult::OK || res == PRECISION_API::APIResult::AlreadyRegistered) {
			logger::info("[PrecisionBackend] Successfully connected to Precision API and registered PreHitCallback.");
		} else {
			logger::warn("[PrecisionBackend] Connected to Precision API but AddPreHitCallback failed with code {}", static_cast<int>(res));
		}

		s_initialized = true;
		return true;
	}

	bool PrecisionBackend::IsAvailable() noexcept
	{
		return s_precisionAPI != nullptr;
	}
}
