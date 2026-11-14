#include "npc/NpcSlots.h"

#include <utility>

namespace Npc
{
	namespace
	{
		/// The position of an NPC, without its name.
		///
		/// The name of a marker is a view into the cache of the tracker, so
		/// the pool does not keep it: the slot owns a copy of the name
		/// instead.
		Marker PositionOf(const Marker& a_marker)
		{
			Marker marker = a_marker;
			marker.name = {};
			return marker;
		}
	}

	SlotPool::SlotPool()
	{
		Reset();
	}

	SlotPool& SlotPool::Get()
	{
		static SlotPool singleton;
		return singleton;
	}

	void SlotPool::Reset()
	{
		std::lock_guard<std::mutex> lock(_mutex);

		_actors.clear();
		_occupied.reset();
		_slots.assign(kMaxSlots, SlotEntry{});

		// The free slots are a stack: the lowest numbers are handed out
		// first, so the slots of the view are filled from the bottom up.
		_free.clear();
		_free.reserve(kMaxSlots);

		for (std::size_t i = kMaxSlots; i > 0; --i) {
			_free.push_back(static_cast<std::uint8_t>(i - 1));
		}
	}

	bool SlotPool::IsOccupied(std::size_t a_index) const
	{
		std::lock_guard<std::mutex> lock(_mutex);

		return a_index < kMaxSlots && _occupied.test(a_index);
	}

	SlotDelta SlotPool::Update(const std::vector<Marker>& a_markers)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		SlotDelta delta;

		// Nobody is on the screen until the markers of the frame say so.
		for (auto& [id, data] : _actors) {
			data.visible = false;
		}

		for (const auto& marker : a_markers) {
			const auto it = _actors.find(marker.id);

			if (it == _actors.end()) {
				// The NPC entered the screen, so it takes a free slot. When
				// every slot is taken, the NPC is not shown at all.
				if (_free.empty()) {
					continue;
				}

				const auto slot = _free.back();
				_free.pop_back();
				_occupied.set(slot);

				SlotEntry entry;
				entry.index = slot;
				entry.id = marker.id;
				entry.name = std::string(marker.name);
				entry.marker = PositionOf(marker);

				_slots[slot] = entry;

				SlotData data;
				data.slot = slot;
				data.visible = true;

				_actors.emplace(marker.id, data);

				delta.shown.push_back(entry);
				continue;
			}

			// The NPC is still on the screen, so it keeps its slot.
			it->second.visible = true;
			_slots[it->second.slot].marker = PositionOf(marker);
		}

		// The NPCs that left the screen give their slots back.
		for (auto it = _actors.begin(); it != _actors.end();) {
			if (it->second.visible) {
				++it;
				continue;
			}

			const auto slot = it->second.slot;

			_occupied.reset(slot);
			_slots[slot] = SlotEntry{};
			_free.push_back(slot);

			delta.hidden.push_back(slot);

			it = _actors.erase(it);
		}

		// The state of every slot is sent along, so the view knows where the
		// NPCs are without asking for them one by one.
		delta.occupied.reserve(_actors.size());

		for (const auto& entry : _slots) {
			if (entry.id != 0) {
				delta.occupied.push_back(entry);
			}
		}

		return delta;
	}
}
