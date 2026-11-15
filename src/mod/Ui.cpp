#include "mod/Ui.h"
#include "mod/Mod.h"
#include "mod/ModScanner.h"
#include "mod/Catalogue.h"

#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <imgui.h>

#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/service/Bedrock.h"

namespace bmm::ui {
namespace {

bool       g_open = false;
bool       g_scanned = false;
ScanResult g_scan;
int        g_sel = -1;
char       g_search[128] = "";
int        g_sort = 0; // 0 name, 1 type
bool       g_showLibs = false;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// HEURISTIC: no Level => title screen. Loading screens also count as "title" (untested on client).
bool onTitleScreen() {
    return !ll::service::getLevel().has_value();
}

void rescan() {
    g_scan    = scanMods(Mod::getInstance().modsDir());
    g_scanned = true;
    g_sel     = -1;
}

void badge(const char* t, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(), ts = ImGui::CalcTextSize(t);
    ImVec2 q(p.x + ts.x + 10, p.y + ts.y + 2);
    dl->AddRectFilled(p, q, col, 3.f);
    dl->AddText(ImVec2(p.x + 5, p.y + 1), IM_COL32(255, 255, 255, 255), t);
    ImGui::Dummy(ImVec2(q.x - p.x, q.y - p.y));
}

const char* loadState(const ModEntry& m) {
    if (!m.enabled) return "Disabled (restart to apply)";
    auto mod = ll::mod::ModManagerRegistry::getInstance().getMod(m.name);
    if (!mod) return "Not loaded";
    return mod->isEnabled() ? "Enabled" : "Loaded, not enabled";
}

void titleButton() {
    ImGuiIO& io = ImGui::GetIO();
    // Position measured from a 1456x819 title-screen screenshot: slot right of "Profile".
    ImVec2 pos(io.DisplaySize.x * 0.163f, io.DisplaySize.y * 0.828f);
    ImVec2 size(io.DisplaySize.x * 0.095f, io.DisplaySize.y * 0.054f);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##bmm_btn", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::InvisibleButton("##mods", size);
    bool hov = ImGui::IsItemHovered(), down = ImGui::IsItemActive();
    if (ImGui::IsItemClicked()) { rescan(); g_open = true; }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = pos, b(pos.x + size.x, pos.y + size.y);
    float u = std::max(2.f, size.y / 18.f); // bevel unit scales with button size
    ImU32 face = down ? IM_COL32(150, 151, 154, 255) : hov ? IM_COL32(200, 201, 205, 255) : IM_COL32(177, 178, 181, 255);
    dl->AddRectFilled(a, b, IM_COL32(30, 30, 31, 255));
    dl->AddRectFilled(ImVec2(a.x + u, a.y + u), ImVec2(b.x - u, b.y - u), face);
    dl->AddRectFilled(ImVec2(a.x + u, a.y + u), ImVec2(b.x - u, a.y + 2 * u), IM_COL32(236, 237, 239, 255));
    dl->AddRectFilled(ImVec2(a.x + u, b.y - 2 * u), ImVec2(b.x - u, b.y - u), IM_COL32(88, 88, 90, 255));

    if (!g_scanned) rescan();
    std::string label = "Mods (" + std::to_string(g_scan.mods.size()) + ")";
    float fs = size.y * 0.42f;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, label.c_str());
    dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + (size.x - ts.x) / 2, a.y + (size.y - ts.y) / 2 - (down ? 0 : u / 2)),
                IM_COL32(60, 60, 62, 255), label.c_str());
    ImGui::End();
    ImGui::PopStyleVar(2);
}

bool matches(const ModEntry& m, const std::string& q) {
    if (q.empty()) return true;
    if (lower(m.name).find(q) != std::string::npos) return true;
    if (lower(m.description).find(q) != std::string::npos) return true;
    for (auto& a : m.authors) if (lower(a).find(q) != std::string::npos) return true;
    return false;
}

void listRow(int i) {
    auto& m = g_scan.mods[i];
    ImGui::PushID(i);
    std::string t = m.name + "  " + m.version;
    if (ImGui::Selectable(t.c_str(), g_sel == i)) { g_sel = i; catalogue::requestAsync(m); }
    ImGui::SameLine(); 
    if (m.kind == ModKind::PreloadNative) { badge("Preload", IM_COL32(150, 90, 30, 255)); ImGui::SameLine(); }
    if (m.kind == ModKind::Other)         { badge(m.typeRaw.c_str(), IM_COL32(90, 90, 90, 255)); ImGui::SameLine(); }
    if (m.isLibrary)                      { badge("Library", IM_COL32(60, 90, 150, 255)); ImGui::SameLine(); }
    if (!m.enabled)                       { badge("Disabled", IM_COL32(150, 50, 50, 255)); }
    ImGui::NewLine();
    ImGui::PopID();
}

