#include "mod/Mod.h"
#include "mod/ModScanner.h"

#include "bridge/DearOreUIBridge.h"

#include "api/IHostMethod.h"
#include "api/manifest/UiManifest.h"
#include "api/types/ComponentSpec.h"
#include "api/types/DomNode.h"
#include "api/types/HostMethodManifest.h"

#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/thread/ClientThreadExecutor.h"
#include "ll/api/thread/InterruptableSleep.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace bmm {
namespace {

DearOreUIBridgeResult (*queryApi)(uint32_t) = nullptr;

bool loadBridge() {
#ifdef _WIN32
    HMODULE module = GetModuleHandleW(L"DearOreUI.dll");
    if (module == nullptr) return false;
    queryApi = reinterpret_cast<DearOreUIBridgeResult (*)(uint32_t)>(GetProcAddress(module, "DearOreUI_QueryApi"));
    return queryApi != nullptr;
#else
    return false;
#endif
}

constexpr int                       kMaxRetryPolls = 20;
constexpr std::chrono::milliseconds kRetryInterval{500};

std::string loadScript(std::filesystem::path const& modDir, char const* name) {
    std::ifstream in(modDir / "scripts" / name, std::ios::binary);
    if (!in) return {};
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// JS -> C++: returns the current mod list. Rescans on every call (cheap; one call per page view).
class ListMethod final : public dearoreui::api::IHostMethod {
public:
    [[nodiscard]] std::string name() const override { return "bmm.list"; }
    [[nodiscard]] dearoreui::api::Permission requiredPermission() const override {
        return dearoreui::api::Permission::HostReadOnly;
    }
    [[nodiscard]] dearoreui::api::Result<std::string> execute(dearoreui::api::ContextId, std::string_view) override {
        auto scan = scanMods(Mod::getInstance().modsDir());
        auto& reg = ll::mod::ModManagerRegistry::getInstance();
        for (auto& m : scan.mods) {
            if (!m.enabled) { m.state = "Disabled (restart to apply)"; continue; }
            auto mod = reg.getMod(m.name);
            m.state  = !mod ? "Not loaded" : (mod->isEnabled() ? "Enabled" : "Loaded, not enabled");
        }
        return dearoreui::api::Result<std::string>::success(toJson(scan));
    }
};

} // namespace

Mod& Mod::getInstance() {
    static Mod instance;
    return instance;
}

bool Mod::load() { return true; }

// config.json in the mod folder: {"scopes":["any"]}. Names: any, main_menu, play, settings, pause, in_game, custom.
// Default is "any" so the button shows on every OreUI page DearOreUI can reach; narrow it once you know
// which scope the title screen reports.
std::vector<dearoreui::api::PageScope> Mod::readScopes() const {
    using dearoreui::api::PageScope;
    std::vector<PageScope> out;
    try {
        std::ifstream in(mSelf.getModDir() / "config.json");
        if (in) {
            auto j = nlohmann::json::parse(in, nullptr, true, true);
            if (j.contains("scopes") && j["scopes"].is_array())
                for (auto& s : j["scopes"]) {
                    auto n = s.is_string() ? s.get<std::string>() : std::string{};
                    if (n == "any") out.push_back(PageScope::Any);
                    else if (n == "main_menu") out.push_back(PageScope::MainMenu);
                    else if (n == "play") out.push_back(PageScope::PlayScreen);
                    else if (n == "settings") out.push_back(PageScope::Settings);
                    else if (n == "pause") out.push_back(PageScope::Pause);
                    else if (n == "in_game") out.push_back(PageScope::InGame);
                    else if (n == "custom") out.push_back(PageScope::Custom);
                }
        }
    } catch (...) {
    }
    if (out.empty()) out.push_back(PageScope::Any);
    return out;
}

bool Mod::registerAll() {
    using namespace dearoreui::api;
    auto& log    = mSelf.getLogger();
    auto  scopes = readScopes();

    ModManifest manifest;
    manifest.id           = ModId{"bmm"};
    manifest.modNamespace = "bmm";
    manifest.displayName  = "BMM (Bedrock Mod Menu)";
    manifest.modVersion   = Version{0, 1, 0};
    manifest.permissions  = {Permission::HostReadOnly, Permission::PageObserve, Permission::UiMount};
    auto registered = mOreui->registerMod(manifest);
    if (registered.isErr()) {
        log.error("registerMod failed: {}", registered.error().message);
        return false;
    }
    mModId = manifest.id;

    std::string script = loadScript(mSelf.getModDir(), "bmm_ui.js");
    if (script.empty()) {
        log.error("scripts/bmm_ui.js not found next to the DLL");
        static_cast<void>(mOreui->unregisterMod(mModId));
        return false;
    }

    UiManifest ui;
    ui.modNamespace  = manifest.modNamespace;
    ui.id            = "mods_menu";
    ui.kind          = UiKind::Overlay;
    ui.pageScopes    = scopes;
    ui.anchor        = UiAnchor::FullScreen;
    ui.pointerEvents = true;
    ui.containerId   = makeUiContainerId(ui.modNamespace, ui.kind, ui.id);
    ui.fingerprint   = "bmm.ui.v1";

    std::vector<DomNode> body;
    // The root is click-through; the script gives only the button and the panel pointer-events.
    body.push_back(DomNode{.tag   = "div",
                           .attrs = {{"id", "bmm-root"}},
                           .style = "position:fixed;top:0;left:0;right:0;bottom:0;overflow:hidden;pointer-events:none;",
                           .text  = ""});
    body.push_back(DomNode{.tag = "script", .text = std::move(script)});

    ComponentSpec root;
    root.kind = ComponentKind::Section;
    root.body = std::move(body);

    auto uiResult = mOreui->registerComponent(mModId, ui, root);
    if (uiResult.isErr()) {
        log.error("registerComponent failed: {}", uiResult.error().message);
        static_cast<void>(mOreui->unregisterMod(mModId));
        return false;
    }
    mUi = uiResult.value();

    HostMethodManifest host;
    host.name             = "bmm.list";
    host.pageScopes       = scopes;
    host.permissions      = PermissionSet{std::vector{Permission::HostReadOnly}};
    host.maxResponseBytes = 512 * 1024;
    auto hostResult = mOreui->registerHostMethod(mModId, host, std::make_shared<ListMethod>());
    if (hostResult.isErr()) {
        log.error("registerHostMethod failed: {}", hostResult.error().message);
        static_cast<void>(mOreui->unregisterUi(*mUi));
        mUi.reset();
        static_cast<void>(mOreui->unregisterMod(mModId));
        return false;
    }
    mHost = hostResult.value();

    log.info("BMM registered with DearOreUI");
    return true;
}

void Mod::unregisterAll() {
    if (!mOreui) return;
    if (mHost) { static_cast<void>(mOreui->unregisterHostMethod(*mHost)); mHost.reset(); }
    if (mUi)   { static_cast<void>(mOreui->unregisterUi(*mUi)); mUi.reset(); }
    static_cast<void>(mOreui->unregisterMod(mModId));
    mModId = dearoreui::api::ModId{};
    mOreui = nullptr;
}

bool Mod::connect() {
    auto& log = mSelf.getLogger();
    if (queryApi == nullptr && !loadBridge()) {
        log.warn("DearOreUI.dll is not loaded; BMM has nothing to attach to.");
        return false;
    }
    auto bridge = queryApi(1);
    if (bridge.status == DearOreUIBridge_Ok) {
        mOreui = static_cast<dearoreui::api::IDearOreUIApi*>(bridge.api);
        if (!registerAll()) { mOreui = nullptr; return false; }
        return true;
    }
    if (bridge.status == DearOreUIBridge_VersionMismatch) {
        log.error("DearOreUI protocol mismatch: BMM expects 1, DearOreUI reports {}", bridge.protocolVersion);
        return false;
    }
    log.warn("DearOreUI is not ready yet; retrying for up to 10 s.");
    mRetryStop   = false;
    mRetryThread = std::jthread([this](std::stop_token stop) {
        ll::thread::InterruptableSleep sleeper;
        for (int polls = 0; polls < kMaxRetryPolls && !stop.stop_requested() && !mRetryStop.load(); ++polls) {
            auto candidate = queryApi(1);
            if (candidate.status == DearOreUIBridge_Ok) {
                void* apiPtr = candidate.api;
                ll::thread::ClientThreadExecutor::getDefault().execute([this, apiPtr]() {
                    if (mRetryStop.load()) return;
                    mOreui = static_cast<dearoreui::api::IDearOreUIApi*>(apiPtr);
                    if (!registerAll()) mOreui = nullptr;
                });
                return;
            }
            sleeper.sleepFor(kRetryInterval);
        }
        mSelf.getLogger().warn("DearOreUI did not become ready in time.");
    });
    return false;
}

bool Mod::enable() {
    connect();
    return true; // stay enabled even if DearOreUI is late; the retry loop attaches later
}

bool Mod::disable() {
    mRetryStop = true;
    if (mRetryThread.joinable()) mRetryThread.join();
    unregisterAll();
    return true;
}

} // namespace bmm

LL_REGISTER_MOD(bmm::Mod, bmm::Mod::getInstance());
