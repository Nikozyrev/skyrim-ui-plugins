#include "npc/NpcOverlay.h"

#include "npc/NpcSlots.h"
#include "npc/NpcTracker.h"
#include "ui/PrismaBridge.h"

#include <chrono>
#include <exception>

namespace Npc
{
	namespace
	{
		// Оптимальный интервал для PrismaUI (~60 FPS).
		// При FPS игры ниже 60 обновление будет происходить каждый кадр.
		constexpr auto kMinUpdateInterval = std::chrono::milliseconds(16);

		std::chrono::steady_clock::time_point g_lastUpdate{};
	}

	Overlay& Overlay::Get()
	{
		static Overlay singleton;
		return singleton;
	}

	bool Overlay::Install()
	{
		const auto version = REL::Module::get().version();

		// The place of the call that the hook replaces is different in every
		// version of the game: the engine moves the code around, so the offset
		// of the call inside the function is not stable. The id of the
		// function is stable, so the function is taken from the address
		// library and the offset is picked by the version of the game.
		std::uintptr_t site = 0;

		if (version >= REL::Version(1, 7, 99, 0)) {
			// AE 1.7.104: the engine was rebuilt, the update of the frame
			// (36564) is now called from the function 36544.
			site = REL::RelocationID(35545, 36544).address() + 0x160;
		}
		else if (version >= REL::Version(1, 6, 0, 0)) {
			// AE 1.6: the hook sits inside the update of the frame (36564).
			site = REL::RelocationID(35565, 36564).address() + 0xC26;
		}
		else {
			// SE 1.5: the hook sits inside the update of the frame (35565).
			site = REL::RelocationID(35565, 36564).address() + 0x748;
		}

		return InstallFrameHook<0>(site);
	}

	template <std::size_t I>
	bool Overlay::InstallFrameHook(std::uintptr_t a_site)
	{
		auto& trampoline = SKSE::GetTrampoline();

		// The hook replaces the call of the game with a call of our thunk. The
		// trampoline gives back the function that the game called there, so
		// the thunk can call it with the same arguments and give its result
		// back to the game.
		FrameHook<I>::Original = trampoline.write_call<5>(a_site, FrameHook<I>::Thunk);

		logger::info("NPC overlay: the frame update hook is installed.");

		return true;
	}

	void Overlay::Start()
	{
		_running.store(true);

		logger::info("NPC overlay started.");
	}

	void Overlay::Stop()
	{
		_running.store(false);
		logger::info("NPC overlay stopped.");
	}

	void Overlay::OnFrame()
	{
		if (!_running.load()) {
			return;
		}

		try {
			Tick();
		}
		catch (const std::exception& a_error) {
			logger::error("NPC overlay: the overlay tick threw: {}", a_error.what());
		}
		catch (...) {
			logger::error("NPC overlay: the overlay tick threw an unknown error.");
		}
	}

	void Overlay::Tick()
	{
		if (!_running.load()) {
			return;
		}

		// Проверяем, что мы не в главном меню и игра не на паузе
		auto* ui = RE::UI::GetSingleton();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!ui || !player || ui->GameIsPaused() || ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();

		// Ограничиваем частоту отправки данных в PrismaUI (не чаще 60 раз в сек)
		if (now - g_lastUpdate < kMinUpdateInterval) {
			return;
		}
		g_lastUpdate = now;

		try {
			// The overlay is pointless without a view to draw on.
			if (UI::Bridge::Get().IsReady()) {
				// The tracker clears its markers when there is nothing to
				// track, so the pool also frees the slots of the NPCs that
				// left the screen.
				Npc::Tracker::Get().Collect();
				const auto& markers = Npc::Tracker::Get().Markers();

				// The pool owns the slots: it keeps the slot of an NPC that
				// is still on the screen, frees the slot of an NPC that left
				// and hands a free one to an NPC that entered the screen.
				const auto delta = Npc::SlotPool::Get().Update(markers);

				// The bridge takes care of the traffic: it only sends the
				// slots that really changed and it skips a frame in which no
				// marker moved.
				UI::Bridge::Get().SendNpcSlots(delta);
			}
		}
		catch (const std::exception& a_error) {
			logger::error("NPC overlay: failed to refresh the NPC markers: {}", a_error.what());
		}
		catch (...) {
			logger::error("NPC overlay: failed to refresh the NPC markers (unknown error).");
		}
	}
}
