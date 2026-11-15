#pragma once
#include "mod/ModScanner.h"
// Fills catUrl / catLicense / catReadme (+ icon) from https://levimc.org/mods/.
// TODO (not implemented yet): the site serves HTML only (no JSON API found). Detail pages live at
//   https://levimc.org/mods/github.com/<owner>/<repo>/   and icons are .webp (…/mods/icons/<hash>-144.webp),
// so this needs: (1) an index fetch + name->URL match, (2) page parse, (3) libwebp decode for icons,
// (4) caching under BMM/cache/. Run on a worker thread via WinHTTP; never block the render thread.
namespace bmm::catalogue {
inline void requestAsync(ModEntry&) {}
}
