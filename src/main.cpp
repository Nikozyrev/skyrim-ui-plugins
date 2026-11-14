#include "PrismaUI_API.h"

#include "damage/DamageHook.h"
#include "damage/DamageTracker.h"
#include "npc/NpcOverlay.h"
#include "ui/PrismaBridge.h"
#include "util/GameState.h"

namespace
{
	/// The name SKSE knows this plugin under (see the commonlibsse-ng plugin rule).
	constexpr auto kPluginName = "PrismaUI-Example-Plugin";

	/// Path of the view relative to the PrismaUI "views" folder.
	constexpr auto kViewPath = "PrismaUI-Example-UI/index.html";

	/// Upper bound of the game thread poll that brings the UI up. The poll
	/// waits for the player to load a world, so it has to survive a long time
	/// in the main menu.
	constexpr int kUiPollLimit = 60 * 60 * 60 * 1000;

	int g_uiPollCount = 0;

	/// Brings the UI up. Must run on the game thread, because creating a view
	/// initializes PrismaUI's renderer.
	void TryCreateUi()
	{
		// The view is created only while a world is loaded. In the main menu
		// the overlay would sit on top of the menu and swallow its input.
		// if (!GameState::IsWorldLoaded()) {
		// 	return;
		// }

		auto& bridge = UI::Bridge::Get();

		if (!bridge.AcquireApi()) {
			return;
		}

		bridge.CreateView(kViewPath);
	}

	/// Game thread poll that creates the view as soon as PrismaUI is available.
	///
	/// The messaging interface is the primary way to learn that every plugin has
	/// been loaded. This poll is the fallback for the case where the listener
	/// could not be registered, and it also covers PrismaUI being loaded after
	/// this plugin. It keeps running until a world is loaded, because that is
	/// the moment the overlay is allowed to appear.
	void PollForUi()
	{
		if (UI::Bridge::Get().IsViewCreated()) {
			return;
		}

		++g_uiPollCount;

		TryCreateUi();

		if (UI::Bridge::Get().IsViewCreated()) {
			return;
		}

		if (g_uiPollCount >= kUiPollLimit) {
			logger::error("The overlay was not started within an hour, it stays disabled.");
			return;
		}

		if (auto* taskInterface = SKSE::GetTaskInterface(); taskInterface) {
			taskInterface->AddTask(PollForUi);
		}
	}

	void OnDataLoaded()
	{
		// A new game or a loaded save starts with a clean slate.
		Damage::Tracker::Get().Reset();
		UI::Bridge::Get().SendSnapshot();
	}

	void SKSEMessageHandler(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kPostLoad:
		case SKSE::MessagingInterface::kPostPostLoad:
			// Every plugin has been loaded by now, so PrismaUI is available.
			TryCreateUi();
			break;

		case SKSE::MessagingInterface::kDataLoaded:
			OnDataLoaded();

			// The NPC head overlay runs as a game thread task. It stays idle until the
			// view is up, so it can safely be started this early.
			Npc::Overlay::Get().Start();
			break;

		case SKSE::MessagingInterface::kPreLoadGame:
			UI::Bridge::Get().ResetNpcCache();
			break;

		default:
			break;
		}
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	REL::Module::reset();

	auto* messaging =
		reinterpret_cast<SKSE::MessagingInterface*>(a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));

	if (!messaging) {
		logger::critical("Failed to acquire the messaging interface. The plugin will not load.");
		return false;
	}

	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(1 << 10);

	// The damage hook does not depend on the game state, so it is installed right
	// away. Damage tracking therefore works even when the messaging interface is
	// unavailable.
	Damage::Hook::Install();
	
	Npc::Overlay::InstallHook();

	// The sender of a SKSE message has to be the name of a *loaded plugin*.
	// "SKSE" is the pseudo plugin SKSE registers itself under and is the only
	// sender that is guaranteed to exist while the plugins are still loading.
	// Any other name makes RegisterListener fail.
	if (!messaging->RegisterListener("SKSE", SKSEMessageHandler)) {
		logger::error("Failed to register the SKSE messaging listener, falling back to the game thread poll.");
	}

	// Bring the UI up as soon as PrismaUI is loaded.
	if (auto* taskInterface = SKSE::GetTaskInterface(); taskInterface) {
		taskInterface->AddTask(PollForUi);
	}

	logger::info("{} loaded.", kPluginName);
	return true;
}
