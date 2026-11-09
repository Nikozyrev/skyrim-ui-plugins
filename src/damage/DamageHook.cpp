#include "damage/DamageHook.h"

#include "damage/DamageTracker.h"
#include "damage/DamageTypes.h"
#include "ui/PrismaBridge.h"

#include <atomic>
#include <chrono>
#include <exception>

namespace Damage
{
	namespace
	{
		// `Actor::HandleHealthDamage` is a virtual function. Its slot inside the
		// actor vtable is 0x104 on SE/AE and 0x106 on VR (the VR vtable carries two
		// extra entries). The value matches the layout documented by CommonLibSSE-NG.
		constexpr std::size_t kHandleHealthDamageSlot = 0x104;
		constexpr std::size_t kHandleHealthDamageSlotVR = 0x106;

		// Guards against re-entrancy: our own capture path must never end up
		// calling the hooked function again.
		thread_local bool g_capturing = false;

		/// Resets the re-entrancy guard even when an exception unwinds the stack.
		struct CaptureGuard
		{
			CaptureGuard() { g_capturing = true; }
			~CaptureGuard() { g_capturing = false; }
		};

		std::atomic_bool g_installed{ false };

		std::uint64_t NowMs()
		{
			// Wall clock milliseconds so the view can age out old entries with the
			// same clock it uses for `Date.now()`.
			using namespace std::chrono;
			return static_cast<std::uint64_t>(
				duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
		}

		/// The player is identified by the singleton pointer first (robust against
		/// unusual form IDs) and by the well known player reference ID as fallback.
		bool IsPlayer(const RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}

			if (const auto* player = RE::PlayerCharacter::GetSingleton(); player == a_actor) {
				return true;
			}

			return a_actor->IsPlayerRef();
		}

		/// Display name of an actor with a safe fallback chain. Never returns null.
		std::string SafeName(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return {};
			}

			if (const auto* name = a_actor->GetDisplayFullName(); name && *name) {
				return name;
			}

			if (const auto* base = a_actor->GetBaseObject()) {
				if (const auto* name = base->GetName(); name && *name) {
					return name;
				}
			}

			return "Unknown";
		}

		/// The attacker pointer comes from whoever called into the hooked vtable
		/// slot, not only from the engine. An implausible pointer is treated as
		/// "no attacker" instead of being dereferenced.
		RE::Actor* SanitizeAttacker(RE::Actor* a_attacker)
		{
			if (!a_attacker) {
				return nullptr;
			}

			// Windows never maps the first 64 KiB of a process address space, so
			// any real Actor* is guaranteed to be above it.
			constexpr auto kMinValidUserAddress = std::uintptr_t{ 0x10000 };

			if (reinterpret_cast<std::uintptr_t>(a_attacker) < kMinValidUserAddress) {
				logger::warn("Damage hook: implausible attacker pointer ignored.");
				return nullptr;
			}

			return a_attacker;
		}

		/// Builds a damage event out of the hooked call and hands it over to the
		/// tracker. Only damage that involves the player is of interest.
		void Capture(RE::Actor* a_target, RE::Actor* a_attacker, float a_damage)
		{
			if (!a_target) {
				return;
			}

			a_attacker = SanitizeAttacker(a_attacker);

			const bool targetIsPlayer = IsPlayer(a_target);
			const bool attackerIsPlayer = IsPlayer(a_attacker);

			if (!targetIsPlayer && !attackerIsPlayer) {
				return;
			}

			// Nothing may ever unwind into the engine's damage routine: a stray
			// exception here would take the game down.
			try {
				DamageEvent event;
				event.timeMs = NowMs();
				event.damage = a_damage;

				event.targetId = a_target->GetFormID();
				event.targetName = SafeName(a_target);
				event.targetIsPlayer = targetIsPlayer;

				if (a_attacker) {
					event.attackerId = a_attacker->GetFormID();
					event.attackerName = SafeName(a_attacker);
					event.attackerIsPlayer = attackerIsPlayer;
				}
				else {
					// Environmental damage (falls, traps, poison without a source).
					event.attackerName = "Environment";
				}

				Tracker::Get().Record(event);
				UI::Bridge::Get().OnDamageRecorded(event);
			}
			catch (const std::exception& a_error) {
				logger::error("Damage hook: failed to record a damage event: {}", a_error.what());
			}
			catch (...) {
				logger::error("Damage hook: failed to record a damage event (unknown error).");
			}
		}

		/// One hook instance per concrete actor class. Every class owns its own
		/// copy of the vtable, so every class needs its own slot for the original.
		template <class TActor>
		struct HandleHealthDamageHook
		{
			using Thunk_t = void(RE::Actor*, RE::Actor*, float);

			static void Thunk(RE::Actor* a_self, RE::Actor* a_attacker, float a_damage)
			{
				// `a_damage` is negative when the target loses health and positive
				// when it is healed. Only the negative branch is a damage event.
				//
				// The data is captured *before* the original runs: the original may
				// destroy the target (death), which would make a later access a
				// use-after-free.
				if (!g_capturing && a_self && a_damage < 0.0f) {
					const CaptureGuard guard;
					Capture(a_self, a_attacker, -a_damage);
				}

				Original(a_self, a_attacker, a_damage);
			}

			static inline REL::Relocation<Thunk_t> Original;

			static bool Install(std::size_t a_slot, const char* a_className)
			{
				REL::Relocation<std::uintptr_t> vtable{ TActor::VTABLE[0] };

				if (!vtable.address()) {
					logger::error("Damage hook: could not resolve the vtable of {}.", a_className);
					return false;
				}

				Original = vtable.write_vfunc(a_slot, Thunk);

				if (!Original.address()) {
					logger::error("Damage hook: the vtable slot of {} was empty.", a_className);
					return false;
				}

				logger::info("Damage hook installed on {} (vtable slot 0x{:X}).", a_className, a_slot);
				return true;
			}
		};
	}

	bool Hook::Install()
	{
		if (g_installed.load()) {
			return true;
		}

		const auto slot = REL::Module::IsVR() ? kHandleHealthDamageSlotVR : kHandleHealthDamageSlot;

		const bool npcHooked = HandleHealthDamageHook<RE::Character>::Install(slot, "RE::Character");
		const bool playerHooked = HandleHealthDamageHook<RE::PlayerCharacter>::Install(slot, "RE::PlayerCharacter");

		if (!npcHooked && !playerHooked) {
			logger::error("Damage hook: no hook could be installed, damage tracking is disabled.");
			return false;
		}

		if (!npcHooked || !playerHooked) {
			logger::warn("Damage hook: only a part of the actor classes could be hooked, some damage may be missed.");
		}

		g_installed.store(true);
		return true;
	}

	bool Hook::IsInstalled()
	{
		return g_installed.load();
	}
}
