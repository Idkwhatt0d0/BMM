add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")
-- DearOreUI's header-only public API package (same source the official example mod uses).
add_repositories("dearoreui-repo https://github.com/copper-lamp/dearoreui-repo.git")

option("target_type")
    set_default("client")
    set_showmenu(true)
    set_values("client")
option_end()

-- Pin to your LeviLamina if you want, e.g. add_requires("levilamina 26.60.7", ...).
-- NOTE: DearOreUI and its example are written against 26.10.x. The public headers must match the
-- DearOreUI.dll you actually run, or structs drift and the game crashes. If you installed DearOreUI
-- 0.1.2, use that version here; add_requires("dearoreui") alone takes whatever the repo offers.
add_requires("levilamina", {configs = {target_type = get_config("target_type")}})
add_requires("dearoreui")
add_requires("nlohmann_json")
add_requires("levibuildscript")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("BMM")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1")
        set_exceptions("none") -- To avoid conflicts with /EHa.
        add_cxflags( "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
        set_toolchains("clang-cl")
    end
    add_packages("levilamina", "dearoreui", "nlohmann_json")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")

    -- Ship the page script next to the DLL (loaded at registration via getModDir()/scripts).
    after_build(function(target)
        local mod_define = target:extraconf("rules", "@levibuildscript/modpacker") or {}
        local modName    = mod_define.modName or target:name()
        local srcDir     = path.join(os.scriptdir(), "assets", "scripts")
        if os.isdir(srcDir) then
            local outDir = path.join(os.projectdir(), "bin", modName, "scripts")
            os.mkdir(outDir)
            os.cp(path.join(srcDir, "*"), outDir)
        end
        local cfg = path.join(os.scriptdir(), "config.json")
        if os.isfile(cfg) then
            os.cp(cfg, path.join(os.projectdir(), "bin", modName, "config.json"))
        end
    end)
