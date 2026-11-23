#include <windows.h>
#include <stdio.h>

int main(void) {
    HANDLE read_end, write_end;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) return 1;

    DWORD written = 0;
    WriteFile(write_end, "pipe", 4, &written, NULL);
    CloseHandle(write_end);

    char buffer[16] = {0};
    DWORD read = 0;
    ReadFile(read_end, buffer, sizeof(buffer) - 1, &read, NULL);
    CloseHandle(read_end);

    HANDLE file = CreateFileW(L"a2-fixture.tmp", GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);

    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(L"*", &data);
    if (find != INVALID_HANDLE_VALUE) FindClose(find);

    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    wchar_t cmd[] = L"cmd.exe /c exit 0";
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    void *block = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (block) VirtualFree(block, 0, MEM_RELEASE);

    wchar_t module[MAX_PATH];
    GetModuleFileNameW(NULL, module, MAX_PATH);

    printf("a2 file/process fixture: read=%lu written=%lu\n",
           (unsigned long)read, (unsigned long)written);
    return 0;
}
