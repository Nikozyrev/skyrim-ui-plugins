#pragma once

#include "PrismaUI_API.h"
#include "damage/DamageTypes.h"
#include "npc/NpcHealth.h"
#include "npc/NpcSlots.h"
#include "npc/NpcTypes.h"

#include <array>
#include <atomic>
#include <bitset>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace UI
{
	/// Thin, failure tolerant wrapper around the PrismaUI modder API.
	///
	/// Responsibilities:
	///   * acquire the PrismaUI API (it may not be loaded yet, so acquisition is
	///     retried until it succeeds);
	///   * own the view and wait for its DOM to become ready;
	///   * serialize the damage state and push it to the view.
	///
	/// Every method is safe to call from any thread and never throws. When the
	/// UI is unavailable the bridge silently drops updates instead of crashing
	/// the game - the damage tracking keeps working in the background.
	class Bridge
	{
	private:
		/// The state of a single slot of the overlay.
		///
		/// The bridge knows the measured positions of the markers and how
		/// fast they move, so it can put every marker where it will be when
		/// the game frame it belongs to is on the screen. The view only
		/// writes the values it gets into the element of the slot.
		struct SlotState
		{
			/// The last measured position of the marker.
			Npc::ViewMarker measured{};

			/// The moment the last measurement was made, in milliseconds
			/// since the epoch. It is zero as long as the slot was never
			/// measured.
			std::int64_t measuredTime = 0;

			/// The length of the movement of the last frame, in pixels.
			///
			/// It is the movement of the marker between the last two
			/// measurements. It says whether the movement is still going on.
			float moved = 0.0f;

			/// The position that the view knows.
			Npc::ViewMarker sent{};

			/// True when the view knows the position of the slot.
			bool known = false;
		};

	public:
		static Bridge& Get();

		/// Tries to acquire the PrismaUI API. Idempotent.
		/// @return true when the API is available.
		bool AcquireApi();

		/// Creates the view. Idempotent, a second call is a no-op.
		/// @param a_viewPath path of the view relative to the PrismaUI views folder
		/// @return true when the view exists
		bool CreateView(const char* a_viewPath);

		/// Called by PrismaUI once the DOM of the view became ready.
		void OnDomReady();

		/// Called by the view once it registered its own handler. The plugin uses
		/// it to push the complete state, which closes the race between the view
		/// being created and the view script being evaluated.
		void OnViewReady();

		/// Called by the view whenever the size of its viewport changed.
		///
		/// The positions of the markers are sent in pixels, so the plugin has
		/// to know the size of the viewport to convert them. The payload is
		/// the width and the height of the viewport, separated by a comma.
		void OnViewportUpdate(const char* a_argument);

		/// True when the API is available, the view exists and its DOM is ready.
		[[nodiscard]] bool IsReady() const;

		/// True when the view was created successfully.
		[[nodiscard]] bool IsViewCreated() const;

		/// Called by the view whenever it drew a frame of the markers.
		///
		/// The argument is the number of milliseconds that the positions of
		/// the frame needed until they were on the screen. It is the delay of
		/// the whole path, so it says how much of it the view has to
		/// compensate.
		void OnNpcSync(const char* a_argument);

		/// The delay of the last frame, in milliseconds.
		///
		/// It is negative when the view did not report a frame yet.
		[[nodiscard]] std::int64_t SyncAge() const { return _syncAge.load(); }

		/// The horizon of the last prediction, in milliseconds.
		///
		/// It is the time that the markers were moved forward, so it says
		/// how much of the delay of the whole path was compensated.
		[[nodiscard]] std::int64_t PredictionHorizon() const { return _predictionHorizon.load(); }

		/// The number of frames of the game that the last prediction
		/// compensated.
		[[nodiscard]] int PredictionFrames() const { return _predictionFrames.load(); }

		/// Sends a newly recorded damage event together with the current aggregates.
		void OnDamageRecorded(const Damage::DamageEvent& a_event);

		/// Sends the complete state (aggregates + recent events) to the view.
		void SendSnapshot();

		/// Sends the styles of the overlay to the view.
		///
		/// The base size of the font of the numbers lives in the plugin, so
		/// the view does not have to know it: it sets the value as a global
		/// custom property of the document.
		void SendStyle();

		/// Sends the changes of the slots of the NPCs that are visible on the
		/// screen.
		///
		/// The slots are owned by the plugin: the pool hands a free one to
		/// every NPC that enters the screen and takes it back when the NPC
		/// leaves. The payload therefore carries the numbers of the slots
		/// instead of the ids of the NPCs:
		///   * the slots of the NPCs that left the screen;
		///   * the slots of the NPCs that moved, together with their new
		///     positions;
		///   * the slots of the NPCs that entered the screen, together with
		///     their ids and their names.
		///
		/// The positions are sent in pixels, so nothing is sent before the
		/// view reported the size of its viewport.
		///
		/// The positions are the ones the markers will have when the game
		/// frame they belong to is on the screen: the bridge knows how fast
		/// the markers move, so it moves them forward by the length of the
		/// frame that the view is behind.
		///
		/// The view does not have to calculate anything on its own: it only
		/// writes the numbers it gets into the element of the slot.
		///
		/// @param a_delta the changes of the slots
		/// @param a_time the moment the positions were measured, in
		///               milliseconds since the epoch
		/// @param a_frameInterval the length of a frame of the game, in
		///                       milliseconds
		/// @param a_leadFrames the number of frames that the camera of the
		///                     measurement was already old
		void SendNpcSlots(const Npc::SlotDelta& a_delta,
						  std::int64_t a_time,
						  int a_frameInterval,
						  int a_leadFrames);

		/// Forgets the NPCs of the view.
		///
		/// The world is replaced, so every slot is freed and the view is told
		/// to drop the NPCs it knows.
		void ResetNpcCache();

	private:
		Bridge() = default;

		/// Serializes and pushes a payload to a JavaScript function of the
		/// view. Returns false when the UI is not ready.
		bool Send(const char* a_function, const std::string& a_payload);

		/// Sends the NPCs of the given slots and the names they need.
		///
		/// The ids of the NPCs travel with the numbers of their slots, so the
		/// view can look them up. The names travel on a channel of their own:
		/// a name never changes, so it is only sent when the view does not
		/// know it yet.
		///
		/// The names are sent first, so a marker is never drawn without the
		/// name of its NPC.
		bool SendShowAndNames(const std::vector<Npc::SlotEntry>& a_entries);

		/// Forgets the NPC state that was sent to the view.
		///
		/// The view starts with an empty overlay, so the ids and the names of
		/// every NPC have to be sent again after the view announced itself.
		void ResetNpcState();

		/// Moves a marker forward to the place it will have when the frame it
		/// belongs to is on the screen.
		///
		/// The view of PrismaUI is drawn in the frame after the one that was
		/// measured and it is composited with the frame after that one, so
		/// every marker is a whole frame behind its NPC. The movement of a
		/// marker is a straight line: it is the movement of the camera on the
		/// screen, and a camera that turns at a constant speed moves every
		/// point of the screen at a constant speed too.
		///
		/// The prediction is repeated as long as the movement goes on. When
		/// the camera slows down, the movement of the last two frames says so
		/// and the prediction becomes smaller with it, so a camera that stops
		/// does not make the markers run away.
		///
		/// @param a_state the state of the slot
		/// @param a_measured the position that was just measured
		/// @param a_time the moment the position was measured, in
		///               milliseconds since the epoch
		/// @param a_horizon the time that the marker has to be moved forward,
		///                  in milliseconds
		/// @return the position of the marker on the screen
		[[nodiscard]] static Npc::ViewMarker Predict(const SlotState& a_state,
													 const Npc::ViewMarker& a_measured,
													 std::int64_t a_time,
													 double a_horizon);

		PRISMA_UI_API::IVPrismaUI1* _api = nullptr;

		/// The view id is written on the game thread and read from the PrismaUI
		/// thread, therefore it has to be atomic.
		std::atomic<std::uint64_t> _view{ 0 };

		std::atomic_bool _viewCreated{ false };
		std::atomic_bool _domReady{ false };
		std::atomic_bool _viewReady{ false };
		std::atomic_bool _apiMissingLogged{ false };

		/// Guards the PrismaUI calls so that two threads never talk to the view
		/// at the same time.
		std::mutex _sendMutex;

		/// Guards the NPC state below, so that the game thread and the
		/// PrismaUI thread never touch it at the same time.
		std::mutex _npcMutex;

		/// The state of the slots.
		std::array<SlotState, Npc::kMaxSlots> _slots{};

		/// The NPCs whose names the view knows.
		///
		/// A name never changes, so it is only sent once per NPC.
		std::unordered_set<Damage::ActorId> _knownNames;

		/// The health of the NPCs that the view knows.
		///
		/// The health is only sent again when it really changed, so an NPC
		/// that is at full health costs nothing.
		std::unordered_map<Damage::ActorId, Npc::Health> _sentHealth;

		/// True when the view lost the state of the slots.
		///
		/// The view starts with an empty overlay, so the ids and the names of
		/// every NPC have to be sent again after the view announced itself.
		bool _npcStateStale = true;

		// Переиспользуемые буферы, чтобы не выделять память каждый кадр
		std::vector<std::uint8_t> _scratchHide;
		std::vector<std::uint8_t> _scratchSlots;
		std::vector<Npc::ViewMarker> _scratchValues;

		/// The slots that the view has to hide, but did not get yet.
		std::vector<std::uint8_t> _pendingHide;

		/// The size of the viewport of the view, in pixels.
		///
		/// The view reports it, because the window of the overlay is not the
		/// screen of the game. The size is zero as long as the view did not
		/// report it, and the positions cannot be sent before that.
		Npc::Viewport _viewport;

		/// The delay of the last frame of the markers, in milliseconds.
		///
		/// The view reports it, because only the view knows how long the
		/// positions of a frame needed until they were on the screen. It is
		/// written on the thread of the view and read on the game thread.
		std::atomic<std::int64_t> _syncAge{ -1 };

		/// The horizon of the last prediction, in milliseconds.
		///
		/// It is written on the game thread and read on the game thread, but
		/// it is atomic, so the log can read it without a lock.
		std::atomic<std::int64_t> _predictionHorizon{ 0 };

		/// The number of frames that the last prediction compensated.
		std::atomic<int> _predictionFrames{ 0 };
	};
}
