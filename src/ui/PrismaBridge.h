#pragma once

#include "PrismaUI_API.h"
#include "damage/DamageTypes.h"
#include "npc/NpcTypes.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
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

		/// True when the API is available, the view exists and its DOM is ready.
		[[nodiscard]] bool IsReady() const;

		/// True when the view was created successfully.
		[[nodiscard]] bool IsViewCreated() const;

		/// Sends a newly recorded damage event together with the current aggregates.
		void OnDamageRecorded(const Damage::DamageEvent& a_event);

		/// Sends the complete state (aggregates + recent events) to the view.
		void SendSnapshot();

		/// Sends the heads of the NPCs that are currently visible on the screen.
		///
		/// The markers are keyed by the form id of the NPC. The payload
		/// replaces the markers of the previous update completely.
		///
		/// The bridge takes care of the traffic itself: the table of the
		/// visible NPCs (their ids and their names) is only sent when it
		/// really changed, and a frame of the positions is only sent when a
		/// marker really moved.
		void SendNpcMarkers(const std::vector<Npc::Marker>& a_markers);

	private:
		Bridge() = default;

		/// Serializes and pushes a payload to a JavaScript function of the
		/// view. Returns false when the UI is not ready.
		bool Send(const char* a_function, const std::string& a_payload);

		/// Forgets the NPC state that was sent to the view.
		///
		/// The view starts with an empty list of markers, so the table and the
		/// positions have to be sent again after the view announced itself.
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

		/// The ids of the visible NPCs that the view knows.
		///
		/// The ids are in the order of the indexes that the frames of the
		/// positions use, so the order matters as well as the content.
		std::vector<Damage::ActorId> _npcIds;

		/// The positions of the visible NPCs that the view knows.
		std::unordered_map<Damage::ActorId, Npc::QuantizedMarker> _npcFrame;
	};
}
