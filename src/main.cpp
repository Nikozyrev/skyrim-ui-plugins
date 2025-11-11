#include "PrismaUI_API.h"
#include <keyhandler/keyhandler.h>

auto viewUrl = "PrismaUI-Example-UI/index.html";

PRISMA_UI_API::IVPrismaUI1* PrismaUI;
static PrismaView view;

class HitSink: public RE::BSTEventSink<RE::TESHitEvent> {

    

    RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) {
        if (!event->target) return RE::BSEventNotifyControl::kContinue;
        auto targetName = event->target.get()->GetBaseObject()->GetName();
        auto source = event->source;
        logger::info("Hit target {} with source {}", targetName, source);
        std::string script = std::format("onHitEvent('{}', {})", targetName, source);
        PrismaUI->Invoke(view, script.c_str());
        return RE::BSEventNotifyControl::kContinue;
    }
};

auto* hitSink = new HitSink();


static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
    switch (message->type) {
    case SKSE::MessagingInterface::kDataLoaded:
        PrismaUI = static_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));

        view = PrismaUI->CreateView(viewUrl);

        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(hitSink);

        // PrismaUI->RegisterJSListener(view, "sendDataToSKSE", [](const char* data) -> void {
        //     logger::info("Received data from JS: {}", data);
        // });

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
