#include "mod/Mod.h"
#include "mod/Overlay.h"
#include "ll/api/mod/RegisterHelper.h"

namespace bmm {
Mod& Mod::getInstance() { static Mod instance; return instance; }
bool Mod::load()    { return true; }
bool Mod::enable()  { return overlay::install(); }
bool Mod::disable() { overlay::uninstall(); return true; }
} // namespace bmm

LL_REGISTER_MOD(bmm::Mod, bmm::Mod::getInstance());
