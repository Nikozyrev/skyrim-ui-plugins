#include "ui/PrismaBridge.h"

#include "PrismaUI_API.h"
#include "damage/DamageTracker.h"
#include "npc/NpcSlots.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

using JSON = nlohmann::json;

namespace UI
{
	namespace
	{
		/// Name of the JavaScript entry point the overlay listens on.
		constexpr auto kUpdateFunction = "onDamageUpdate";

		/// Name of the JavaScript entry point the NPC head overlay listens on.
		///
		/// The slots are owned by the plugin, so the view only follows them:
		/// the slots of the NPCs that left the screen, the slots of the NPCs
		/// that entered the screen (with their ids and their names) and the
		/// slots of the NPCs that moved.
		constexpr auto kNpcSlotsHideFunction = "onNpcSlotsHide";
		constexpr auto kNpcSlotsShowFunction = "onNpcSlotsShow";
		constexpr auto kNpcSlotsMoveFunction = "onNpcSlotsMove";

		/// Name of the JavaScript entry point the names of the NPCs listen on.
		///
		/// The names are a record of the form id of an NPC and its display
		/// name. They travel on a channel of their own, because a name never
		/// changes: it is only sent when the view does not know it yet.
		constexpr auto kNpcNamesFunction = "onNpcNames";
		constexpr auto kNpcNamesClearFunction = "onNpcNamesClear";

		/// Name of the JavaScript entry point the view uses to report the size
		/// of its viewport. The positions of the markers are sent in pixels,
		/// so the plugin needs that size.
		constexpr auto kViewportFunction = "onViewportUpdate";

		/// Name of the JavaScript entry point the view listens on for the
		/// styles of the overlay.
		constexpr auto kStyleFunction = "onStyleUpdate";

		/// PrismaUI calls this plain function pointer once the view is ready.
		void OnDomReadyTrampoline(PrismaView /*a_view*/)
		{
			Bridge::Get().OnDomReady();
		}

		/// The view calls this once it registered `window.onDamageUpdate`.
		void OnViewReadyTrampoline(const char* /*a_argument*/)
		{
			Bridge::Get().OnViewReady();
		}

		/// The view calls this whenever the size of its viewport changed.
		void OnViewportUpdateTrampoline(const char* a_argument)
		{
			Bridge::Get().OnViewportUpdate(a_argument);
		}

		/// Reads an integer out of a piece of text.
		bool ParseInt(const std::string_view a_text, int& a_value)
		{
			const auto result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), a_value);

