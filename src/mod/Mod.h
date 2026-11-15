#pragma once

#include "ll/api/mod/NativeMod.h"

#include "api/IDearOreUIApi.h"
#include "api/types/Id.h"
#include "api/types/Page.h"

#include <atomic>
#include <filesystem>
#include <optional>
#include <thread>
#include <vector>

namespace bmm {

class Mod {
public:
    static Mod& getInstance();

    Mod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    // <instance>\mods  (BMM's own folder is <instance>\mods\BMM)
    [[nodiscard]] std::filesystem::path modsDir() const { return mSelf.getModDir().parent_path(); }

    bool load();
    bool enable();
    bool disable();

private:
    bool connect();
    bool registerAll();
    void unregisterAll();
    std::vector<dearoreui::api::PageScope> readScopes() const;

    ll::mod::NativeMod& mSelf;

    // Owned by DearOreUI; never deleted here. Written only on the client main thread.
    dearoreui::api::IDearOreUIApi* mOreui{nullptr};
    dearoreui::api::ModId          mModId;
    std::optional<dearoreui::api::RegistrationHandle> mUi, mHost;

    std::atomic<bool> mRetryStop{false};
    std::jthread      mRetryThread;
};

} // namespace bmm
