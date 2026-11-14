/*
 * version.dll proxy: the loader half of the runtime component.
 *
 * UnityPlayer.dll imports VERSION.dll, and Windows searches the application
 * folder first, so this file is loaded at game start. Every export is forwarded
 * to the system's real version.dll; the game behaves exactly as without it.
 * DllMain only resolves the forwards and hands over to the runtime bootstrap,
 * which waits for IL2CPP to initialise (see bootstrap.cpp).
 *
 * windows.h is deliberately not included here: its version-API prototypes
 * would clash with the naked forwarding stubs below.
 */
typedef void *HMODULE_;
typedef unsigned long DWORD_;
typedef int BOOL_;

__declspec(dllimport) HMODULE_ __stdcall LoadLibraryW(const unsigned short *);
__declspec(dllimport) void *__stdcall GetProcAddress(HMODULE_, const char *);
__declspec(dllimport) unsigned int __stdcall GetSystemDirectoryW(unsigned short *, unsigned int);
__declspec(dllimport) BOOL_ __stdcall DisableThreadLibraryCalls(HMODULE_);

void ltm_on_process_attach(HMODULE_ self);

#define LTM_FORWARDS(X)                \
    X(GetFileVersionInfoA)             \
    X(GetFileVersionInfoByHandle)      \
    X(GetFileVersionInfoExA)           \
    X(GetFileVersionInfoExW)           \
    X(GetFileVersionInfoSizeA)         \
    X(GetFileVersionInfoSizeExA)       \
    X(GetFileVersionInfoSizeExW)       \
    X(GetFileVersionInfoSizeW)         \
    X(GetFileVersionInfoW)             \
    X(VerFindFileA)                    \
    X(VerFindFileW)                    \
    X(VerInstallFileA)                 \
    X(VerInstallFileW)                 \
    X(VerLanguageNameA)                \
    X(VerLanguageNameW)                \
    X(VerQueryValueA)                  \
    X(VerQueryValueW)

/* On x64 there is a single calling convention, so a tail jump forwards any
 * signature unchanged. */
#define LTM_DEFINE(name)                                                     \
    void *ltm_real_##name;                                                   \
    __declspec(dllexport) __attribute__((naked)) void name(void) {           \
        __asm__ volatile("jmpq *ltm_real_" #name "(%rip)");                  \
    }
LTM_FORWARDS(LTM_DEFINE)

static void *ltm_missing(void) { return 0; }

static void resolve_forwards(void) {
    unsigned short path[600];
    unsigned int n = GetSystemDirectoryW(path, 560);
    const char *tail = "\\version.dll";
    HMODULE_ real;
    unsigned int i;
    if (n == 0 || n >= 560) {
        n = 0;
    }
    for (i = 0; tail[i]; ++i) {
        path[n + i] = (unsigned short)tail[i];
    }
    path[n + i] = 0;
    real = LoadLibraryW(path);
#define LTM_RESOLVE(name)                                                    \
    ltm_real_##name = real ? GetProcAddress(real, #name) : 0;                \
    if (!ltm_real_##name) ltm_real_##name = (void *)ltm_missing;
    LTM_FORWARDS(LTM_RESOLVE)
}

BOOL_ __stdcall DllMain(HMODULE_ self, DWORD_ reason, void *reserved) {
    (void)reserved;
    if (reason == 1 /* DLL_PROCESS_ATTACH */) {
        DisableThreadLibraryCalls(self);
        resolve_forwards();
        ltm_on_process_attach(self);
    }
    return 1;
}
