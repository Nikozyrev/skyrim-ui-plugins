#include "npc/NpcHealth.h"

#include "util/ActorUtil.h"

#include <RE/A/Actor.h>
#include <RE/A/ActorValues.h>
#include <RE/T/TESNPC.h>

namespace Npc
{
	namespace
	{
		/// The time an NPC needs to heal again after it was hit.
		///
		/// The engine waits a while before the health of an actor is
		/// regenerated, and the regeneration itself takes a few seconds. An
		/// NPC that was not hit for that long is healed again, so its
		/// damage is forgotten.
		constexpr auto kHealDelay = std::chrono::seconds(22);

		/// The smaller of the two values.
		///
		/// The helpers of the standard library are avoided on purpose: the
		/// macros of the windows headers would break them.
		float Smaller(float a_value, float a_other)
		{
			return a_value < a_other ? a_value : a_other;
		}

		/// A value that is neither below nor above its bounds.
		float Clamp(float a_value, float a_min, float a_max)
		{
			if (a_value < a_min) {
				return a_min;
			}

			return Smaller(a_value, a_max);
		}

		/// The health an actor can have at most.
		///
		/// The health of the form of the actor is the health it has at most:
		/// it does not move when the actor is damaged or healed, so it is the
		/// length of the bar.
		float ReadMaxHealth(RE::Actor* a_actor)
		{
			auto* base = a_actor->GetActorBase();

			if (!base) {
				return 0.0f;
			}

			const auto value = base->GetActorValue(RE::ActorValue::kHealth);
			return value > 0.0f ? value : 0.0f;
		}
	}

	HealthTracker& HealthTracker::Get()
	{
		static HealthTracker singleton;
		return singleton;
	}

	Health HealthTracker::Sync(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return {};
		}

		const auto id = a_actor->GetFormID();
		const auto max = ReadMaxHealth(a_actor);
		const auto now = std::chrono::steady_clock::now();

		std::lock_guard<std::mutex> lock(_mutex);

		auto& entry = _entries[id];

		// The maximum health of an NPC does not change, but it is read
		// again anyway: it keeps the tracker honest when the form of the
		// NPC was changed in the meantime.
		if (max > 0.0f) {
			entry.max = max;
		}

		// An NPC that was not hit for a while is healed again, so its
		// damage is forgotten.
		if (entry.damageTaken > 0.0f && now - entry.lastHit > kHealDelay) {
			entry.damageTaken = 0.0f;
		}

		Health health;
		health.max = entry.max;
		health.current = Clamp(entry.max - entry.damageTaken, 0.0f, entry.max);

		return health;
	}

	void HealthTracker::ApplyDamage(RE::Actor* a_actor, float a_damage)
	{
		if (!a_actor || a_damage <= 0.0f) {
			return;
		}

		// The player is not shown by the overlay, so its health is of no
		// interest here.
		if (ActorUtil::IsPlayer(a_actor)) {
			return;
		}

		const auto id = a_actor->GetFormID();
		const auto max = ReadMaxHealth(a_actor);

		std::lock_guard<std::mutex> lock(_mutex);

		auto& entry = _entries[id];

		if (max > 0.0f) {
			entry.max = max;
		}

		// The damage of a hit is added to the damage the NPC took, and the
		// moment of the hit is remembered: an NPC that is not hit for a
		// while is healed again.
		entry.damageTaken += a_damage;
		entry.lastHit = std::chrono::steady_clock::now();
	}
}
