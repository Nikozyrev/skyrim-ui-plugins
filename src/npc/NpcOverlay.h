#pragma once

#include "npc/NpcTypes.h"

#include <REL/Relocation.h>

#include <atomic>
#include <chrono>
#include <cstdint>

namespace Npc
{
	/// Drives the NPC head overlay.
	///
	/// The overlay refreshes the heads of the NPCs that are visible on the
	/// screen and pushes them to the view. It runs on the game thread,
	/// because the scene graph and the camera of the engine are only valid
	/// there.
	class Overlay
	{
	public:
		static Overlay& Get();

		/// Installs the hook of the frame update of the game.
		static bool Install();

		void Start();
		void Stop();

	private:
		Overlay() = default;
		~Overlay() = default;

		/// One refresh of the markers.
		void Tick();

		/// Called by the hook of the frame update.
		void OnFrame();

		/// One hook of the frame update.
		///
		/// Every place where the game calls its main update needs its own
		/// copy of the hook, because every copy needs its own pointer to the
		/// original function. The copy forwards all four arguments of the
		/// call and returns the result of the original function, so the game
		/// gets exactly the values it would have got without the hook.
		template <std::size_t I>
		struct FrameHook
		{
			using Thunk_t = std::uint64_t (*)(void*, void*, void*, void*);

			static std::uint64_t Thunk(void* a_arg1, void* a_arg2, void* a_arg3, void* a_arg4)
			{
				// The camera is read before the game updates the frame, so the
				// overlay can tell whether the engine moves the camera while
				// the frame is updated or after it.
				Overlay::Get().CaptureCamera();

				const auto started = std::chrono::steady_clock::now();

				// The game updates the frame first: the overlay then sees a
				// finished scene and a fresh camera.
				const auto result = Original(a_arg1, a_arg2, a_arg3, a_arg4);

				Overlay::Get().MeasureUpdate(std::chrono::steady_clock::now() - started);

				Overlay::Get().OnFrame();

				return result;
			}

			static inline REL::Relocation<Thunk_t> Original;
		};

		/// Installs the hook of the frame update on one place of a call.
		template <std::size_t I>
		static bool InstallFrameHook(std::uintptr_t a_site);

		/// Reads the camera of the world before the game updates the frame.
		void CaptureCamera();

		/// Measures how long the update of the frame takes.
		///
		/// The number says how much of the frame is left after the overlay
		/// measured the markers, so it tells whether the payload still has
		/// the time of the frame to reach the view.
		void MeasureUpdate(std::chrono::steady_clock::duration a_duration);

		/// Measures how long the overlay itself takes.
		///
		/// The number is the price of the overlay: it is the time that the
		/// collection of the NPCs, the projection of the markers and the
		/// push of the positions take away from the frame of the game.
		void MeasureOverlay(std::chrono::steady_clock::duration a_duration);

		std::atomic_bool _running{ false };

		/// The state of the camera before the game updated the frame.
		CameraState _cameraBefore;

		/// The state of the camera at the moment the markers were measured.
		///
		/// It is the state of the camera of the last frame, so the overlay
		/// can tell whether the camera moved while the frame was updated or
		/// outside of it.
		CameraState _cameraLastFrame;

		/// The time between two frames of the game, in milliseconds.
		double _frameInterval = 16.0;

		/// The time that the update of the frame takes, in milliseconds.
		double _updateDuration = 0.0;

		/// The time that the overlay itself takes, in milliseconds.
		double _overlayDuration = 0.0;

		/// The moment of the last frame of the game.
		std::chrono::steady_clock::time_point _lastTick{};

		/// True when the camera that was measured was already one frame old.
		///
		/// The engine moves the camera in the update of the frame or after
		/// it. When it moves it afterwards, the camera that the markers are
		/// projected with is the one of the last frame, so every marker is
		/// one frame behind its NPC.
		bool _cameraStale = false;

		/// The number of frames between two lines about the delay of the
		/// frames.
		int _framesSinceSyncLog = 0;

		/// The number of frames between two lines about the delay.
		///
		/// A second is about the right distance between two lines: the delay
		/// is a number that does not change much from one frame to the next.
		static constexpr int kSyncLogInterval = 120;
	};
}
