/* Ozero Stage 7 - PE32+ console hello-world.
 *
 * Built as a PE32+ x86-64 image (tools/mkpe.py) and loaded by the Ozero PE
 * loader. Calls kernel32.WriteFile -> ntdll.NtWriteFile -> native debug_write
 * -> serial console, then kernel32.ExitProcess -> native exit.
 */
#include "kernel32.h"

#include "ozero/types.h"

static int k_strlen(const char *s) {
    int n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

O0_NORETURN void _start(void) {
    const char msg[] = "OZERO: stage7 pe hello OK\n";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), msg, (DWORD)k_strlen(msg), NULL,
              NULL);
    ExitProcess(0);
    /* Unreached. */
    for (;;) {
        __asm__ volatile ("hlt" ::: "memory");
    }
}
