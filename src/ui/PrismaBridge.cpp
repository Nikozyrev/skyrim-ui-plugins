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

		/// Name of the JavaScript entry point the health of the NPCs listens on.
		///
		/// The health is a record of the form id of an NPC and its health. It
		/// travels on a channel of its own, because it changes all the time:
		/// it is sent when the NPC enters the screen and every time it takes
		/// damage.
		constexpr auto kNpcHealthFunction = "onNpcHealth";

		/// Name of the JavaScript entry point the view uses to report the size
		/// of its viewport. The positions of the markers are sent in pixels,
		/// so the plugin needs that size.
		constexpr auto kViewportFunction = "onViewportUpdate";

		/// Name of the JavaScript entry point the view uses to report how old
		/// the positions of the last frame are.
		///
		/// The number tells how long the positions of a frame need until they
		/// are on the screen. It is the delay of the whole path, so it says
		/// how much of it the view has to compensate.
		constexpr auto kNpcSyncFunction = "onNpcSync";

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

		/// The view calls this whenever it drew a frame of the markers.
		///
		/// The argument is the number of milliseconds that the positions of
		/// the frame needed until they were on the screen.
		void OnNpcSyncTrampoline(const char* a_argument)
		{
			Bridge::Get().OnNpcSync(a_argument);
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

		/// The bigger of the two numbers.
		///
		/// The helpers of the standard library cannot be used here: the
		/// headers of windows define `min` and `max` as macros, so they
		/// break the name of the helper.
		template <typename T>
		[[nodiscard]] constexpr T Larger(T a_left, T a_right)
		{
			return a_left > a_right ? a_left : a_right;
		}

		/// The smaller of the two numbers.
		template <typename T>
		[[nodiscard]] constexpr T Smaller(T a_left, T a_right)
		{
			return a_left < a_right ? a_left : a_right;
		}

		/// The share of the movement of a marker that the prediction repeats.
		///
		/// A marker that moves at a constant speed is put exactly where it
		/// will be when the frame it belongs to is on the screen. The factor
		/// leaves a little room: the movement of a camera is never constant,
		/// so a prediction that repeats the whole movement overshoots when the
		/// camera stops.
		constexpr float kPredictionGain = 0.85f;

		/// The share of the previous movement that the current one has to
		/// reach, so the movement is still seen as going on.
		///
		/// When the camera slows down, the share gets smaller and the
		/// prediction gets smaller with it, so a marker does not run away
		/// when the camera stops.
		constexpr float kSustainedRatio = 0.5f;

		/// The smallest movement that is worth a prediction, in pixels.
		constexpr float kMinMovement = 0.2f;

		/// The longest time a prediction may look ahead, in milliseconds.
		constexpr float kMaxPredictionMs = 250.0f;

		/// The longest distance a prediction may move a marker, in pixels.
		constexpr float kMaxPredictionPx = 400.0f;

		/// Writes a number into a frame.
		///
		/// The offsets of a marker are written with a tenth of a pixel and
		/// the scale of its font with a hundredth. The view needs the tenth
		/// of a pixel to tell how fast a marker moves: a marker that is put
		/// on a whole pixel cannot be moved forward in time.
		void AppendNumber(std::string& a_frame, float a_value, int a_decimals)
		{
			char buffer[32];

			const int written = std::snprintf(buffer, sizeof(buffer), "%.*f", a_decimals, a_value);

			if (written > 0) {
				a_frame.append(buffer, static_cast<std::size_t>(written));
			}
		}

		/// Builds a frame of the positions of the slots.
		///
		/// A frame is a list of the tuples "slot:x,y,scale", separated by
		/// semicolons. The slot is the number of the slot of the overlay and
		/// the numbers are the plain CSS values of the marker, so a frame only
		/// carries the slots that really moved.
		///
		/// The frame starts with the time of the frame. The view compares it
		/// with its own clock, so it knows how old the positions of the frame
		/// are and can report it to the plugin.
		std::string BuildFrame(const std::vector<Npc::ViewMarker>& a_values,
							   const std::vector<std::uint8_t>& a_slots,
							   std::int64_t a_time)
		{
			std::string frame;

			frame += "T:";
			frame += std::to_string(a_time);

			for (std::size_t i = 0; i < a_slots.size(); ++i) {
				frame.push_back(';');

				const auto& value = a_values[i];

				frame += std::to_string(a_slots[i]);
				frame.push_back(':');

				AppendNumber(frame, value.x, 1);
				frame.push_back(',');
				AppendNumber(frame, value.y, 1);
				frame.push_back(',');
				AppendNumber(frame, value.scale, 2);
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

		// The view reports how old the positions of a frame are when it drew
		// it, so the delay of the whole path is visible in the log.
		_api->RegisterJSListener(view, kNpcSyncFunction, &OnNpcSyncTrampoline);

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

		for (auto& slot : _slots) {
			slot = SlotState{};
		}

		// The health of the NPCs is lost with it, so it is sent again as well.
		_sentHealth.clear();
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
		// when the size of the viewport changed. The measurements are dropped
		// as well: they belong to the old size of the viewport.
		for (auto& slot : _slots) {
			slot = SlotState{};
		}

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

	void Bridge::OnNpcSync(const char* a_argument)
	{
		if (!a_argument) {
			return;
		}

		const std::string_view payload(a_argument);

		int age = 0;

		if (!ParseInt(payload, age) || age < 0) {
			return;
		}

		// The view runs on its own thread, so the number is written in an
		// atomic and read by the game thread.
		_syncAge.store(age);
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

	Npc::ViewMarker Bridge::Predict(const SlotState& a_state,
									const Npc::ViewMarker& a_measured,
									std::int64_t a_time,
									double a_horizon)
	{
		// The slot was never measured, so there is no movement to follow.
		if (a_state.measuredTime == 0 || a_horizon <= 0.0) {
			return a_measured;
		}

		// The time between the last measurement and this one. It is the
		// length of a frame of the game, so it is also the time in which
		// the marker moved the distance it moved.
		const auto dt = static_cast<double>(a_time - a_state.measuredTime);

		// Two measurements that are too close or too far apart do not tell
		// how fast the marker moves.
		if (dt <= 0.0 || dt > kMaxPredictionMs) {
			return a_measured;
		}

		const auto dx = a_measured.x - a_state.measured.x;
		const auto dy = a_measured.y - a_state.measured.y;

		if (std::abs(dx) < kMinMovement && std::abs(dy) < kMinMovement) {
			return a_measured;
		}

		// How much of the movement is still going on. When the camera slows
		// down, the movement of the last frame says so, and the prediction
		// becomes smaller with it.
		auto share = 1.0;

		if (a_state.moved > 0.0f) {
			const auto moved = std::sqrt(dx * dx + dy * dy);
			const auto ratio = moved / Larger(a_state.moved, kMinMovement);

			share = Smaller(1.0, static_cast<double>(ratio / kSustainedRatio));
		}

		const auto factor = kPredictionGain * share * a_horizon / dt;

		Npc::ViewMarker result = a_measured;

		result.x = a_measured.x + static_cast<float>(dx * factor);
		result.y = a_measured.y + static_cast<float>(dy * factor);

		// The prediction is never allowed to run away.
		const auto offsetX = result.x - a_measured.x;
		const auto offsetY = result.y - a_measured.y;

		const auto distance = std::sqrt(offsetX * offsetX + offsetY * offsetY);

		if (distance > kMaxPredictionPx) {
			const auto scale = kMaxPredictionPx / distance;

			result.x = a_measured.x + offsetX * scale;
			result.y = a_measured.y + offsetY * scale;
		}

		return result;
	}

	void Bridge::SendNpcSlots(const Npc::SlotDelta& a_delta,
							  std::int64_t a_time,
							  int a_frameInterval,
							  int a_leadFrames)
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
					// The slot is free now, so its measurements are of no
					// use to the NPC that takes it next.
					_slots[slot] = SlotState{};
				}

				_pendingHide.clear();
			}
			else {
				// The view did not get the message, so the slots are hidden
				// with one of the next frames.
				_pendingHide = _scratchHide;
			}
		}

		// How far the markers have to be moved forward.
		//
		// The page of the view is drawn in the frame after the one that was
		// measured and it is composited with the frame after that one, so a
		// whole frame of the game has to be compensated. A pipeline that
		// fell further behind is compensated frame by frame, and a camera
		// that was already one frame old needs one frame more.
		auto frames = 1 + a_leadFrames;

		const auto age = _syncAge.load();

		if (age > 0 && a_frameInterval > 0) {
			const auto behind = static_cast<int>((age + a_frameInterval - 1) / a_frameInterval);

			frames = Larger(frames, behind + a_leadFrames);
		}

		const auto horizon = Smaller(static_cast<double>(frames * a_frameInterval),
									 static_cast<double>(kMaxPredictionMs));

		// The horizon is written down, so the log can tell how much of the
		// delay of the whole path was compensated.
		_predictionFrames.store(frames, std::memory_order_relaxed);
		_predictionHorizon.store(static_cast<std::int64_t>(horizon + 0.5),
								  std::memory_order_relaxed);

		// The slots of the NPCs that moved. The frame goes first: it is the
		// one that has to be on the screen as soon as possible, while the
		// frames of the other channels only carry the names, the health and
		// the slots that left the screen.
		_scratchSlots.clear();
		_scratchValues.clear();

		for (const auto& entry : a_delta.occupied) {
			auto& state = _slots[entry.index];

			const auto measured = Npc::BuildViewMarker(entry.marker, _viewport);

			// The first measurement of a slot has no movement to follow.
			if (state.measuredTime == 0) {
				state.measured = measured;
				state.measuredTime = a_time;
				state.moved = 0.0f;
			}

			const auto value = Predict(state, measured, a_time, horizon);

			// The measurements are kept for every frame, not only for the
			// frames that are sent, so the movement of a marker is always
			// the movement of the last frame.
			const auto dx = measured.x - state.measured.x;
			const auto dy = measured.y - state.measured.y;

			state.moved = static_cast<float>(std::sqrt(dx * dx + dy * dy));
			state.measured = measured;
			state.measuredTime = a_time;

			if (state.known && state.sent == value) {
				continue;
			}

			_scratchSlots.push_back(entry.index);
			_scratchValues.push_back(value);
		}

		if (!_scratchSlots.empty()) {
			if (Send(kNpcSlotsMoveFunction, BuildFrame(_scratchValues, _scratchSlots, a_time))) {
				for (std::size_t i = 0; i < _scratchSlots.size(); ++i) {
					_slots[_scratchSlots[i]].sent = _scratchValues[i];
					_slots[_scratchSlots[i]].known = true;
				}
			}

			// The positions are not known by the view, so they are sent again
			// with the next frame. The rest of the frame still goes out: the
			// view needs the ids and the names of the NPCs that entered the
			// screen even when a position was lost.
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

		// The health of the NPCs that the view does not know yet or whose
		// health changed. The health is drawn as a bar above the head of the
		// NPC, so it is only sent when it really changed.
		JSON health = JSON::object();

		std::vector<Damage::ActorId> healthIds;
		std::vector<Npc::Health> healthValues;

		for (const auto& entry : a_delta.occupied) {
			const auto& value = entry.marker.health;

			if (!value.IsValid()) {
				continue;
			}

			const auto it = _sentHealth.find(entry.id);

			if (it != _sentHealth.end() && it->second == value) {
				continue;
			}

			health[FormIdToString(entry.id)] = {
				{ "current", value.current },
				{ "max", value.max },
			};

			healthIds.push_back(entry.id);
			healthValues.push_back(value);
		}

		if (!health.empty() && Send(kNpcHealthFunction, health.dump())) {
			for (std::size_t i = 0; i < healthIds.size(); ++i) {
				_sentHealth[healthIds[i]] = healthValues[i];
			}
		}

		// The health of the NPCs that left the screen is forgotten, so it is
		// sent again when they come back.
		if (!a_delta.hidden.empty()) {
			std::unordered_map<Damage::ActorId, Npc::Health> known;

			for (const auto& entry : a_delta.occupied) {
				const auto it = _sentHealth.find(entry.id);

				if (it != _sentHealth.end()) {
					known.emplace(entry.id, it->second);
				}
			}

			_sentHealth = std::move(known);
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
		return Send(kNpcSlotsShowFunction, showFrame);
	}

	void Bridge::ResetNpcCache() {
		std::lock_guard<std::mutex> lock(_npcMutex);

		// The world is replaced, so every NPC is gone and every slot is free.
		Npc::SlotPool::Get().Reset();

		// The view has to drop the NPCs it knows.
		for (std::size_t i = 0; i < Npc::kMaxSlots; ++i) {
			if (!_slots[i].known) {
				continue;
			}

			const auto slot = static_cast<std::uint8_t>(i);

			if (std::find(_pendingHide.begin(), _pendingHide.end(), slot) == _pendingHide.end()) {
				_pendingHide.push_back(slot);
			}
		}

		for (auto& slot : _slots) {
			slot = SlotState{};
		}

		// The names of the old world are of no use to the new one.
		_knownNames.clear();
		_sentHealth.clear();
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
