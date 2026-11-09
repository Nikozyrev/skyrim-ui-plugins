#pragma once

#include <cstdint>
#include <string>

namespace Damage
{
	/// Stable identifier of an actor. Mirrors RE::FormID (plugin index + record id).
	/// The value 0 is reserved for "no actor" (environmental damage, unattributed hits).
	using ActorId = std::uint32_t;

	/// Aggregated damage statistics of a single actor.
	struct ActorStats
	{
		ActorId id = 0;
		std::string name;
		bool isPlayer = false;
		float damageDealt = 0.0f;
		float damageTaken = 0.0f;
		std::uint32_t hitsDealt = 0;
		std::uint32_t hitsTaken = 0;

		[[nodiscard]] float Total() const noexcept { return damageDealt + damageTaken; }
	};

	/// A single damage application.
	struct DamageEvent
	{
		ActorId attackerId = 0;
		std::string attackerName;
		bool attackerIsPlayer = false;

		ActorId targetId = 0;
		std::string targetName;
		bool targetIsPlayer = false;

		/// Applied damage, always positive.
		float damage = 0.0f;

		/// Milliseconds since plugin start, used only for ordering / display.
		std::uint64_t timeMs = 0;
	};
}
