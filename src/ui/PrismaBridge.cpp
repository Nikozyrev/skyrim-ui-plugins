#include "ui/PrismaBridge.h"

#include "PrismaUI_API.h"
#include "damage/DamageTracker.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

using JSON = nlohmann::json;

namespace UI
{
	namespace
	{
		/// Name of the JavaScript entry point the overlay listens on.
		constexpr auto kUpdateFunction = "onDamageUpdate";

		/// Name of the JavaScript entry point the NPC head overlay listens on.
		/// The ids of the visible NPCs, their names and their positions use
		/// three separate functions: the ids change far less often than the
		/// positions and the names change even less often than the ids.
		constexpr auto kNpcIdsFunction = "onNpcIdsUpdate";
		constexpr auto kNpcNamesFunction = "onNpcNamesUpdate";
		constexpr auto kNpcNamesClearFunction = "onNpcNamesClear";
		constexpr auto kNpcMarkersFunction = "onNpcMarkersUpdate";

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

		/// The ids of the visible NPCs, in the order of the indexes that the
		/// frames of the positions use.
		JSON NpcIdsToJson(const std::vector<Damage::ActorId>& a_ids)
		{
			JSON ids = JSON::array();

			for (const auto id : a_ids) {
				ids.push_back(FormIdToString(id));
			}

			return JSON{
				{ "type", "npcIds" },
				{ "ids", ids },
			};
		}

		/// Builds a frame of the positions of the visible NPCs.
		///
		/// A frame is a list of the tuples "index:x,y,scale", separated by
		/// semicolons. The index is the position of the NPC in the list of the
		/// ids, so a frame only carries the numbers that really changed.
		std::string BuildFrame(const std::vector<Npc::ViewMarker>& a_values,
							   const std::vector<std::size_t>& a_indices)
		{
			std::string frame;

			for (const auto index : a_indices) {
				if (!frame.empty()) {
					frame.push_back(';');
				}

				const auto& value = a_values[index];

				frame += std::to_string(index);
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
		JSON payload{
			{ "type", "style" },
			{ "baseFontSize", Npc::kBaseFontSize },
		};

		Send(kStyleFunction, payload.dump());
	}

	void Bridge::ResetNpcState()
	{
		// The view starts with an empty list of markers, so the table and the
		// positions have to be sent again even when they did not change since
		// the last update.
		std::lock_guard<std::mutex> lock(_npcMutex);
		_npcIds.clear();
		_npcFrame.clear();
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
		_npcFrame.clear();

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

	void Bridge::SendNpcMarkers(const std::vector<Npc::Marker>& a_markers)
	{
    if (!_api || !_viewReady.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> lock(_npcMutex);

    if (!_viewport.IsValid()) {
        return;
    }

    // Очищаем буферы без освобождения выделенной памяти (capacity сохраняется)
    _scratchIds.clear();
    _scratchValues.clear();
    _scratchChanged.clear();

    const std::size_t count = a_markers.size();
    _scratchIds.reserve(count);
    _scratchValues.reserve(count);
    _scratchChanged.reserve(count);

    // 1. Собираем ID и проверяем новые имена за один проход
    JSON names = nullptr;
    for (const auto& marker : a_markers) {
        _scratchIds.push_back(marker.id);

				const bool isKnown = _knownNames.contains(marker.id);
        // Имя отправляем только если вообще не видели этого NPC в текущей сессии
        if (!isKnown) {
            if (names.is_null()) {
                names = JSON::object();
            }
            names[FormIdToString(marker.id)] = marker.name;
						_knownNames.try_emplace(marker.id, marker.name);
        }
    }

    const bool idsChanged = (_scratchIds != _npcIds);

    // 2. Считаем экранные координаты и сразу ищем изменения (без создания nextFrame)
    for (std::size_t i = 0; i < count; ++i) {
        const auto& marker = a_markers[i];
        auto viewMarker = Npc::BuildViewMarker(marker, _viewport);
        _scratchValues.push_back(viewMarker);

        if (idsChanged) {
            _scratchChanged.push_back(i);
            continue;
        }

        const auto it = _npcFrame.find(marker.id);
        if (it == _npcFrame.end() || it->second != viewMarker) {
            _scratchChanged.push_back(i);
        }
    }

    // 3. Отправка данных на фронт
    if (!names.is_null()) {
        JSON payload{
            { "type", "npcNames" },
            { "names", std::move(names) },
        };

				logger::info("Send NPC names {}", payload.dump());

        if (!Send(kNpcNamesFunction, payload.dump())) {
            // Если отправка не удалась, откатываем кеш имен, чтобы попробовать в след. кадре
            for (const auto& marker : a_markers) {
                _knownNames.erase(marker.id);
            }
            return;
        }
    }

    if (idsChanged) {
				std::string npcIdsPayload = NpcIdsToJson(_scratchIds).dump();

				logger::info("Send NPC ids {}", npcIdsPayload);

        if (!Send(kNpcIdsFunction, npcIdsPayload)) {
            return;
        }
        _npcIds = _scratchIds; // Копирует в уже выделенную память _npcIds
    }

    if (_scratchChanged.empty()) {
        return; // Ничего не сдвинулось, _npcFrame обновлять не нужно
    }

    if (!Send(kNpcMarkersFunction, BuildFrame(_scratchValues, _scratchChanged))) {
        return;
    }

    // 4. Обновляем _npcFrame на месте, без пересоздания хеш-таблицы
    if (idsChanged) {
        _npcFrame.clear();
        for (std::size_t i = 0; i < count; ++i) {
            _npcFrame.emplace(_scratchIds[i], _scratchValues[i]);
        }
    } else {
        for (std::size_t idx : _scratchChanged) {
            _npcFrame[_scratchIds[idx]] = _scratchValues[idx];
        }
    }
	}

	void Bridge::ResetNpcCache() {
		_knownNames.clear();
		_npcIds.clear();
		_npcFrame.clear();
		Send(kNpcNamesClearFunction, "");
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
