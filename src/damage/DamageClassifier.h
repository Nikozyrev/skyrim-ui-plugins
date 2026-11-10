#pragma once

#include "damage/DamageTypes.h"

namespace RE
{
	class Actor;
	class EffectSetting;
}

namespace Damage
{
	/// The magic effect that is currently writing an actor value on this thread.
	///
	/// `Actor::HandleHealthDamage` is called from inside the effect that applies
	/// the damage, so the effect is the only reliable source of the elemental
	/// nature of the damage. The context is thread local, because the engine
	/// applies effects on the game thread only.
	struct EffectContext
	{
		/// The actor whose actor value is being modified.
		RE::Actor* target = nullptr;

		/// The magic effect (MGEF) that performs the modification.
		const RE::EffectSetting* effect = nullptr;

		/// The actor that cast the effect, if there is one.
		RE::Actor* caster = nullptr;
	};

	/// Maps damage applications onto the kind of damage they are.
	class Classifier
	{
	public:
		static Classifier& Get();

		/// Maps a magic effect onto the damage kind it deals.
		///
		/// Fire, frost, shock and poison effects point at the resistance they
		/// bypass. Everything else that reaches an actor through an effect is
		/// plain magic damage.
		[[nodiscard]] static DamageKind ClassifyEffect(const RE::EffectSetting* a_effect);

		/// The effect context of the calling thread.
		[[nodiscard]] static EffectContext& Context();

		/// Determines the kind of a damage application on the given target.
		[[nodiscard]] DamageKind Classify(RE::Actor* a_target) const;
	};
}
