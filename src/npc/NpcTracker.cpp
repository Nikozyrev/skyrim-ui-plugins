#include "npc/NpcTracker.h"

#include "util/ActorUtil.h"
#include "util/GameState.h"

#include <RE/B/BSContainer.h>
#include <RE/M/Main.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiCamera.h>
#include <RE/N/NiPoint3.h>
#include <RE/N/NiRTTI.h>
#include <RE/P/PlayerCamera.h>
#include <RE/P/ProcessLists.h>

#include "RE/B/bhkPickData.h"
#include "RE/B/bhkWorld.h"
#include "RE/T/TESObjectCELL.h"

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

		/// Fallback height (in world units) of an actor of an unknown size.
		///
		/// It is the distance between the feet and the top of the head of a
		/// human, so it is used when the bound of the model is not usable.
		constexpr float kFallbackBodyHeight = 1.8f;

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
						 a_hasWorld ? "a world is loaded, the NPC markers are active"
									: "no world is loaded, the NPC markers are idle");
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

		/// The point of an actor that its marker is built around.
		struct BodyPoint
		{
			/// The upper middle point of the actor: the top of the body in
			/// the middle of the model.
			RE::NiPoint3 position;

			/// The radius of the head of the actor, in world units.
			float radius = 0.0f;
		};

		/// Resolves the upper middle point of an actor.
		///
		/// The bound of the model is a sphere around the whole model, so its
		/// center is the middle of the actor and the top of that sphere is
		/// the top of the body.
		///
		/// The head bone of the skeleton is not used anymore: it follows
		/// every animation of the actor, while the bound of the model covers
		/// the whole body and stays where the body is.
		BodyPoint ResolveBodyPoint(RE::Actor* a_actor, RE::NiAVObject* a_model)
		{
			BodyPoint result;

			const auto& bound = a_model->worldBound;
			const float radius = bound.radius;

			if (radius > 0.05f && radius < 100.0f) {
				result.position = bound.center;
				result.position.z += radius;

				// The size of the head is estimated from the bound of the
				// model, because the bound of the model covers the whole
				// body.
				result.radius = radius * kHeadToBodyRatio;

				return result;
			}

			// The bound of the model is not usable, so the actor is estimated
			// from its own position, which is the point between its feet.
			result.position = a_actor->GetPosition();
			result.position.z += kFallbackBodyHeight;
			result.radius = kFallbackHeadRadius;

			return result;
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
		/// has no point that is visible on the screen.
		bool BuildMarker(RE::Actor* a_actor, RE::NiCamera* a_camera, Marker& a_marker)
		{
			// Actors without a model have nothing to draw. The model is also
			// the anchor for every following access.
			auto* model = a_actor->Get3D();
			if (!model) {
				return false;
			}

			const BodyPoint body = ResolveBodyPoint(a_actor, model);

			if (body.radius <= 0.0f) {
				return false;
			}

			float x = 0.0f;
			float y = 0.0f;

			if (!ProjectToScreen(a_camera, body.position, x, y)) {
				return false;
			}

			// The size of the head on the screen is measured with the very
			// same projection: a probe point one head radius above the point
			// of the body is projected as well and the distance between both
			// points is the radius of the head in screen fractions.
			const auto cameraPosition = a_camera->world.translate;

			RE::NiPoint3 forward = body.position - cameraPosition;
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

			if (!ProjectToScreen(a_camera, body.position + up * body.radius, probeX, probeY)) {
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

		/// True when the actor is an NPC that can carry a marker.
		bool IsTrackable(RE::PlayerCharacter *player, RE::Actor* a_actor)
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

	bool HasClearLineOfSight(RE::Actor* a_target) 
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !camera || !a_target) {
				return false;
		}

		auto* cell = player->GetParentCell();
		if (!cell) {
				return false;
		}

		auto* havokWorld = cell->GetbhkWorld();
		if (!havokWorld) {
				return false;
		}

		// 1. Начальная точка луча — позиция камеры игрока
		RE::NiPoint3 startPos = camera->cameraRoot ? camera->cameraRoot->world.translate : player->GetLookingAtLocation();
		
		// 2. Конечная точка луча — центр туловища или голова цели
		RE::NiPoint3 endPos = a_target->GetPosition();
		endPos.z += a_target->GetHeight() * 0.95f; // Примерно уровень груди/шеи

		RE::bhkPickData pickData;
		
		// Переводим координаты из игровых единиц в масштаб Havok (1 игровой юнит = ~0.01428 в Havok)
		const float havokScale = RE::bhkWorld::GetWorldScale();
		pickData.rayInput.from = startPos * havokScale;
		pickData.rayInput.to = endPos * havokScale;

		// Настраиваем фильтр коллизий: луч должен реагировать на статику (стены), ландшафт и акторов
		// Коллизия от лица игрока, чтобы луч не застрял в собственной модели игрока
		uint32_t collisionGroup = 0;
		player->GetCollisionFilterInfo(collisionGroup);
		// Устанавливаем слой L_LOS (Line of Sight = 13) или L_PROJECTILE (10)
		pickData.rayInput.filterInfo = (collisionGroup & 0xFFFF0000) | static_cast<uint32_t>(RE::COL_LAYER::kLOS);

		// Пускаем луч в физическом мире
		havokWorld->PickObject(pickData);

		// 3. Анализируем, во что уперся луч
		if (pickData.rayOutput.HasHit()) {
				// Получаем объект, в который попал луч
				auto* hitRef = RE::TESHavokUtilities::FindCollidableRef(*pickData.rayOutput.rootCollidable);
				
				// Если луч попал именно в нашего целевого NPC — препятствий нет!
				if (hitRef == a_target) {
						return true;
				}
				
				// Если hitRef == nullptr (стена, камень, ландшафт) или другой объект — цель за преградой
				return false;
		}

		// Если луч вообще ни во что не попал (например, пролетел мимо хитбокса NPC, но и стен не задел)
		return true;
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

			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* camera = ResolveCamera();
			auto* processLists = RE::ProcessLists::GetSingleton();

			if (!player || !camera || !processLists) {
				return;
			}

			std::unordered_set<Damage::ActorId> visible;

			for (auto& actorHandle : processLists->highActorHandles) {
        if (auto actor = actorHandle.get()) {
					try {
							RE::Actor* a_actor = actor.get();

							if (!IsTrackable(player, a_actor) || !HasClearLineOfSight(a_actor)) continue;

							Marker marker;

							if (BuildMarker(a_actor, camera, marker)) {
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
        }
    	}

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
