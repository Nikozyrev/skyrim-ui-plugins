#include "damage/DamageClassifier.h"

#include <map>

namespace Damage
{
	namespace
	{
		/// The resistances a magic effect can bypass, mapped onto the kind of
		/// damage they deal. The table is built once and never changed.
		const std::map<RE::ActorValue, DamageKind>& ResistKinds()
		{
			static const std::map<RE::ActorValue, DamageKind> kinds{
				{ RE::ActorValue::kResistFire, DamageKind::kFire },
				{ RE::ActorValue::kResistFrost, DamageKind::kFrost },
				{ RE::ActorValue::kResistShock, DamageKind::kShock },
				{ RE::ActorValue::kPoisonResist, DamageKind::kPoison },
			};

			return kinds;
		}
	}

	Classifier& Classifier::Get()
	{
		static Classifier singleton;
		return singleton;
	}

	DamageKind Classifier::ClassifyEffect(const RE::EffectSetting* a_effect)
	{
		if (!a_effect) {
			return DamageKind::kPhysical;
		}

		// The engine stores the resistance that the effect bypasses inside the
		// MGEF. This is the only element information the effect itself carries.
		const auto& kinds = ResistKinds();
		const auto it = kinds.find(a_effect->data.resistVariable);

		if (it != kinds.end()) {
			return it->second;
		}

		// A damage that is dealt through an effect but does not point at any
		// resistance is magic damage.
		return DamageKind::kMagic;
	}

	EffectContext& Classifier::Context()
	{
		// Effects are applied on the game thread, so the context of the calling
		// thread is the context of the current effect application.
		thread_local EffectContext context;
		return context;
	}

	DamageKind Classifier::Classify(RE::Actor* a_target) const
	{
		const auto& context = Context();

		// Only an effect that is currently modifying this very actor tells
		// anything about the nature of the damage.
		if (context.effect && context.target == a_target) {
			return ClassifyEffect(context.effect);
		}

		// Everything that is not caused by an effect is a physical hit.
		return DamageKind::kPhysical;
	}
}
