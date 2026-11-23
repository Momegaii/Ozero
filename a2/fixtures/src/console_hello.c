#include <stdio.h>
#include <windows.h>

static int compute(int x) { return x * 3 + 1; }

int main(void) {
    HMODULE self = GetModuleHandleW(NULL);
    printf("a2 console fixture: compute=%d module=%p\n", compute(7), (void *)self);
    return 0;
}
