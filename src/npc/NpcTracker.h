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
	/// Collects the point above every NPC that is currently visible on the
	/// screen.
	///
	/// The tracker has to run on the game thread: it reads the scene graph
	/// and the camera of the engine, which are only valid there.
	class Tracker
	{
	public:
		static Tracker& Get();

		/// Gathers the markers of all visible NPCs. Never throws.
		void Collect();

		/// The height of an actor, cached by its form id.
		///
		/// The height of an actor is measured from its bound, and that bound
		/// follows every animation of the NPC. The height is measured once
		/// and then kept, so the point of the marker stays where it is.
		///
		/// The height is in world units: 128 units are 1.828 metres.
		[[nodiscard]] float ResolveHeight(RE::Actor* a_actor, Damage::ActorId a_id);

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

		/// The heights of the NPCs that were seen during the session.
		///
		/// The heights are kept for the whole session, so the point of the
		/// marker of an NPC does not move when the NPC comes back.
		std::unordered_map<Damage::ActorId, float> _heights;
	};
}
