#pragma once

#include "PrismaUI_API.h"
#include "damage/DamageTypes.h"
#include "npc/NpcSlots.h"
#include "npc/NpcTypes.h"

#include <array>
#include <atomic>
#include <bitset>
#include <mutex>
#include <string>
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
		///   * the slots of the NPCs that entered the screen, together with
		///     their ids and their names;
		///   * the slots of the NPCs that moved, together with their new
		///     positions.
		///
		/// The positions are sent in pixels, so nothing is sent before the
		/// view reported the size of its viewport.
		void SendNpcSlots(const Npc::SlotDelta& a_delta);

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

		/// The positions of the slots that the view knows.
		///
		/// The positions are the plain CSS values of a marker, so a slot is
		/// only sent again when it really moved.
		std::array<Npc::ViewMarker, Npc::kMaxSlots> _slotValues{};

		/// The slots whose positions the view knows.
		std::bitset<Npc::kMaxSlots> _slotSent;

		/// The NPCs whose names the view knows.
		///
		/// A name never changes, so it is only sent once per NPC.
		std::unordered_set<Damage::ActorId> _knownNames;

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
	};
}
