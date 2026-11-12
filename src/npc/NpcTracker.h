#pragma once

#include "npc/NpcTypes.h"

#include <vector>

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

		std::vector<Marker> _markers;
	};
}
