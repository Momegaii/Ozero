extern "C" int printf(const char *format, ...);

static int fail(int value) {
    if (value > 0) throw 42;
    return value;
}

int main() {
    int result = 0;
    try {
        result = fail(1);
    } catch (int code) {
        result = code;
    }
    printf("a2 c++ fixture: caught=%d\n", result);
    return 0;
}
