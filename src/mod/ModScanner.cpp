#include "mod/ModScanner.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace bmm {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string str(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

bool looksLikeLibrary(const ModEntry& m, const nlohmann::json& j) {
    auto extra = j.find("extraInfo");
    if (extra != j.end() && extra->is_object()) {
        auto lib = extra->find("library");
        if (lib != extra->end() && lib->is_boolean()) return lib->get<bool>();
    }
    std::string n = lower(m.name);
    return n == "levilamina" || n.ends_with("lib") || n.ends_with("library") || n.ends_with("api")
        || n.starts_with("lib");
}

} // namespace

ScanResult scanMods(const std::filesystem::path& modsDir) {
    ScanResult r;
    std::error_code ec;
    if (!std::filesystem::is_directory(modsDir, ec)) return r;

    for (auto& dir : std::filesystem::directory_iterator(modsDir, ec)) {
        if (!dir.is_directory(ec)) continue;

        auto manifest = dir.path() / "manifest.json";
        bool enabled  = true;
        if (!std::filesystem::exists(manifest, ec)) {
            manifest = dir.path() / "manifest.json.close";
            enabled  = false;
            if (!std::filesystem::exists(manifest, ec)) continue; // not a mod folder
        }

        nlohmann::json j;
        try {
            std::ifstream f(manifest);
            j = nlohmann::json::parse(f, nullptr, true, /*ignore_comments*/ true);
        } catch (...) {
            continue; // malformed manifest: skip rather than crash the game
        }
        if (!j.is_object()) continue;

        ModEntry m;
        m.folder      = dir.path();
        m.enabled     = enabled;
        m.name        = str(j, "name");
        if (m.name.empty()) m.name = dir.path().filename().string();
        m.version     = str(j, "version");
        m.description = str(j, "description");
        m.license     = str(j, "license");
        m.entry       = str(j, "entry");
        m.typeRaw     = str(j, "type");

        if (m.typeRaw == "native")              m.kind = ModKind::Native;
        else if (m.typeRaw == "preload-native") m.kind = ModKind::PreloadNative;
        else                                    m.kind = ModKind::Other;

        if (j.contains("author") && j["author"].is_string()) m.authors.push_back(j["author"].get<std::string>());
        if (j.contains("authors") && j["authors"].is_array())
            for (auto& a : j["authors"]) if (a.is_string()) m.authors.push_back(a.get<std::string>());

        if (j.contains("dependencies") && j["dependencies"].is_array())
            for (auto& d : j["dependencies"]) {
                if (d.is_object()) m.dependencies.push_back(str(d, "name"));
                else if (d.is_string()) m.dependencies.push_back(d.get<std::string>());
            }

        m.isLibrary = looksLikeLibrary(m, j);
        if (j.contains("extraInfo") && j["extraInfo"].is_object()) m.parent = str(j["extraInfo"], "parent");

        r.mods.push_back(std::move(m));
    }

    // Implicit parent/child: "GMLIB-LegacyRemoteCallApi" is a child of "GMLIB" if both are installed.
    for (auto& child : r.mods) {
        if (!child.parent.empty()) continue;
        for (auto& p : r.mods) {
            if (&p == &child) continue;
            if (child.name.size() > p.name.size() + 1 && child.name.starts_with(p.name)
                && child.name[p.name.size()] == '-') {
                child.parent = p.name;
                break;
            }
        }
    }

    std::sort(r.mods.begin(), r.mods.end(), [](const ModEntry& a, const ModEntry& b) {
        return lower(a.name) < lower(b.name);
    });

    for (auto& m : r.mods) {
        switch (m.kind) {
        case ModKind::Native:        ++r.native;  break;
        case ModKind::PreloadNative: ++r.preload; break;
        default:                     ++r.other;   break;
        }
        if (m.isLibrary) ++r.libraries;
        if (!m.enabled) ++r.disabled;
    }
    return r;
}

} // namespace bmm
