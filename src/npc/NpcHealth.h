#pragma once

#include "damage/DamageTypes.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace RE
{
	class Actor;
}

namespace Npc
{
	/// The health of a single NPC.
	///
	/// The health is drawn as a bar above the head of the NPC, so the view
	/// needs the health the NPC has right now and the health it can have at
	/// most.
	struct Health
	{
		/// The health the NPC has right now.
		float current = 0.0f;

		/// The health the NPC can have at most.
		float max = 0.0f;

		/// True when the health can be drawn as a bar.
		[[nodiscard]] bool IsValid() const
		{
			return max > 0.0f && current >= 0.0f;
		}

		bool operator==(const Health&) const = default;
	};

	/// Keeps the health of the NPCs that the overlay shows.
	///
	/// The maximum health of an NPC is the health of its form: it does not
	/// move when the NPC is damaged or healed. The current health is the
	/// maximum health without the damage the NPC took, and the damage hook
	/// hands that damage over at the very moment the NPC is hit. So the bar
	/// above the head of an NPC reacts to a hit without waiting for the next
	/// frame.
	///
	/// The tracker is safe to use from any thread.
	class HealthTracker
	{
	public:
		static HealthTracker& Get();

		/// Keeps the maximum health of an NPC up to date.
		///
		/// The tracker calls it for every NPC that is on the screen, so an
		/// NPC that just entered the screen gets its health as well.
		/// @return the health of the actor
		Health Sync(RE::Actor* a_actor);

		/// Takes the damage off the health of an NPC.
		///
		/// The damage hook calls it for every hit an NPC takes, no matter
		/// who hit it.
		void ApplyDamage(RE::Actor* a_actor, float a_damage);

	private:
		HealthTracker() = default;

		/// The state of a single NPC.
		struct Entry
		{
			/// The damage the NPC took since it was hit the last time.
			float damageTaken = 0.0f;

			/// The health the NPC can have at most.
			float max = 0.0f;

			/// The moment the NPC was hit the last time.
			std::chrono::steady_clock::time_point lastHit{};
		};

		/// Guards the state of the NPCs, so that the game thread and the
		/// thread of the UI never touch it at the same time.
		mutable std::mutex _mutex;

		/// The state of the NPCs, keyed by their form id.
		std::unordered_map<Damage::ActorId, Entry> _entries;
	};
}
