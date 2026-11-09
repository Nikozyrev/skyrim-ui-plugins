#pragma once

#include "damage/DamageTypes.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Damage
{
	/// Thread-safe accumulator for damage events.
	///
	/// The tracker keeps two things:
	///   * per-actor aggregates (damage dealt / taken, hit counters);
	///   * a bounded ring buffer of the most recent events, used to populate the
	///     live feed right after the view becomes ready.
	///
	/// Every public method is safe to call from any thread.
	class Tracker
	{
	public:
		/// Maximum amount of actors reported to the UI. Keeps the payload small.
		static constexpr std::size_t kMaxActors = 20;

		/// Maximum amount of actors kept in memory. Prevents an endless session
		/// from growing the tracker without bounds.
		static constexpr std::size_t kMaxTrackedActors = 256;

		/// Maximum amount of recent events kept in memory.
		static constexpr std::size_t kMaxRecentEvents = 24;

		static Tracker& Get();

		/// Registers a damage application and updates the aggregates.
		void Record(const DamageEvent& a_event);

		/// Drops all accumulated data (called on save load / new game).
		void Reset();

		/// Aggregates, sorted by total damage (descending).
		[[nodiscard]] std::vector<ActorStats> Actors() const;

		/// Most recent events, newest first.
		[[nodiscard]] std::vector<DamageEvent> RecentEvents() const;

	private:
		Tracker() = default;

		/// Drops the least involved actors when the tracker exceeds its budget.
		void PruneActors();

		mutable std::mutex _mutex;
		std::unordered_map<ActorId, ActorStats> _actors;
		std::deque<DamageEvent> _recent;
	};
}
