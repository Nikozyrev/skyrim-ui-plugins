#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Damage
{
	/// Stable identifier of an actor. Mirrors RE::FormID (plugin index + record id).
	/// The value 0 is reserved for "no actor" (environmental damage, unattributed hits).
	using ActorId = std::uint32_t;

	/// The elemental nature of a damage application.
	///
	/// The kind is derived from the magic effect (MGEF) that applies the damage.
	/// The engine stores the resistance that the effect bypasses in
	/// `EffectSetting::data.resistVariable`, which is the very same signal that
	/// Floating Damage NG uses for its colour coding.
	enum class DamageKind : std::uint8_t
	{
		kPhysical = 0,  ///< Weapon hits, falls and other non elemental damage.
		kFire,          ///< Fire damage.
		kFrost,         ///< Frost damage.
		kShock,         ///< Shock / lightning damage.
		kPoison,        ///< Poison damage.
		kMagic,         ///< Magic damage without a matching resistance.
	};

	/// Amount of known damage kinds.
	inline constexpr std::size_t kDamageKindCount = 6;

	/// Damage accumulated per damage kind.
	using DamageKindTotals = std::array<float, kDamageKindCount>;

	/// Stable, machine readable name of a damage kind ("fire", "frost", ...).
	[[nodiscard]] const char* ToString(DamageKind a_kind);

	/// Index of a damage kind inside `DamageKindTotals`.
	[[nodiscard]] constexpr std::size_t KindIndex(DamageKind a_kind) noexcept
	{
		return static_cast<std::size_t>(a_kind);
	}

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

		/// Damage the actor dealt, broken down by damage kind.
		DamageKindTotals damageDealtByKind{};

		/// Damage the actor took, broken down by damage kind.
		DamageKindTotals damageTakenByKind{};

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

		/// The elemental nature of the damage.
		DamageKind kind = DamageKind::kPhysical;

		/// Milliseconds since plugin start, used only for ordering / display.
		std::uint64_t timeMs = 0;
	};
}
