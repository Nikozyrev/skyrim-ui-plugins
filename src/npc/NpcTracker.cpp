#include "npc/NpcTracker.h"

#include "util/ActorUtil.h"
#include "util/GameState.h"

#include <RE/B/BSContainer.h>
#include <RE/F/FixedStrings.h>
#include <RE/M/Main.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiCamera.h>
#include <RE/N/NiPoint3.h>
#include <RE/N/NiRTTI.h>
#include <RE/P/PlayerCamera.h>
#include <RE/P/ProcessLists.h>

#include <cmath>
#include <exception>
#include <unordered_set>

namespace Npc
{
	namespace
	{
		/// The head of a humanoid takes up roughly an eighth of the body
		/// height. The bound of the actor covers the whole body, so the head
		/// is a small fraction of it.
		constexpr float kHeadToBodyRatio = 0.13f;

		/// Fallback radius (in world units) for actors of an unknown size.
		constexpr float kFallbackHeadRadius = 0.12f;

		/// Heads smaller than this fraction of the screen height do
		/// not get a marker: they would be a couple of pixels wide.
		constexpr float kMinScreenRadius = 0.0015f;

		/// A head that is (partially) outside of the viewport is still drawn.
		constexpr float kViewportMargin = 0.02f;

		/// Dead bodies do not get a marker.
		constexpr bool kIncludeDeadNpcs = false;

		/// True while a world is loaded.
		///
		/// The tracker reads the scene graph and the actors of the world, so
		/// it must stay idle while the main menu is up: there the world is
		/// torn down and its objects are not valid anymore.
		bool IsWorldLoaded()
		{
			return GameState::IsWorldLoaded();
		}

		/// Logs the transition between "a world is loaded" and "no world".
		/// The first line tells whether the tracker ever came up at all.
		void ReportState(bool a_hasWorld)
		{
			static bool g_hasWorld = false;
			static bool g_reported = false;

			if (g_reported && g_hasWorld == a_hasWorld) {
				return;
			}

			g_reported = true;
			g_hasWorld = a_hasWorld;

			logger::info("NPC tracker: {}.",
						 a_hasWorld ? "a world is loaded, the head markers are active"
									: "no world is loaded, the head markers are idle");
		}

		/// True when the camera carries a usable projection.
		bool IsUsableCamera(const RE::NiCamera* a_camera)
		{
			if (!a_camera) {
				return false;
			}

			const auto& position = a_camera->world.translate;

			return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
		}

		/// The camera that renders the world. Its projection matches what the
		/// player sees, so it is the only camera the markers can be based on.
		RE::NiCamera* ResolveCamera()
		{
			// The world camera is a child of the camera root of the player
			// camera. Those objects exist in every game state, so they are the
			// safer source.
			auto* playerCamera = RE::PlayerCamera::GetSingleton();
			if (playerCamera && playerCamera->cameraRoot) {
				for (auto& child : playerCamera->cameraRoot->GetChildren()) {
					if (auto* camera = netimmerse_cast<RE::NiCamera*>(child.get())) {
						if (IsUsableCamera(camera)) {
							return camera;
						}
					}
				}
			}

			// Fallback: the camera of the world scene graph.
			if (auto* camera = RE::Main::WorldRootCamera(); IsUsableCamera(camera)) {
				return camera;
			}

			return nullptr;
		}

		/// The node that carries the head of an actor.
		///
		/// The head bone of the humanoid skeleton is used on purpose: it is
		/// part of the model that is loaded anyway, while the face part of
		/// the NPC would make the engine touch the first person model.
		RE::NiAVObject* ResolveHead(RE::NiAVObject* a_model, const RE::BSFixedString& a_headName)
		{
			if (a_headName.empty()) {
				return nullptr;
			}

			return a_model->GetObjectByName(a_headName);
		}

		/// The radius of the head in world units.
		float ResolveHeadRadius(const RE::NiAVObject* a_model, const RE::NiAVObject* a_head)
		{
			// The bound of the head node is the most accurate source, but it
			// is only filled in for the meshes and not for the bones.
			if (a_head) {
				const float radius = a_head->worldBound.radius;

				if (radius > 0.02f && radius < 10.0f) {
					return radius;
				}
			}

			// Otherwise the head is estimated from the bound of the model,
			// which covers the whole body.
			if (a_model) {
				const float radius = a_model->worldBound.radius;

				if (radius > 0.05f && radius < 100.0f) {
					return radius * kHeadToBodyRatio;
				}
			}

			return kFallbackHeadRadius;
		}

		/// Projects a world position into the normalized viewport of the game.
		///
		/// `NiCamera::WorldPtToScreenPt3` returns the coordinates inside the
		/// camera port: (0,0) is the bottom left corner, (1,1) the top right
		/// one. The function fails when the point is behind the camera.
		bool ProjectToScreen(RE::NiCamera* a_camera, const RE::NiPoint3& a_world, float& a_x, float& a_y)
		{
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;

			if (!a_camera->WorldPtToScreenPt3(a_world, x, y, z, 1e-5f)) {
				return false;
			}

			// A depth of zero or less means the point is behind the camera.
			if (z <= 0.0f) {
				return false;
			}

			a_x = x;
			a_y = y;
			return true;
		}

