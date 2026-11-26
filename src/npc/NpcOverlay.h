#pragma once

#include <REL/Relocation.h>

#include <atomic>
#include <cstdint>

namespace Npc
{
	/// Drives the NPC head overlay.
	///
	/// The overlay refreshes the heads of the NPCs that are visible on the
	/// screen and pushes them to the view. It runs on the game thread,
	/// because the scene graph and the camera of the engine are only valid
	/// there.
	class Overlay
	{
	public:
		static Overlay& Get();

		/// Installs the hook of the frame update of the game.
		static bool Install();

		void Start();
		void Stop();

	private:
		Overlay() = default;
		~Overlay() = default;

		/// One refresh of the markers.
		void Tick();

		/// Called by the hook of the frame update.
		void OnFrame();

		/// One hook of the frame update.
		///
		/// Every place where the game calls its main update needs its own
		/// copy of the hook, because every copy needs its own pointer to the
		/// original function. The copy forwards all four arguments of the
		/// call and returns the result of the original function, so the game
		/// gets exactly the values it would have got without the hook.
		template <std::size_t I>
		struct FrameHook
		{
			using Thunk_t = std::uint64_t (*)(void*, void*, void*, void*);

			static std::uint64_t Thunk(void* a_arg1, void* a_arg2, void* a_arg3, void* a_arg4)
			{
				// The game updates the frame first: the overlay then sees a
				// finished scene and a fresh camera.
				const auto result = Original(a_arg1, a_arg2, a_arg3, a_arg4);

				Overlay::Get().OnFrame();

				return result;
			}

			static inline REL::Relocation<Thunk_t> Original;
		};

		/// Installs the hook of the frame update on one place of a call.
		template <std::size_t I>
		static bool InstallFrameHook(std::uintptr_t a_site);

		std::atomic_bool _running{ false };
	};
}
