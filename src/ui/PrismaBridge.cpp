#include "ui/PrismaBridge.h"

#include "PrismaUI_API.h"
#include "damage/DamageTracker.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <exception>

using JSON = nlohmann::json;

namespace UI
{
	namespace
	{
		/// Name of the JavaScript entry point the overlay listens on.
		constexpr auto kUpdateFunction = "onDamageUpdate";

		/// Name of the JavaScript entry point the NPC head overlay listens on.
		constexpr auto kNpcFunction = "onNpcUpdate";

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

		JSON MarkersToJson(const std::vector<Npc::Marker>& a_markers)
		{
			JSON markers = JSON::array();

			for (const auto& marker : a_markers) {
				markers.push_back(JSON{
					{ "id", FormIdToString(marker.id) },
					{ "name", marker.name },
					{ "x", marker.x },
					{ "y", marker.y },
					{ "r", marker.radius },
				});
			}

			return markers;
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

		logger::info("PrismaUI view '{}' created.", a_viewPath);
		return true;
	}

	void Bridge::OnDomReady()
	{
		_domReady.store(true);
		logger::info("PrismaUI view is ready, pushing the damage overlay state.");
		SendSnapshot();
	}

	void Bridge::OnViewReady()
	{
		// The view registered its handlers, so the JavaScript functions the
		// plugin calls do exist now.
		_viewReady.store(true);

		logger::info("PrismaUI view announced itself, pushing the damage overlay state.");
		SendSnapshot();
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
		// The NPC channel is only used once the view announced itself: before
		// that the view does not know the JavaScript function yet.
		if (!_api || !_viewReady.load()) {
			return;
		}

		JSON payload{
			{ "type", "npc" },
			{ "markers", MarkersToJson(a_markers) },
		};

		Send(kNpcFunction, payload.dump());
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
