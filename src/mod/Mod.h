#pragma once
#include "ll/api/mod/NativeMod.h"
#include <filesystem>

namespace bmm {
class Mod {
public:
    static Mod& getInstance();
    Mod() : mSelf(*ll::mod::NativeMod::current()) {}
    ll::mod::NativeMod& getSelf() const { return mSelf; }
    // <instance>\mods  (BMM's own dir is <instance>\mods\BMM)
    std::filesystem::path modsDir() const { return mSelf.getModDir().parent_path(); }
    bool load();
    bool enable();
    bool disable();
private:
    ll::mod::NativeMod& mSelf;
};
} // namespace bmm
