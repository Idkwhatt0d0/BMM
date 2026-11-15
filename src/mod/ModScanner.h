#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace bmm {

enum class ModKind { Native, PreloadNative, Other };

struct ModEntry {
    std::string name, version, description, license, entry, typeRaw, state;
    std::vector<std::string> authors, dependencies;
    std::filesystem::path folder;
    ModKind kind = ModKind::Native;
    bool enabled   = true;  // manifest.json present (false = manifest.json.close)
    bool isLibrary = false;
    std::string parent;     // empty = top level
};

struct ScanResult {
    std::vector<ModEntry> mods;
    int native = 0, preload = 0, other = 0, libraries = 0, disabled = 0;
};

// modsDir = <instance>\mods
ScanResult scanMods(const std::filesystem::path& modsDir);

// JSON for the page: {"counts":{...},"mods":[...]}. Descriptions are truncated to keep it small.
std::string toJson(const ScanResult& r);

} // namespace bmm