			return result.ec == std::errc{} && result.ptr == a_text.data() + a_text.size();
		}

		/// Reads the size of the viewport out of the payload of the view.
		///
		/// The payload is the width and the height of the viewport, separated
		/// by a comma: "1920,1080".
		bool ParseViewport(const char* a_argument, Npc::Viewport& a_viewport)
		{
			if (!a_argument) {
				return false;
			}

			const std::string_view payload(a_argument);

			const auto separator = payload.find(',');

			if (separator == std::string_view::npos) {
				return false;
			}

			Npc::Viewport viewport;

			if (!ParseInt(payload.substr(0, separator), viewport.width) ||
				!ParseInt(payload.substr(separator + 1), viewport.height)) {
				return false;
			}

			if (!viewport.IsValid()) {
				return false;
			}

			a_viewport = viewport;
			return true;
		}

		std::string FormIdToString(Damage::ActorId a_id)
		{
			char buffer[16];
			std::snprintf(buffer, sizeof(buffer), "%08X", a_id);
			return buffer;
		}

		JSON EventToJson(const Damage::DamageEvent& a_event)
		{
			return JSON{
				{ "attackerId", FormIdToString(a_event.attackerId) },
				{ "attackerName", a_event.attackerName },
				{ "attackerIsPlayer", a_event.attackerIsPlayer },
				{ "targetId", FormIdToString(a_event.targetId) },
				{ "targetName", a_event.targetName },
				{ "targetIsPlayer", a_event.targetIsPlayer },
				{ "damage", a_event.damage },
				{ "kind", Damage::ToString(a_event.kind) },
				{ "time", a_event.timeMs },
			};
		}

		/// The accumulated damage of every kind, so the view can draw a coloured
		/// breakdown. Kinds without damage are left out.
		JSON KindsToJson(const Damage::DamageKindTotals& a_totals)
		{
			JSON kinds = JSON::object();

			for (std::size_t i = 0; i < Damage::kDamageKindCount; ++i) {
				if (a_totals[i] > 0.0f) {
					kinds[Damage::ToString(static_cast<Damage::DamageKind>(i))] = a_totals[i];
				}
			}

			return kinds;
		}

		JSON ActorToJson(const Damage::ActorStats& a_stats)
		{
			return JSON{
				{ "id", FormIdToString(a_stats.id) },
				{ "name", a_stats.name },
				{ "isPlayer", a_stats.isPlayer },
				{ "damageDealt", a_stats.damageDealt },
				{ "damageTaken", a_stats.damageTaken },
				{ "hitsDealt", a_stats.hitsDealt },
				{ "hitsTaken", a_stats.hitsTaken },
				{ "dealtByKind", KindsToJson(a_stats.damageDealtByKind) },
				{ "takenByKind", KindsToJson(a_stats.damageTakenByKind) },
			};
		}

		JSON ActorsToJson()
		{
			JSON actors = JSON::array();

			for (const auto& stats : Damage::Tracker::Get().Actors()) {
				actors.push_back(ActorToJson(stats));
			}

			return actors;
		}

		/// Builds a frame of the slots that have to be freed.
		///
		/// A frame is a plain list of the numbers of the slots, separated by
		/// semicolons.
		std::string BuildHideFrame(const std::vector<std::uint8_t>& a_slots)
		{
			std::string frame;

			for (const auto slot : a_slots) {
				if (!frame.empty()) {
					frame.push_back(';');
				}

				frame += std::to_string(slot);
			}

			return frame;
		}

		/// Builds a frame of the NPCs that entered the screen.
		///
		/// A frame is a list of the tuples "slot:id", separated by
		/// semicolons. The slot is the number of the slot of the overlay and
		/// the id is the form id of the NPC. The name of an NPC travels on a
		/// channel of its own, so it never needs to be escaped here.
		std::string BuildShowFrame(const std::vector<Npc::SlotEntry>& a_entries)
		{
			std::string frame;

			for (const auto& entry : a_entries) {
				if (!frame.empty()) {
					frame.push_back(';');
				}

				frame += std::to_string(entry.index);
				frame.push_back(':');

				// The form id is a number in its hexadecimal notation, so it
				// never contains a separator.
				frame += FormIdToString(entry.id);
			}

			return frame;
		}

		/// Builds a frame of the positions of the slots.
		///
		/// A frame is a list of the tuples "slot:x,y,scale", separated by
		/// semicolons. The slot is the number of the slot of the overlay and
		/// the numbers are the plain CSS values of the marker, so a frame only
		/// carries the slots that really moved.
		std::string BuildFrame(const std::vector<Npc::ViewMarker>& a_values,
							   const std::vector<std::uint8_t>& a_slots)
		{
			std::string frame;

			for (std::size_t i = 0; i < a_slots.size(); ++i) {
				if (!frame.empty()) {
					frame.push_back(';');
				}

				const auto& value = a_values[i];

				frame += std::to_string(a_slots[i]);
				frame.push_back(':');
				frame += std::to_string(value.x);
				frame.push_back(',');
				frame += std::to_string(value.y);
				frame.push_back(',');

				// The scale is a float, so it is written with a fixed number
				// of decimals: the view parses it as a plain number.
				//
				// Two decimals are enough: the scale is the size of the font
				// of the number divided by the base size, and both of them
				// are whole numbers, so the scale is a fraction with a step
				// of 1/11. The error of the rounding stays below a hundredth
				// of a pixel and two bytes of every marker are saved.
				char buffer[32];
				std::snprintf(buffer, sizeof(buffer), "%.2f", value.scale);

				frame += buffer;
			}

			return frame;
		}
	}

	Bridge& Bridge::Get()
	{
		static Bridge singleton;
		return singleton;
	}

	bool Bridge::AcquireApi()
	{
		if (_api) {
			return true;
		}

		auto* api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();

		if (!api) {
			if (!_apiMissingLogged.exchange(true)) {
				logger::warn("PrismaUI was not found - the damage overlay stays disabled. Is PrismaUI.dll installed?");
			}
			return false;
		}

		_api = api;
		logger::info("PrismaUI API acquired.");
		return true;
	}

	bool Bridge::CreateView(const char* a_viewPath)
	{
		if (_viewCreated.load()) {
			return _view.load() != 0;
		}

		if (!AcquireApi()) {
			return false;
		}

		// PrismaUI throws when its renderer cannot be initialized. A failing view
		// must never take the game down.
		PrismaView view = 0;
		try {
			view = _api->CreateView(a_viewPath, &OnDomReadyTrampoline);
		}
		catch (const std::exception& a_error) {
			logger::error("PrismaUI failed to create the view '{}': {}", a_viewPath, a_error.what());
			_viewCreated.store(true);
			return false;
		}
		catch (...) {
			logger::error("PrismaUI failed to create the view '{}' (unknown error).", a_viewPath);
			_viewCreated.store(true);
			return false;
		}

		_viewCreated.store(true);

		if (!view || !_api->IsValid(view)) {
			logger::error("PrismaUI could not create the view '{}'. Is the view deployed to 'PrismaUI/views/{}'?",
						  a_viewPath, a_viewPath);
			_view.store(0);
			return false;
		}

		_view.store(view);

		// The overlay has to be visible without stealing the mouse focus.
		_api->Show(view);

		// The view announces itself as soon as it registered its handler. The
		// plugin answers with the complete state, which makes the overlay work
		// even when the first DOM ready push happened too early.
		_api->RegisterJSListener(view, "onViewReady", &OnViewReadyTrampoline);

		// The positions of the markers are sent in pixels, so the view has to
		// report the size of its viewport.
		_api->RegisterJSListener(view, kViewportFunction, &OnViewportUpdateTrampoline);

		logger::info("PrismaUI view '{}' created.", a_viewPath);
		return true;
	}

	void Bridge::OnDomReady()
	{
		_domReady.store(true);
		logger::info("PrismaUI view is ready, pushing the damage overlay state.");
		ResetNpcState();
		SendStyle();
		SendSnapshot();
	}

	void Bridge::OnViewReady()
	{
		// The view registered its handlers, so the JavaScript functions the
		// plugin calls do exist now.
		_viewReady.store(true);

		logger::info("PrismaUI view announced itself, pushing the damage overlay state.");
		ResetNpcState();
		SendStyle();
		SendSnapshot();
	}

	void Bridge::SendStyle()
	{
		if (!_api) {
			return;
		}

		// The base size of the font of the numbers is a constant of the
		// plugin, so the stylesheet does not have to know it: the view sets
		// the value as a global custom property of the document.
		//
		// The number of the slots is sent as well: the view draws one
		// element per slot, so it has to know how many slots to expect.
		JSON payload{
			{ "type", "style" },
			{ "baseFontSize", Npc::kBaseFontSize },
			{ "slotCount", Npc::kMaxSlots },
		};

		Send(kStyleFunction, payload.dump());
	}

	void Bridge::ResetNpcState()
	{
		std::lock_guard<std::mutex> lock(_npcMutex);

		// The view starts with an empty overlay, so the ids and the names of
		// every NPC have to be sent again even when they did not change since
		// the last update.
		_npcStateStale = true;
		_slotSent.reset();
	}

	void Bridge::OnViewportUpdate(const char* a_argument)
	{
		Npc::Viewport viewport;

		if (!ParseViewport(a_argument, viewport)) {
			logger::warn("The view reported an unusable viewport size ('{}').", a_argument ? a_argument : "");
			return;
		}

		std::lock_guard<std::mutex> lock(_npcMutex);

		if (_viewport.width == viewport.width && _viewport.height == viewport.height) {
			return;
		}

		const bool wasKnown = _viewport.IsValid();

		_viewport = viewport;

		// The positions are in pixels, so every marker moved to another pixel
		// when the size of the viewport changed.
		_slotSent.reset();

		// The size changes on every pixel while the window of the overlay is
		// dragged, so only the first one is worth a line in the log.
		if (wasKnown) {
			logger::debug("The viewport of the view changed to {}x{} pixels.", viewport.width, viewport.height);
		}
		else {
			logger::info("The view reported a viewport of {}x{} pixels.", viewport.width, viewport.height);
		}
	}

	bool Bridge::IsReady() const
	{
		return _api != nullptr && _view.load() != 0 && _domReady.load();
	}

	bool Bridge::IsViewCreated() const
	{
		return _view.load() != 0;
	}

	void Bridge::OnDamageRecorded(const Damage::DamageEvent& a_event)
	{
		if (!_api) {
			return;
		}

		JSON payload{
			{ "type", "event" },
			{ "actors", ActorsToJson() },
			{ "event", EventToJson(a_event) },
		};

		Send(kUpdateFunction, payload.dump());
	}

	void Bridge::SendSnapshot()
	{
		if (!_api) {
			return;
		}

		JSON events = JSON::array();
		for (const auto& event : Damage::Tracker::Get().RecentEvents()) {
			events.push_back(EventToJson(event));
		}

		JSON payload{
			{ "type", "snapshot" },
			{ "actors", ActorsToJson() },
			{ "events", events },
		};

		Send(kUpdateFunction, payload.dump());
	}

	void Bridge::SendNpcSlots(const Npc::SlotDelta& a_delta)
	{
		if (!_api || !_viewReady.load(std::memory_order_acquire)) {
			return;
		}

		std::lock_guard<std::mutex> lock(_npcMutex);

		if (!_viewport.IsValid()) {
			return;
		}

		// The slots of the NPCs that left the screen. The slots that the
		// view did not get yet are sent again, so a lost message cannot
		// leave a marker of an NPC that is long gone on the screen.
		_scratchHide.clear();
		_scratchHide.insert(_scratchHide.end(), _pendingHide.begin(), _pendingHide.end());

		for (const auto slot : a_delta.hidden) {
			if (std::find(_scratchHide.begin(), _scratchHide.end(), slot) == _scratchHide.end()) {
				_scratchHide.push_back(slot);
			}
		}

		if (!_scratchHide.empty()) {
			if (Send(kNpcSlotsHideFunction, BuildHideFrame(_scratchHide))) {
				for (const auto slot : _scratchHide) {
					_slotSent.reset(slot);
				}

				_pendingHide.clear();
			}
			else {
				// The view did not get the message, so the slots are hidden
				// with one of the next frames.
				_pendingHide = _scratchHide;
			}
		}

		// The NPCs that entered the screen. Their ids travel with the numbers
		// of their slots, so the view can look them up by it. Their names
		// travel on a channel of their own.
		if (_npcStateStale) {
			// The view lost its state, so every NPC is sent again.
			_knownNames.clear();

			if (!SendShowAndNames(a_delta.occupied)) {
				return;
			}

			_npcStateStale = false;
		}
		else if (!a_delta.shown.empty()) {
			if (!SendShowAndNames(a_delta.shown)) {
				// The view does not know the NPCs, so the whole state is sent
				// again with the next frame.
				_npcStateStale = true;
				return;
			}
		}

		// The slots of the NPCs that moved.
		_scratchSlots.clear();
		_scratchValues.clear();

		for (const auto& entry : a_delta.occupied) {
			const auto value = Npc::BuildViewMarker(entry.marker, _viewport);

			if (_slotSent.test(entry.index) && _slotValues[entry.index] == value) {
				continue;
			}

			_scratchSlots.push_back(entry.index);
			_scratchValues.push_back(value);
		}

		if (_scratchSlots.empty()) {
			return;
		}

		if (!Send(kNpcSlotsMoveFunction, BuildFrame(_scratchValues, _scratchSlots))) {
			// The positions are not known by the view, so they are sent again
			// with the next frame.
			return;
		}

		for (std::size_t i = 0; i < _scratchSlots.size(); ++i) {
			_slotValues[_scratchSlots[i]] = _scratchValues[i];
			_slotSent.set(_scratchSlots[i]);
		}
	}

	bool Bridge::SendShowAndNames(const std::vector<Npc::SlotEntry>& a_entries)
	{
		if (a_entries.empty()) {
			return true;
		}

		// The names of the NPCs that the view does not know yet. A name never
		// changes, so it is only sent once per NPC.
		JSON names = JSON::object();

		std::vector<Damage::ActorId> sent;

		for (const auto& entry : a_entries) {
			if (!_knownNames.insert(entry.id).second) {
				continue;
			}

			names[FormIdToString(entry.id)] = entry.name;
			sent.push_back(entry.id);
		}

		if (!names.empty()) {
			logger::info("Sending names: {}", names.dump());
			if (!Send(kNpcNamesFunction, names.dump())) {
				// The view does not know the names, so they are sent again
				// with the next frame.
				for (const auto id : sent) {
					_knownNames.erase(id);
				}

				return false;
			}
		}
		
		std::string showFrame = BuildShowFrame(a_entries);
		logger::info("Sending show slots: {}", showFrame);
		return Send(kNpcSlotsShowFunction, showFrame);
	}

	void Bridge::ResetNpcCache() {
		std::lock_guard<std::mutex> lock(_npcMutex);

		// The world is replaced, so every NPC is gone and every slot is free.
		Npc::SlotPool::Get().Reset();

		// The view has to drop the NPCs it knows.
		for (std::size_t i = 0; i < Npc::kMaxSlots; ++i) {
			if (!_slotSent.test(i)) {
				continue;
			}

			const auto slot = static_cast<std::uint8_t>(i);

			if (std::find(_pendingHide.begin(), _pendingHide.end(), slot) == _pendingHide.end()) {
				_pendingHide.push_back(slot);
			}
		}

		_slotSent.reset();

		// The names of the old world are of no use to the new one.
		_knownNames.clear();
		Send(kNpcNamesClearFunction, "");

		// There is nothing to show yet, so the state is not stale: the NPCs
		// of the new world are sent as soon as they appear.
		_npcStateStale = false;
	}

	bool Bridge::Send(const char* a_function, const std::string& a_payload)
	{
		std::lock_guard<std::mutex> lock(_sendMutex);

		const auto view = _view.load();

		if (!_api || !view || !_domReady.load()) {
			return false;
		}

		if (!_api->IsValid(view)) {
			logger::warn("The PrismaUI view became invalid, the damage overlay is disabled.");
			_domReady.store(false);
			return false;
		}

		// InteropCall hands the payload over as a plain string, so the payload can
		// never be interpreted as JavaScript code. The view parses it as JSON.
		_api->InteropCall(view, a_function, a_payload.c_str());
		return true;
	}
}
