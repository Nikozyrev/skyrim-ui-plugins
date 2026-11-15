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
		/// The height of a human NPC in world units.
		///
		/// The world units of the game are the units of the creation kit:
		/// 128 units are 1.828 metres, so a human is 128 units high. The
		/// height of a race is a scale factor and not a length, so the
		/// height of every other size is a multiple of this one.
		constexpr float kBaseBodyHeight = 128.0f;

		/// The radius of the head as a fraction of the height of the body.
		///
		/// The number of the damage is scaled with the size of the head of
		/// the NPC, so it stays readable for every size of an actor.
		constexpr float kHeadRadiusPerHeight = 0.08f;

		/// The bounds of the height of an actor that are still plausible.
		///
		/// A height outside of them means that the engine did not measure
		/// the actor, so the height of its race is used instead. The bounds
		/// are in world units: 40 units are 0.57 metres and 400 units are
		/// 5.7 metres.
		constexpr float kMinBodyHeight = 40.0f;
		constexpr float kMaxBodyHeight = 400.0f;

		/// The distance between the position of an actor and the root of its
		/// model that is still plausible, in world units.
		///
		/// The root of the model follows the actor while it walks, so it is
		/// the better source for the point of the marker. It is only used
		/// when it is close to the position of the actor though, so a model
		/// that is not in the world cannot move the marker.
		constexpr float kMaxRootDistance = 100.0f;

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
			/// The point above the actor: the feet of the actor plus the
			/// height of its body.
			RE::NiPoint3 position;

			/// The radius of the head of the actor, in world units.
			float radius = 0.0f;
		};

		/// The point between the feet of an actor.
		///
		/// The position of the actor is the point between its feet and the
		/// root of its model follows the actor while it walks. The root is
		/// only used when it is close to the position of the actor, so a
		/// model that is not in the world cannot move the marker.
		RE::NiPoint3 ResolveBase(RE::Actor* a_actor, RE::NiAVObject* a_model)
		{
			const RE::NiPoint3 position = a_actor->GetPosition();

			if (!a_model) {
				return position;
			}

			const RE::NiPoint3 root = a_model->world.translate;
			const RE::NiPoint3 delta = root - position;

			if (delta.SqrLength() > kMaxRootDistance * kMaxRootDistance) {
				return position;
			}

			return root;
		}

		/// Resolves the point above an actor.
		///
		/// The point is built from the position and the height of the actor
		/// and not from the model of the actor, so it does not follow the
		/// animations of the NPC: a swing of a weapon or a crouch does not
		/// move it and it never leaves the screen.
		BodyPoint ResolveBodyPoint(RE::Actor* a_actor, RE::NiAVObject* a_model, float a_height)
		{
			BodyPoint result;

			result.position = ResolveBase(a_actor, a_model);
			result.position.z += a_height;

			// The size of the head is a part of the size of the body, so the
			// number of the damage keeps its size for every NPC.
			result.radius = a_height * kHeadRadiusPerHeight;

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
		bool BuildMarker(RE::Actor* a_actor, RE::NiCamera* a_camera, float a_height, Marker& a_marker)
		{
			// Actors without a model have nothing to draw. The model is also
			// the anchor of the point of the marker.
			auto* model = a_actor->Get3D();
			if (!model) {
				return false;
			}

			const BodyPoint body = ResolveBodyPoint(a_actor, model, a_height);

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
		// Высота берётся из трекера: она одна и та же для NPC и не зависит
		// от его анимации, поэтому луч всегда целится в одно и то же место.
		RE::NiPoint3 endPos = a_target->GetPosition();
		endPos.z += Tracker::Get().ResolveHeight(a_target, a_target->GetFormID()) * 0.95f; // Примерно уровень груди/шеи
	
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

			// Оружие и щит NPC — это отдельные объекты, поэтому луч может попасть
			// в них вместо самого NPC. Владелец такого объекта — это и есть NPC,
			// значит перед ним ничего не стоит.
			if (hitRef && hitRef->GetOwner() == a_target) {
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

	float Tracker::ResolveHeight(RE::Actor* a_actor, Damage::ActorId a_id)
	{
		if (const auto it = _heights.find(a_id); it != _heights.end()) {
			return it->second;
		}

		// The engine measures the height of an actor from the bound of its
		// model, and that bound follows every animation of the NPC. The
		// engine caches the height in the high process of the actor, so it
		// stays the same while the actor is in the high process.
		float height = a_actor->GetHeight();

		// The height of the race of the NPC is a scale factor that never
		// changes, so it is the fallback when the height of the engine is
		// not usable.
		float scale = 0.0f;

		if (auto* npc = a_actor->GetActorBase(); npc) {
			scale = npc->GetHeight();
		}

		if (!(height > kMinBodyHeight && height < kMaxBodyHeight)) {
			height = scale > 0.0f ? scale * kBaseBodyHeight : kBaseBodyHeight;
		}

		// The height is kept for the whole session, so the point of the
		// marker of an NPC never moves while the NPC is on the screen.
		_heights.emplace(a_id, height);

		return height;
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

							if (!IsTrackable(a_actor) || !HasClearLineOfSight(a_actor)) continue;

							Marker marker;

							// The height of the actor is measured once and then kept, so the
							// point of the marker does not follow the animations of the NPC.
							const float height = ResolveHeight(a_actor, a_actor->GetFormID());

							if (BuildMarker(a_actor, camera, height, marker)) {
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
