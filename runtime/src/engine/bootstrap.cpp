// Bootstrap: waits for IL2CPP to initialise, then starts the runtime.
//
// UnityPlayer.dll loads GameAssembly.dll itself and resolves every il2cpp_*
// export with GetProcAddress. Patching UnityPlayer's import of GetProcAddress
// lets us wrap il2cpp_init: once it returns, the managed runtime is ready and
// we are on the game's main thread, before any scene or UI exists.
#include <windows.h>

#include <cstring>
#include <string>

#include "il2cpp_api.h"
#include "runtime.h"

namespace {

using GetProcAddress_t = FARPROC(WINAPI *)(HMODULE, LPCSTR);
using il2cpp_init_t = int (*)(const char *);

GetProcAddress_t g_real_get_proc_address = nullptr;
il2cpp_init_t g_real_il2cpp_init = nullptr;
HMODULE g_game_assembly = nullptr;
HMODULE g_self = nullptr;

int hooked_il2cpp_init(const char *domain_name) {
    int result = g_real_il2cpp_init(domain_name);
    ltm::runtime_start(g_self, g_game_assembly);
    return result;
}

FARPROC WINAPI hooked_get_proc_address(HMODULE module, LPCSTR name) {
    FARPROC proc = g_real_get_proc_address(module, name);
    // Ordinal lookups pass a small integer instead of a string.
    if (proc && reinterpret_cast<uintptr_t>(name) > 0xFFFF && std::strcmp(name, "il2cpp_init") == 0 &&
        !g_real_il2cpp_init) {
        g_game_assembly = module;
        g_real_il2cpp_init = reinterpret_cast<il2cpp_init_t>(reinterpret_cast<void *>(proc));
        return reinterpret_cast<FARPROC>(reinterpret_cast<void *>(&hooked_il2cpp_init));
    }
    return proc;
}

// Replaces one imported function in a module's import address table.
bool patch_import(HMODULE module, const char *dll, const char *function, void *replacement, void **original) {
    auto base = reinterpret_cast<BYTE *>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp(reinterpret_cast<char *>(base + imp->Name), dll) != 0) continue;
        if (!imp->OriginalFirstThunk) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + imp->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA *>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<char *>(by_name->Name), function) != 0) continue;
            DWORD old;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) return false;
            *original = reinterpret_cast<void *>(slots->u1.Function);
            slots->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            VirtualProtect(&slots->u1.Function, sizeof(void *), old, &old);
            return true;
        }
    }
    return false;
}

}  // namespace

extern "C" void ltm_on_process_attach(void *self) {
    g_self = static_cast<HMODULE>(self);
    HMODULE player = GetModuleHandleW(L"UnityPlayer.dll");
    if (!player) return;  // Not the game process (e.g. the crash handler).
    void *original = nullptr;
    // UnityPlayer imports GetProcAddress from KERNEL32.dll.
    if (patch_import(player, "KERNEL32.dll", "GetProcAddress",
                     reinterpret_cast<void *>(&hooked_get_proc_address), &original)) {
        g_real_get_proc_address = reinterpret_cast<GetProcAddress_t>(original);
    }
}
