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

	void Overlay::InstallHook()
	{
		// Выделяем память для трамплина (14 байт достаточно для одного write_call<5>)
		SKSE::AllocTrampoline(14);

		// Смещения для SE (35565 + 0x748) и AE (36564 + 0xC26)
		// В CommonLibSSE-NG можно использовать REL::VariantOffset
		REL::Relocation<std::uintptr_t> target{
			REL::RelocationID(35565, 36564),
			REL::VariantOffset(0x748, 0xC26, 0x7EE)
		};

		auto& trampoline = SKSE::GetTrampoline();
		MainUpdateHook::_original = trampoline.write_call<5>(target.address(), MainUpdateHook::Thunk);

		logger::info("NPC overlay: MainUpdate hook installed.");
	}

	void Overlay::MainUpdateHook::Thunk()
	{
		// 1. Сначала даем игре полностью обновить кадр, физику и камеру
		_original();

		// 2. Затем обновляем наш оверлей
		Overlay::Get().Tick();
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
			logger::error("NPC overlay: failed to refresh the head markers: {}", a_error.what());
		}
		catch (...) {
			logger::error("NPC overlay: failed to refresh the head markers (unknown error).");
		}
	}
}
