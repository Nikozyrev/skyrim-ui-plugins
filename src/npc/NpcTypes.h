#pragma once

#include "damage/DamageTypes.h"

#include <cstdint>
#include <string>

namespace Npc
{
	/// The head of a single NPC, ready to be drawn by the view.
	///
	/// The coordinates are normalized to the viewport of the game:
	///   * `x` - 0 is the left edge of the screen, 1 the right one;
	///   * `y` - 0 is the bottom edge of the screen, 1 the top one;
	///   * `radius` - the radius of the head circle as a fraction of the
	///     height of the screen.
	///
	/// Normalized values are used on purpose: the view knows its own size
	/// and can therefore place the circle correctly at any resolution.
	struct Marker
	{
		Damage::ActorId id = 0;
		std::string name;

		float x = 0.0f;
		float y = 0.0f;
		float radius = 0.0f;
	};
}
