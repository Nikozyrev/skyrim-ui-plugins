#pragma once

#include "damage/DamageTypes.h"
#include "npc/NpcTypes.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Npc
{
	/// The number of the slots of the overlay.
	///
	/// The number is a part of the protocol: the view draws exactly one
	/// element per slot, so the plugin tells it how many slots it has to
	/// expect.
	constexpr std::size_t kMaxSlots = 50;

	/// One NPC that occupies a slot of the overlay.
	struct SlotEntry
	{
		/// The number of the slot.
		std::uint8_t index = 0;

		/// The form id of the NPC.
		Damage::ActorId id = 0;

		/// The display name of the NPC.
		std::string name;

		/// The last known position of the NPC.
		Marker marker;
	};

	/// The changes of the slots of a single frame.
	struct SlotDelta
	{
		/// The slots of the NPCs that left the screen.
		std::vector<std::uint8_t> hidden;

		/// The NPCs that entered the screen.
		std::vector<SlotEntry> shown;

		/// Every NPC that is on the screen, in the order of the numbers of
		/// their slots.
		std::vector<SlotEntry> occupied;
	};

	/// The slots of the NPCs that are visible on the screen.
	///
	/// The pool owns the slots: it hands a free one to every NPC that enters
	/// the screen and takes it back when the NPC leaves. The view draws one
	/// element per slot, so the number of the slot is the only thing that
	/// travels with the markers.
	///
	/// The pool is safe to use from any thread.
	class SlotPool
	{
	public:
		static SlotPool& Get();

		/// Assigns the slots of the NPCs that are visible now.
		///
		/// The NPCs that are still on the screen keep their slot, the ones
		/// that left give it back and the new ones take a free one. An NPC
		/// that does not get a slot is not shown at all.
		SlotDelta Update(const std::vector<Marker>& a_markers);

		/// Forgets every NPC and frees every slot.
		void Reset();

		/// True when the slot is in use.
		[[nodiscard]] bool IsOccupied(std::size_t a_index) const;

	private:
		SlotPool();

		/// The state of a single NPC of the pool.
		struct SlotData
		{
			/// The number of the slot of the NPC.
			std::uint8_t slot = 0;

			/// True while the NPC was seen during the last update.
			bool visible = false;
		};

		/// The free slots. The last one is handed out first.
		std::vector<std::uint8_t> _free;

		/// The slots that are in use.
		std::bitset<kMaxSlots> _occupied;

		/// The NPCs of the pool, keyed by their form id.
		std::unordered_map<Damage::ActorId, SlotData> _actors;

		/// The occupied slots, indexed by the number of the slot.
		std::vector<SlotEntry> _slots;

		/// Guards the state of the pool, so that the game thread and the
		/// thread of the UI never touch it at the same time.
		mutable std::mutex _mutex;
	};
}
