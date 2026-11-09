#include "damage/DamageTracker.h"

#include <algorithm>

namespace Damage
{
	Tracker& Tracker::Get()
	{
		static Tracker singleton;
		return singleton;
	}

	void Tracker::Record(const DamageEvent& a_event)
	{
		if (a_event.damage <= 0.0f) {
			return;
		}

		std::lock_guard<std::mutex> lock(_mutex);

		{
			auto& target = _actors[a_event.targetId];
			target.id = a_event.targetId;
			target.name = a_event.targetName;
			target.isPlayer = a_event.targetIsPlayer;
			target.damageTaken += a_event.damage;
			target.hitsTaken += 1;
		}

		if (a_event.attackerId != 0) {
			auto& attacker = _actors[a_event.attackerId];
			attacker.id = a_event.attackerId;
			attacker.name = a_event.attackerName;
			attacker.isPlayer = a_event.attackerIsPlayer;
			attacker.damageDealt += a_event.damage;
			attacker.hitsDealt += 1;
		}

		_recent.push_back(a_event);
		while (_recent.size() > kMaxRecentEvents) {
			_recent.pop_front();
		}

		PruneActors();
	}

	void Tracker::PruneActors()
	{
		if (_actors.size() <= kMaxTrackedActors) {
			return;
		}

		// Drop the least involved actors until the tracker is back within its
		// budget. The player is never dropped.
		while (_actors.size() > kMaxTrackedActors) {
			auto victim = _actors.end();

			for (auto it = _actors.begin(); it != _actors.end(); ++it) {
				if (it->second.isPlayer) {
					continue;
				}

				if (victim == _actors.end() || it->second.Total() < victim->second.Total()) {
					victim = it;
				}
			}

			if (victim == _actors.end()) {
				break;
			}

			_actors.erase(victim);
		}
	}

	void Tracker::Reset()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_actors.clear();
		_recent.clear();
	}

	std::vector<ActorStats> Tracker::Actors() const
	{
		std::lock_guard<std::mutex> lock(_mutex);

		std::vector<ActorStats> actors;
		actors.reserve(_actors.size());
		for (const auto& [id, stats] : _actors) {
			actors.push_back(stats);
		}

		std::sort(actors.begin(), actors.end(), [](const ActorStats& a_lhs, const ActorStats& a_rhs) {
			if (a_lhs.Total() != a_rhs.Total()) {
				return a_lhs.Total() > a_rhs.Total();
			}
			return a_lhs.id < a_rhs.id;
		});

		if (actors.size() > kMaxActors) {
			actors.resize(kMaxActors);
		}

		return actors;
	}

	std::vector<DamageEvent> Tracker::RecentEvents() const
	{
		std::lock_guard<std::mutex> lock(_mutex);

		std::vector<DamageEvent> events(_recent.begin(), _recent.end());
		std::reverse(events.begin(), events.end());
		return events;
	}
}
