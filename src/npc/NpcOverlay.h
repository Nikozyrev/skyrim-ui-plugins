#pragma once

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

		// Вызывать один раз при инициализации плагина (например, в SKSEPluginLoad)
		static void InstallHook();

		void Start();
		void Stop();

	private:
		Overlay() = default;
		~Overlay() = default;

		/// One refresh of the markers. Re-arms itself for the next frame.
		void Tick();

		struct MainUpdateHook
		{
			static void Thunk();
			static inline REL::Relocation<decltype(Thunk)> _original;
		};

		std::atomic_bool _running{ false };
	};
}