#pragma once

#include "damage/DamageTypes.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

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

		/// The display name of the NPC.
		///
		/// The name is a view into the cache of the tracker, so it is only
		/// valid as long as the NPC stays visible.
		std::string_view name;

		float x = 0.0f;
		float y = 0.0f;
		float radius = 0.0f;
	};

	/// The position of a marker, quantized to integers.
	///
	/// The view receives the positions as integers: they are a lot cheaper to
	/// send than floats (a float32 needs up to 19 characters as text, an
	/// integer four) and the view turns them back into normalized coordinates.
	struct QuantizedMarker
	{
		/// The number of steps the normalized coordinates are scaled to.
		///
		/// 10000 steps over the width of the screen are far below the pixel
		/// resolution of the view, so the markers do not visibly jump.
		static constexpr int kScale = 10000;

		/// The margin around the viewport that is still drawn.
		///
		/// The coordinates are shifted by it before they are quantized, so a
		/// quantized coordinate is never negative.
		static constexpr float kMargin = 0.05f;

		/// The horizontal position, quantized.
		int x = 0;

		/// The vertical position, quantized.
		int y = 0;

		/// The radius of the head, quantized.
		int r = 0;

		/// Quantizes a normalized coordinate of the viewport.
		[[nodiscard]] static int QuantizePosition(float a_value)
		{
			return static_cast<int>(std::lround((a_value + kMargin) * kScale));
		}

		/// Quantizes the radius of a head (a fraction of the screen height).
		[[nodiscard]] static int QuantizeRadius(float a_value)
		{
			return static_cast<int>(std::lround(a_value * kScale));
		}

		/// Quantizes a marker.
		[[nodiscard]] static QuantizedMarker FromMarker(const Marker& a_marker)
		{
			return {
				QuantizePosition(a_marker.x),
				QuantizePosition(a_marker.y),
				QuantizeRadius(a_marker.radius),
			};
		}

		bool operator==(const QuantizedMarker&) const = default;
	};
}
