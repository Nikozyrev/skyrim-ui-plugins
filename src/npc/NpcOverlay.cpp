#include "npc/NpcOverlay.h"

#include "npc/NpcSlots.h"
#include "npc/NpcTracker.h"
#include "ui/PrismaBridge.h"

#include <chrono>
#include <exception>

namespace Npc
{
	namespace
	{
		/// The wall clock of the system, in milliseconds since the epoch.
		///
		/// The view compares this clock with its own one, so the time of a
		/// frame tells it how old the positions of that frame are.
		///
		/// The precise clock of Windows is used on purpose: the coarse clock
		/// of the system ticks every few milliseconds only, and a frame of the
		/// game is shorter than that.
		std::int64_t WallClockMs()
		{
			FILETIME fileTime{};

			GetSystemTimePreciseAsFileTime(&fileTime);

			// The file time counts the intervals of 100 nanoseconds since
			// the first of January 1601.
			constexpr std::int64_t kTicksPerMillisecond = 10000;
			constexpr std::int64_t kEpochOffset = 11644473600000LL;

			const auto ticks = (static_cast<std::int64_t>(fileTime.dwHighDateTime) << 32) |
							   static_cast<std::int64_t>(fileTime.dwLowDateTime);

			return ticks / kTicksPerMillisecond - kEpochOffset;
		}
	}

	Overlay& Overlay::Get()
	{
		static Overlay singleton;
		return singleton;
	}

	bool Overlay::Install()
	{
		const auto version = REL::Module::get().version();

		// The place of the call that the hook replaces is different in every
		// version of the game: the engine moves the code around, so the offset
		// of the call inside the function is not stable. The id of the
		// function is stable, so the function is taken from the address
		// library and the offset is picked by the version of the game.
		std::uintptr_t site = 0;

		if (version >= REL::Version(1, 7, 99, 0)) {
			// AE 1.7.104: the engine was rebuilt, the update of the frame
			// (36564) is now called from the function 36544.
			site = REL::RelocationID(35545, 36544).address() + 0x160;
		}
		else if (version >= REL::Version(1, 6, 0, 0)) {
			// AE 1.6: the hook sits inside the update of the frame (36564).
			site = REL::RelocationID(35565, 36564).address() + 0xC26;
		}
		else {
			// SE 1.5: the hook sits inside the update of the frame (35565).
			site = REL::RelocationID(35565, 36564).address() + 0x748;
		}

		return InstallFrameHook<0>(site);
	}

	template <std::size_t I>
	bool Overlay::InstallFrameHook(std::uintptr_t a_site)
	{
		auto& trampoline = SKSE::GetTrampoline();

		// The hook replaces the call of the game with a call of our thunk. The
		// trampoline gives back the function that the game called there, so
		// the thunk can call it with the same arguments and give its result
		// back to the game.
		FrameHook<I>::Original = trampoline.write_call<5>(a_site, FrameHook<I>::Thunk);

		logger::info("NPC overlay: the frame update hook is installed.");

		return true;
	}

	void Overlay::Start()
	{
		_running.store(true);

		logger::info("NPC overlay started.");
	}

	void Overlay::Stop()
	{
		_running.store(false);
		logger::info("NPC overlay stopped.");
	}

	void Overlay::CaptureCamera()
	{
		if (!_running.load()) {
			return;
		}

		_cameraBefore = Npc::CaptureCameraState();
	}

	void Overlay::MeasureUpdate(std::chrono::steady_clock::duration a_duration)
	{
		const auto duration = std::chrono::duration<double, std::milli>(a_duration).count();

		// A single frame is not a good measure: the average of the last
		// frames smooths the jitter.
		_updateDuration = _updateDuration * 0.9 + duration * 0.1;
	}

	void Overlay::MeasureOverlay(std::chrono::steady_clock::duration a_duration)
	{
		const auto duration = std::chrono::duration<double, std::milli>(a_duration).count();

		// A single frame is not a good measure: the average of the last
		// frames smooths the jitter.
		_overlayDuration = _overlayDuration * 0.9 + duration * 0.1;
	}

	void Overlay::OnFrame()
	{
		if (!_running.load()) {
			return;
		}

		const auto started = std::chrono::steady_clock::now();

		try {
			Tick();
		}
		catch (const std::exception& a_error) {
			logger::error("NPC overlay: the overlay tick threw: {}", a_error.what());
		}
		catch (...) {
			logger::error("NPC overlay: the overlay tick threw an unknown error.");
		}

		// The price of the overlay is measured on every frame, also when the
		// tick failed, so the log always tells what the overlay costs.
		MeasureOverlay(std::chrono::steady_clock::now() - started);
	}