		/// Builds the marker of a single actor. Returns false when the actor
		/// has no head that is visible on the screen.
		bool BuildMarker(RE::Actor* a_actor, RE::NiCamera* a_camera, const RE::BSFixedString& a_headName,
						 Marker& a_marker)
		{
			// Actors without a model have nothing to draw. The model is also
			// the anchor for every following access.
			auto* model = a_actor->Get3D();
			if (!model) {
				return false;
			}

			auto* head = ResolveHead(model, a_headName);

			RE::NiPoint3 headPosition;
			float headRadius = 0.0f;

			if (head) {
				headPosition = head->world.translate;
				headRadius = ResolveHeadRadius(model, head);
			}
			else {
				// Without a head node the head is estimated above the feet of
				// the actor.
				const float bodyRadius = model->worldBound.radius;
				headPosition = a_actor->GetPosition();
				headPosition.z += bodyRadius * 1.8f;
				headRadius = ResolveHeadRadius(model, nullptr);
			}

			if (headRadius <= 0.0f) {
				return false;
			}

			float x = 0.0f;
			float y = 0.0f;

			if (!ProjectToScreen(a_camera, headPosition, x, y)) {
				return false;
			}

			// The size of the head on the screen is measured with the very
			// same projection: a probe point one head radius above the head
			// is projected as well and the distance between both points is
			// the radius of the head in screen fractions.
			const auto cameraPosition = a_camera->world.translate;

			RE::NiPoint3 forward = headPosition - cameraPosition;
			const float distance = forward.Length();

			if (distance < 1e-4f) {
				return false;
			}

			forward = forward / distance;

			// The up vector of the camera is the world up vector projected
			// onto the plane of the view.
			const RE::NiPoint3 worldUp{ 0.0f, 0.0f, 1.0f };

			RE::NiPoint3 right = worldUp.Cross(forward);

			if (right.SqrLength() < 1e-6f) {
				// The camera looks straight up or down.
				right = RE::NiPoint3{ 1.0f, 0.0f, 0.0f }.Cross(forward);
			}

			if (right.SqrLength() < 1e-6f) {
				return false;
			}

			right.Unitize();

			const RE::NiPoint3 up = forward.Cross(right);

			float probeX = 0.0f;
			float probeY = 0.0f;

			if (!ProjectToScreen(a_camera, headPosition + up * headRadius, probeX, probeY)) {
				return false;
			}

			const float screenRadius = std::abs(probeY - y);

			if (screenRadius < kMinScreenRadius) {
				return false;
			}

			if (x < -kViewportMargin || x > 1.0f + kViewportMargin) {
				return false;
			}

			if (y < -kViewportMargin || y > 1.0f + kViewportMargin) {
				return false;
			}

			a_marker.id = a_actor->GetFormID();
			a_marker.x = x;
			a_marker.y = y;
			a_marker.radius = screenRadius;

			return true;
		}

		/// True when the actor is an NPC that can carry a head marker.
		bool IsTrackable(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}

			if (ActorUtil::IsPlayer(a_actor)) {
				return false;
			}

			// Only NPCs are tracked: creatures without an actor base are
			// summons and other scripted objects.
			if (!a_actor->GetActorBase()) {
				return false;
			}

			if (a_actor->IsDisabled()) {
				return false;
			}

			if (!kIncludeDeadNpcs && a_actor->IsDead()) {
				return false;
			}

			return true;
		}
	}

	Tracker& Tracker::Get()
	{
		static Tracker singleton;
		return singleton;
	}

	const std::string& Tracker::ResolveName(RE::Actor* a_actor, Damage::ActorId a_id)
	{
		if (const auto it = _names.find(a_id); it != _names.end()) {
			return it->second;
		}

		// The name of an NPC does not change while it is on the screen, so it
		// is read from the engine only once. The reference stays valid, the
		// map only rehashes its buckets.
		return _names.emplace(a_id, ActorUtil::SafeName(a_actor)).first->second;
	}

	void Tracker::Collect()
	{
		_markers.clear();

		// Nothing may ever unwind into the engine: the collection runs from
		// the game loop and a broken actor must not take the game down.
		try {
			// The tracker only makes sense while a world is loaded. In the
			// main menu the scene graph of the world is torn down and the
			// camera and the actors are not valid there.
			if (!IsWorldLoaded()) {
				ReportState(false);
				return;
			}

			ReportState(true);

			auto* camera = ResolveCamera();
			auto* processLists = RE::ProcessLists::GetSingleton();

			if (!camera || !processLists) {
				return;
			}

			// The name of the head bone is looked up once per collection.
			RE::BSFixedString headName;
			if (const auto* fixedStrings = RE::FixedStrings::GetSingleton(); fixedStrings) {
				headName = fixedStrings->npcHead;
			}

			std::unordered_set<Damage::ActorId> visible;

			processLists->ForAllActors([&](RE::Actor* a_actor) -> RE::BSContainer::ForEachResult {
				try {
					if (!IsTrackable(a_actor)) {
						return RE::BSContainer::ForEachResult::kContinue;
					}

					Marker marker;

					if (BuildMarker(a_actor, camera, headName, marker)) {
						// The name is read from the engine only when the NPC is
						// seen for the first time.
						marker.name = ResolveName(a_actor, marker.id);
						visible.insert(marker.id);

						_markers.push_back(std::move(marker));
					}
				}
				catch (const std::exception& a_error) {
					logger::error("NPC tracker: failed to read an actor: {}", a_error.what());
				}
				catch (...) {
					logger::error("NPC tracker: failed to read an actor (unknown error).");
				}

				return RE::BSContainer::ForEachResult::kContinue;
			});

			// The names of the NPCs that left the screen are dropped, so the
			// cache does not grow over the session.
			for (auto it = _names.begin(); it != _names.end();) {
				if (visible.count(it->first) == 0) {
					it = _names.erase(it);
				}
				else {
					++it;
				}
			}
		}
		catch (const std::exception& a_error) {
			logger::error("NPC tracker: failed to collect the visible NPCs: {}", a_error.what());
		}
		catch (...) {
			logger::error("NPC tracker: failed to collect the visible NPCs (unknown error).");
		}
	}
}
