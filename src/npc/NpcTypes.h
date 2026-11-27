#pragma once

#include "damage/DamageTypes.h"
#include "npc/NpcHealth.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace Npc
{
	/// The marker of a single NPC, ready to be drawn by the view.
	///
	/// The marker sits on the upper middle point of the NPC, so the number of
	/// the damage is drawn above the whole body of the NPC.
	///
	/// The coordinates are normalized to the viewport of the game:
	///   * `x` - 0 is the left edge of the screen, 1 the right one;
	///   * `y` - 0 is the bottom edge of the screen, 1 the top one;
	///   * `radius` - the radius of the head of the NPC as a fraction of the
	///     height of the screen. It is the size of the NPC on the screen, so
	///     it follows the distance to the camera.
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

		/// The health of the NPC at the moment of the collection.
		///
		/// The health is drawn as a bar above the head of the NPC, so it
		/// travels with the marker.
		Health health;
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

	/// The state of the camera that renders the world.
	///
	/// The state is a plain copy of the transform of the camera, so it can be
	/// compared with the state of another moment of the frame. The overlay
	/// uses it to find out when the engine moves the camera: the markers are
	/// only as fresh as the camera they are projected with, so a camera that
	/// is one frame old puts every marker one frame behind its NPC.
	struct CameraState
	{
		/// The position of the camera in the world.
		float position[3] = { 0.0f, 0.0f, 0.0f };

		/// The rotation of the camera in the world.
		float rotation[9] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };

		/// True when the camera was found.
		bool valid = false;

		/// True when the camera is at the same place as another one.
		///
		/// The comparison is fuzzy: the engine writes the transform of the
		/// camera in single precision, so the last bits of the numbers are
		/// not stable. A camera that cannot be compared counts as the same
		/// one, so a missing camera never looks like a camera that moved.
		[[nodiscard]] bool Matches(const CameraState& a_other) const
		{
			if (!valid || !a_other.valid) {
				return true;
			}

			for (std::size_t i = 0; i < 3; ++i) {
				if (std::abs(position[i] - a_other.position[i]) > 0.01f) {
					return false;
				}
			}

			for (std::size_t i = 0; i < 9; ++i) {
				if (std::abs(rotation[i] - a_other.rotation[i]) > 0.0001f) {
					return false;
				}
			}

			return true;
		}
	};

	/// Rounds a value to a tenth of a pixel.
	///
	/// The offsets of a marker are sent with a tenth of a pixel, so the view
	/// can tell how fast a marker moves and put it where it will be when it
	/// is drawn.
	[[nodiscard]] inline float RoundToTenth(float a_value)
	{
		return std::round(a_value * 10.0f) / 10.0f;
	}

	/// Rounds a value to a hundredth.
	[[nodiscard]] inline float RoundToHundredth(float a_value)
	{
		return std::round(a_value * 100.0f) / 100.0f;
	}

	/// Reads the state of the camera that renders the world.
	///
	/// The state is a copy of the transform of the camera, so it stays the
	/// same even when the camera moves on. The overlay compares the states
	/// of the frames to find out when the engine moves the camera.
	[[nodiscard]] CameraState CaptureCameraState();

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
	///
	/// The offsets are rounded to a tenth of a pixel: a marker that moves
	/// fast has to be placed between two pixels, otherwise the view cannot
	/// tell how fast it moves and cannot put it where it will be when it is
	/// drawn.
	struct ViewMarker
	{
		/// The horizontal offset of the number, in pixels.
		float x = 0.0f;

		/// The vertical offset of the number, in pixels.
		float y = 0.0f;

		/// The scale of the font of the number.
		float scale = 1.0f;

		bool operator==(const ViewMarker&) const = default;
	};

	/// Builds the number above the marker point of an NPC for a viewport of a
	/// size.
	///
	/// The number is placed right above the point, so its offset is shifted up
	/// by the radius of the head and its lower edge touches the point. The size
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
			RoundToTenth(a_marker.x * a_viewport.width),
			RoundToTenth((1.0f - a_marker.y) * a_viewport.height) - static_cast<float>(headRadius),
			RoundToHundredth(static_cast<float>(size) / static_cast<float>(kBaseFontSize)),
		};
	}
}
