/* Ozero kernel - freestanding memory primitive declarations.
 *
 * kernel/src/string.c provides the definitions; GCC may also lower struct
 * copies and loops into these symbols. Keeping the declarations in one header
 * avoids implicit-function-declaration errors under -Werror.
 */
#ifndef O0_STRING_H
#define O0_STRING_H

#include <stddef.h>

void *memset(void *dst, int value, size_t count);
void *memcpy(void *dst, const void *src, size_t count);
void *memmove(void *dst, const void *src, size_t count);
int memcmp(const void *lhs, const void *rhs, size_t count);
size_t strlen(const char *text);

#endif /* O0_STRING_H */
