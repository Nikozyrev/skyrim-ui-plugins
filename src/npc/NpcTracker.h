#pragma once

#include "npc/NpcTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace RE
{
	class Actor;
}

namespace Npc
{
	/// Collects the heads of every NPC that is currently visible on the screen.
	///
	/// The tracker has to run on the game thread: it reads the scene graph
	/// and the camera of the engine, which are only valid there.
	class Tracker
	{
	public:
		static Tracker& Get();

		/// Gathers the heads of all visible NPCs. Never throws.
		void Collect();

		/// The markers of the last collection.
		[[nodiscard]] const std::vector<Marker>& Markers() const noexcept { return _markers; }

	private:
		Tracker() = default;

		/// The display name of an actor, cached by its form id.
		///
		/// The name of an NPC does not change while it is on the screen, so it
		/// is read from the engine only once per NPC instead of once per
		/// frame.
		[[nodiscard]] const std::string& ResolveName(RE::Actor* a_actor, Damage::ActorId a_id);

		std::vector<Marker> _markers;

		/// The names of the NPCs that were seen during the last collection.
		std::unordered_map<Damage::ActorId, std::string> _names;
	};
}
