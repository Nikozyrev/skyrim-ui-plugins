#pragma once

#include "damage/DamageTypes.h"

#include <RE/A/Actor.h>
#include <RE/P/PlayerCharacter.h>

#include <string>

/// Small helpers around `RE::Actor` that are shared by the damage tracker
/// and the NPC head tracker.
namespace ActorUtil
{
	/// The player is identified by the singleton pointer first (robust against
	/// unusual form IDs) and by the well known player reference ID as fallback.
	[[nodiscard]] inline bool IsPlayer(const RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}

		if (const auto* player = RE::PlayerCharacter::GetSingleton(); player == a_actor) {
			return true;
		}

		return a_actor->IsPlayerRef();
	}

	/// Display name of an actor with a safe fallback chain. Never returns null.
	[[nodiscard]] inline std::string SafeName(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return {};
		}

		if (const auto* name = a_actor->GetDisplayFullName(); name && *name) {
			return name;
		}

		if (const auto* base = a_actor->GetBaseObject()) {
			if (const auto* name = base->GetName(); name && *name) {
				return name;
			}
		}

		return "Unknown";
	}
}
