#pragma once

namespace Damage
{
	/// Installs the engine hooks that observe applied health damage.
	///
	/// The hook is placed on `Actor::HandleHealthDamage`, which is the single
	/// point the engine funnels *every* health damage application through
	/// (melee, ranged, magic, poisons, traps, falls, script driven damage).
	/// It reports the true, post mitigation amount and the responsible attacker.
	///
	/// `HandleHealthDamage` is virtual, therefore every concrete actor class
	/// that owns a copy of the vtable has to be patched separately:
	///   * `RE::Character`      - every NPC;
	///   * `RE::PlayerCharacter` - the player.
	///
	/// The hook does not know the element of the damage, so the magic effect
	/// classes are hooked as well. `ValueModifierEffect::ModifyActorValue` is
	/// the point where the engine applies an effect, and the health change it
	/// causes happens inside that call. The effect therefore tells the damage
	/// apart as fire, frost, shock, poison or plain magic.
	class Hook
	{
	public:
		/// Installs the hooks. Returns true when at least one hook is active.
		/// The call is idempotent and never throws.
		static bool Install();

		/// True when at least one hook is active.
		[[nodiscard]] static bool IsInstalled();
	};
}
