#include <windows.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show) {
    (void)instance; (void)previous; (void)command_line; (void)show;
    MessageBoxW(NULL, L"A2 GUI fixture", L"Ozero A2", MB_OK | MB_ICONINFORMATION);
    return 0;
}
