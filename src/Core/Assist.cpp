#include "Core/Assist.h"

namespace scf::core
{
	bool ShouldAssistPlayerHit(const SummonModel& a_model, EntityID a_targetId) noexcept
	{
		if (a_targetId == InvalidEntityID || a_model.aggression == AggressionMode::Passive) {
			return false;
		}
		switch (a_model.order) {
		case SummonOrder::Follow:
			return true;
		case SummonOrder::Attack:
			return a_model.currentTargetId != a_targetId;
		default:
			return false;
		}
	}

	bool IsWithinAggressionRadius(const Vec3& a_owner, const Vec3& a_target, float a_radius) noexcept
	{
		if (!(a_radius > 0.0f)) {
			return true;
		}
		const float dx = a_owner.x - a_target.x;
		const float dy = a_owner.y - a_target.y;
		return dx * dx + dy * dy <= a_radius * a_radius;
	}
}
