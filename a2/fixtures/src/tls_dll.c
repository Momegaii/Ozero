#include <windows.h>

__thread int tls_counter = 7;

static VOID NTAPI tls_callback(PVOID module, DWORD reason, PVOID reserved) {
    (void)module; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) tls_counter += 1;
}

static PIMAGE_TLS_CALLBACK tls_callback_entry __attribute__((section(".CRT$XLB"), used)) = tls_callback;

__declspec(dllexport) int a2_get_counter(void) { return tls_counter; }
__declspec(dllexport) int a2_add(int a, int b) { return a + b; }

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)module; (void)reason; (void)reserved;
    return TRUE;
}
