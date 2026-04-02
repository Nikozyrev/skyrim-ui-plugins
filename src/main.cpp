#include "PrismaUI_API.h"
#include <keyhandler/keyhandler.h>
#include <nlohmann/json.hpp>

using JSON = nlohmann::json;

auto viewUrl = "Alchemy UI/index.html";

PRISMA_UI_API::IVPrismaUI1* PrismaUI;
static PrismaView view;

class OnWeaponHit
{
public:
    static void Hook()
    {
        _weapon_hit = SKSE::GetTrampoline().write_call<5>(REL::ID(37673).address() + 0x3C0, weapon_hit);
    }

private:
    static void weapon_hit(RE::Actor* target, RE::HitData& hit_data) {
        // Проверяем, что цель жива и удар актуален
        if (hit_data.aggressor && target &&
            !target->IsDead() && // Цель должна быть жива
            (target->IsPlayerRef() || hit_data.aggressor.get()->IsPlayerRef())) {

            std::string attackerName = hit_data.aggressor.get()->GetDisplayFullName();
            std::string targetName = target->GetDisplayFullName();
            int damage = static_cast<int>(hit_data.totalDamage);

            logger::info("Attacker {}; Target {}; Damage {}", attackerName, targetName, damage);
            std::string script = std::format("onHitEvent('{}', '{}', {})", attackerName, targetName, damage);
            PrismaUI->Invoke(view, script.c_str());
        }
        return _weapon_hit(target, hit_data);
    }

    static inline REL::Relocation<decltype(weapon_hit)> _weapon_hit;
};


// class HitSink: public RE::BSTEventSink<RE::TESHitEvent> {

    

//     RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) {
//         if (!event->target) return RE::BSEventNotifyControl::kContinue;
//         auto targetName = event->target.get()->GetBaseObject()->GetName();
//         auto source = event->source;
//         logger::info("Hit target {} with source {}", targetName, source);
//         std::string script = std::format("onHitEvent('{}', {})", targetName, source);
//         PrismaUI->Invoke(view, script.c_str());
//         return RE::BSEventNotifyControl::kContinue;
//     }
// };

// auto* hitSink = new HitSink();

struct IngredientData {
    uint32_t id;          // Уникальный ID предмета
    std::string name;       // Название
    int32_t count;          // Количество
};

std::vector<IngredientData> GetAllPlayerIngredients() {
    std::vector<IngredientData> ingredients;

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return ingredients;

    // Получаем данные об изменениях в контейнере (инвентаре)
    auto containerChanges = player->GetInventoryChanges();
    if (!containerChanges || !containerChanges->entryList) return ingredients;

    auto objList = containerChanges->entryList;
    if (!objList) return ingredients;

    for (auto* entry : *objList) {
        if (!entry || !entry->object) continue;

        // Проверяем, является ли предмет ингредиентом
        if (entry->object->Is(RE::FormType::Ingredient)) {
            auto* ingredient = entry->object->As<RE::IngredientItem>();
            int32_t count = entry->countDelta; 
            
            if (count > 0) {
                IngredientData data;
                data.id = ingredient->GetFormID();
                data.name = ingredient->GetName();
                data.count = entry->countDelta;
                ingredients.push_back(data);
            }
        }
    }

    return ingredients;
}

// Описываем, как структура превращается в JSON объект
void to_json(JSON& j, const IngredientData& i) {
    j = JSON {
        {"id", i.id},
        {"name", i.name},
        {"count", i.count}
    };
}


static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
    switch (message->type) {
    case SKSE::MessagingInterface::kDataLoaded:
        PrismaUI = static_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));

        view = PrismaUI->CreateView(viewUrl);
        
        PrismaUI->RegisterJSListener(view, "getPlayerIngredients", [](const char* data) -> void {
            const auto ingredients = GetAllPlayerIngredients();
            JSON payload = { 
                { "ingredients", ingredients },
            };
            const auto callCommandString = std::format("SKSE_API.call('{}', {})", "ui/ingredients/set", payload.dump());
    		PrismaUI->Invoke(view, callCommandString.c_str());
        });

        // OnWeaponHit::Hook();

        // RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(hitSink);

        // KeyHandler::RegisterSink();
        // KeyHandler* keyHandler = KeyHandler::GetSingleton();
        // const uint32_t TOGGLE_FOCUS_KEY = 0x3D; // F3 key
        // KeyHandlerEvent toggleEventHandler = keyHandler->Register(TOGGLE_FOCUS_KEY, KeyEventType::KEY_DOWN, []() {});
        break;
    }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    REL::Module::reset();

    auto g_messaging = reinterpret_cast<SKSE::MessagingInterface*>(a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));

    if (!g_messaging) {
        logger::critical("Failed to load messaging interface! This error is fatal, plugin will not load.");
        return false;
    }

    SKSE::Init(a_skse);
    SKSE::AllocTrampoline(1 << 10);

    g_messaging->RegisterListener("SKSE", SKSEMessageHandler);

    return true;
}
