# BMM (Bedrock Mod Menu)

Client LeviLamina mod that adds a "Mods (N)" button and a mod browser to Minecraft Bedrock's OreUI,
built as an external mod on [DearOreUI](https://github.com/copper-lamp/Dear-OreUI) (required).

- C++: scans `<instance>\mods\*\manifest.json(.close)` and serves it to the page through the host method `bmm.list`.
- JS: `assets/scripts/bmm_ui.js` draws the button and panel (search, sort, libraries toggle, parent/child, badges, details).
- `config.json` (next to the DLL): `{"scopes":["any"]}`. Narrow to `main_menu`, `play`, ... once you know the title screen's scope.

Build: `xmake f -y -p windows -a x64 -m release` then `xmake`; copy `bin/BMM/` into `mods/`. DearOreUI must be installed.

Status: not built or run. Depends on DearOreUI supporting your game version (it targets LeviLamina 26.10.x).
Not done: levimc.org catalogue data (icon/license/README), enable/disable toggle, open-folder button.
