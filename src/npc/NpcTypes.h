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
	/// Normalized values are used on purpose: the tracker does not know the
	/// size of the viewport of the view, so the bridge turns them into pixels
	/// once the view announced its size.
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

	/// The size of the viewport of the view, in pixels.
	///
	/// The view reports its own size, because the size of the window of the
	/// overlay is not the size of the screen of the game.
	struct Viewport
	{
		int width = 0;
		int height = 0;

		/// True when the size is usable for a conversion.
		[[nodiscard]] bool IsValid() const
		{
			return width > 0 && height > 0;
		}
	};

	/// The smallest radius of a head on the screen, in pixels.
	///
	/// It only keeps the offset above the head from collapsing for NPCs that
	/// are far away.
	constexpr int kMinHeadRadius = 2;

	/// The size of the font of the number in the stylesheet, in pixels.
	///
	/// The number is scaled up from that size. The value is a constant of the
	/// plugin, so it is written down only once: the plugin sends it to the
	/// view, which puts it into a global custom property of the document.
	constexpr int kBaseFontSize = 11;

	/// The bounds of the size of the font of the number, in pixels.
	constexpr int kMinFontSize = 11;
	constexpr int kMaxFontSize = 34;

	/// The size of the font of the number per pixel of the radius of the head.
	constexpr float kFontSizePerRadius = 0.9f;

	/// The number above the head of a single NPC, ready to be drawn.
	///
	/// The values are the plain CSS values of the number: the two offsets in
	/// pixels and the scale of the font. The view uses them as they are, it
	/// does not compute anything on its own.
	struct ViewMarker
	{
		/// The horizontal offset of the number, in pixels.
		int x = 0;

		/// The vertical offset of the number, in pixels.
		int y = 0;

		/// The scale of the font of the number.
		float scale = 1.0f;

		bool operator==(const ViewMarker&) const = default;
	};

	/// Builds the number above the head of an NPC for a viewport of a size.
	///
	/// The number is placed right above the head, so its offset is shifted up
	/// by the radius of the head and its lower edge touches the head. The size
	/// of the font follows that radius: a number of an NPC that is far away
	/// stays small, but it never becomes unreadable. It is sent as the scale
	/// of the base size of the stylesheet.
	///
	/// The vertical axis is flipped, because the game counts the height from
	/// the bottom and the CSS from the top.
	[[nodiscard]] inline ViewMarker BuildViewMarker(const Marker& a_marker, const Viewport& a_viewport)
	{
		// The helpers of the standard library are avoided on purpose: the
		// macros of the windows headers would break them.
		const int radius = static_cast<int>(std::lround(a_marker.radius * a_viewport.height));
		const int headRadius = radius < kMinHeadRadius ? kMinHeadRadius : radius;

		int size = static_cast<int>(std::lround(headRadius * kFontSizePerRadius));

		if (size < kMinFontSize) {
			size = kMinFontSize;
		}

		if (size > kMaxFontSize) {
			size = kMaxFontSize;
		}

		return {
			static_cast<int>(std::lround(a_marker.x * a_viewport.width)),
			static_cast<int>(std::lround((1.0f - a_marker.y) * a_viewport.height)) - headRadius,
			static_cast<float>(size) / static_cast<float>(kBaseFontSize),
		};
	}
}
