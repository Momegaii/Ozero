/* Ozero kernel - freestanding memory primitives.
 *
 * GCC may lower loops or struct copies into calls to these symbols even with
 * -ffreestanding, so the kernel provides its own definitions.
 */

#include <stddef.h>
#include <stdint.h>

void *memset(void *dst, int value, size_t count) {
    uint8_t *out = (uint8_t *)dst;
    for (size_t i = 0; i < count; ++i) {
        out[i] = (uint8_t)value;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t count) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    for (size_t i = 0; i < count; ++i) {
        out[i] = in[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t count) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    if (out < in) {
        for (size_t i = 0; i < count; ++i) {
            out[i] = in[i];
        }
    } else {
        for (size_t i = count; i > 0; --i) {
            out[i - 1] = in[i - 1];
        }
    }
    return dst;
}

int memcmp(const void *lhs, const void *rhs, size_t count) {
    const uint8_t *a = (const uint8_t *)lhs;
    const uint8_t *b = (const uint8_t *)rhs;
    for (size_t i = 0; i < count; ++i) {
        if (a[i] != b[i]) {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}