	void Overlay::Tick()
	{
		if (!_running.load()) {
			return;
		}

		// The time between two frames of the game. It tells how long the
		// camera of the last frame is old, when the engine moves the camera
		// after the update of the frame.
		const auto now = std::chrono::steady_clock::now();

		if (_lastTick.time_since_epoch().count() != 0) {
			const auto delta = std::chrono::duration<double, std::milli>(now - _lastTick).count();

			// A single frame is not a good measure: the average of the last
			// frames smooths the jitter of the length of a frame.
			_frameInterval = _frameInterval * 0.9 + delta * 0.1;
		}

		_lastTick = now;

		// Проверяем, что мы не в главном меню и игра не на паузе
		auto* ui = RE::UI::GetSingleton();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!ui || !player || ui->GameIsPaused() || ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
			return;
		}

		try {
			// The overlay is pointless without a view to draw on.
			if (UI::Bridge::Get().IsReady()) {
				// The moment the positions are measured. The view compares it
				// with its own clock, so it knows how old the positions of
				// the frame are and can put every marker where it will be
				// when it is drawn.
				const auto time = WallClockMs();

				// The state of the camera at the moment the markers are
				// measured.
				const auto camera = Npc::CaptureCameraState();

				// The engine moves the camera either while the frame is
				// updated or after it. When it moves it afterwards, the
				// camera that the markers are projected with is the one of
				// the last frame, so every marker is one frame behind its
				// NPC. The view cannot know that, so the bridge is told how
				// many frames it has to compensate.
				const bool movedInUpdate = !_cameraBefore.Matches(camera);
				const bool movedBetweenFrames = !_cameraLastFrame.Matches(camera);

				_cameraStale = movedBetweenFrames && !movedInUpdate;
				_cameraLastFrame = camera;

				// The camera of the frame is one frame old when the engine
				// moves it after the update of the frame. Every marker is
				// then a frame behind its NPC, so one more frame has to be
				// compensated.
				const auto leadFrames = _cameraStale ? 1 : 0;

				// The tracker clears its markers when there is nothing to
				// track, so the pool also frees the slots of the NPCs that
				// left the screen.
				Npc::Tracker::Get().Collect();
				const auto& markers = Npc::Tracker::Get().Markers();

				// The pool owns the slots: it keeps the slot of an NPC that
				// is still on the screen, frees the slot of an NPC that left
				// and hands a free one to an NPC that entered the screen.
				const auto delta = Npc::SlotPool::Get().Update(markers);

				// The bridge takes care of the traffic: it only sends the
				// slots that really changed and it skips a frame in which no
				// marker moved.
				//
				// The bridge also calculates the future: the page of the view
				// is drawn in the frame after the one that was measured, so
				// every marker is moved a whole frame forward to put it back
				// on the head of its NPC. The view only writes the numbers it
				// gets into the elements of its slots.
				UI::Bridge::Get().SendNpcSlots(delta,
											   time,
											   static_cast<int>(_frameInterval + 0.5),
											   leadFrames);

				// The view tells the plugin how long the positions of a frame
				// needed until they were on the screen. The numbers of the
				// frame are written to the log every few seconds, so the
				// delay of the whole path, the price of the overlay and the
				// horizon of the prediction are visible.
				if (++_framesSinceSyncLog >= kSyncLogInterval) {
					_framesSinceSyncLog = 0;

					const auto age = UI::Bridge::Get().SyncAge();
					const auto frames = UI::Bridge::Get().PredictionFrames();
					const auto horizon = UI::Bridge::Get().PredictionHorizon();

					logger::info("NPC overlay: the positions of the last frame were {} ms old, "
								 "the game draws every {} ms, the game updates the frame in {} ms, "
								 "the overlay takes {} ms, the prediction looks {} ms ahead "
								 "({} frame{}){}.",
								 age,
								 static_cast<int>(_frameInterval + 0.5),
								 static_cast<int>(_updateDuration + 0.5),
								 static_cast<int>(_overlayDuration + 0.5),
								 static_cast<int>(horizon),
								 frames,
								 frames == 1 ? "" : "s",
								 _cameraStale ? ", the camera of the frame is one frame old" : "");
				}
			}
		}
		catch (const std::exception& a_error) {
			logger::error("NPC overlay: failed to refresh the NPC markers: {}", a_error.what());
		}
		catch (...) {
			logger::error("NPC overlay: failed to refresh the NPC markers (unknown error).");
		}
	}
}
