# BMM (Bedrock Mod Menu)

Client-only LeviLamina mod: adds a "Mods (N)" button to the title screen and an ImGui mod browser for
everything in `...\LeviLauncher\Instances\<instance>\mods\` (native + preload-native, libraries,
parent/child, disabled mods, details, search/sort).

Build: `xmake f -y -p windows -a x64 -m release` then `xmake`. Output in `bin/BMM/`; copy into `mods/`.

Status: not built or run yet. Untested: D3D12 vs D3D11 presenting, title-screen detection (no Level),
imgui package under clang-cl, legacy `ImGui_ImplDX12_Init` signature. Catalogue (levimc.org) is a stub, see
`src/mod/Catalogue.h`. Update `lip.toml` id and the LeviLamina range to match your install.
