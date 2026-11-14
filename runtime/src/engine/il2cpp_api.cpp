#include "il2cpp_api.h"

#include <windows.h>

namespace ltm::il2cpp {

#define LTM_IL2CPP_DEFINE(ret, name, args) name##_t name = nullptr;
LTM_IL2CPP_API(LTM_IL2CPP_DEFINE)
#undef LTM_IL2CPP_DEFINE

const char *resolve(void *game_assembly) {
    HMODULE module = static_cast<HMODULE>(game_assembly);
#define LTM_IL2CPP_RESOLVE(ret, name, args)                                        \
    name = reinterpret_cast<name##_t>(reinterpret_cast<void *>(GetProcAddress(module, #name))); \
    if (!name) return #name;
    LTM_IL2CPP_API(LTM_IL2CPP_RESOLVE)
#undef LTM_IL2CPP_RESOLVE
    return nullptr;
}

}  // namespace ltm::il2cpp