void list() {
    std::string q = lower(g_search);
    std::vector<int> idx;
    for (int i = 0; i < (int)g_scan.mods.size(); ++i) {
        auto& m = g_scan.mods[i];
        if (m.isLibrary && !g_showLibs) continue;
        if (!matches(m, q)) continue;
        idx.push_back(i);
    }
    if (g_sort == 1)
        std::stable_sort(idx.begin(), idx.end(), [](int a, int b) {
            return (int)g_scan.mods[a].kind < (int)g_scan.mods[b].kind;
        });

    bool flat = !q.empty(); // search results are flat
    for (int i : idx) {
        auto& m = g_scan.mods[i];
        if (!flat && !m.parent.empty()) {
            // child of an installed parent that is itself shown -> drawn under it below
            bool parentShown = std::any_of(idx.begin(), idx.end(), [&](int p) { return g_scan.mods[p].name == m.parent; });
            if (parentShown) continue;
        }
        listRow(i);
        if (!flat) {
            ImGui::Indent(18.f);
            for (int c : idx) if (g_scan.mods[c].parent == m.name) listRow(c);
            ImGui::Unindent(18.f);
        }
    }
}

void details() {
    if (g_sel < 0 || g_sel >= (int)g_scan.mods.size()) { ImGui::TextDisabled("Select a mod"); return; }
    auto& m = g_scan.mods[g_sel];
    ImGui::SetWindowFontScale(1.4f); ImGui::TextUnformatted(m.name.c_str()); ImGui::SetWindowFontScale(1.f);
    ImGui::Text("Version: %s", m.version.empty() ? "unknown" : m.version.c_str());
    std::string au;
    for (auto& a : m.authors) au += (au.empty() ? "" : ", ") + a;
    ImGui::Text("Authors: %s", au.empty() ? "unknown" : au.c_str());
    ImGui::Text("Type: %s", m.typeRaw.c_str());
    ImGui::Text("State: %s", loadState(m));
    const std::string& lic = !m.license.empty() ? m.license : m.catLicense;
    ImGui::Text("License: %s", lic.empty() ? "unknown" : lic.c_str());
    if (!m.dependencies.empty()) {
        std::string d;
        for (auto& x : m.dependencies) d += (d.empty() ? "" : ", ") + x;
        ImGui::TextWrapped("Depends on: %s", d.c_str());
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", m.description.empty() ? "No description." : m.description.c_str());
    if (!m.catReadme.empty()) { ImGui::Separator(); ImGui::TextWrapped("%s", m.catReadme.c_str()); }
    ImGui::Separator();
    ImGui::TextDisabled("%s", m.folder.string().c_str());
    if (ImGui::Button("Open folder")) ShellExecuteW(nullptr, L"open", m.folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (!m.catUrl.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("LeviMC page"))
            ShellExecuteA(nullptr, "open", m.catUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void menu() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 sz(io.DisplaySize.x * 0.72f, io.DisplaySize.y * 0.78f);
    ImGui::SetNextWindowSize(sz, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2), ImGuiCond_Always, ImVec2(.5f, .5f));
    if (!ImGui::Begin("Mods##bmm", &g_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::End(); return;
    }
    ImGui::Text("%d mods installed  (%d native, %d preload-native%s%s)", (int)g_scan.mods.size(), g_scan.native,
                g_scan.preload, g_scan.other ? ", other" : "", g_scan.disabled ? ", some disabled" : "");
    if (!g_showLibs && g_scan.libraries) { ImGui::SameLine(); ImGui::TextDisabled("- %d libraries hidden", g_scan.libraries); }

    ImGui::SetNextItemWidth(sz.x * 0.3f);
    ImGui::InputTextWithHint("##q", "Search", g_search, sizeof(g_search));
    ImGui::SameLine(); ImGui::SetNextItemWidth(110);
    ImGui::Combo("##sort", &g_sort, "Name\0Type\0");
    ImGui::SameLine(); ImGui::Checkbox("Show libraries", &g_showLibs);
    ImGui::SameLine(); if (ImGui::Button("Rescan")) rescan();

    ImGui::BeginChild("##list", ImVec2(sz.x * 0.42f, 0), true);
    list();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##details", ImVec2(0, 0), true);
    details();
    ImGui::EndChild();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) g_open = false;
    ImGui::End();
}

} // namespace

bool wantsInput() { return g_open; }

void draw() {
    if (g_open) menu();
    else if (onTitleScreen()) titleButton();
}

} // namespace bmm::ui
