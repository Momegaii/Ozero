#!/bin/sh
# Stage 0 header checks. A1 owns this conformance script; A4 owns CI integration and
# must invoke it unmodified. See docs/decisions/a1-global-decisions.md (D-010).
#
# 1. Every public header compiles standalone as freestanding C11.
# 2. The umbrella header compiles as C11 and C++17 (extern "C" correctness).
# 3. Static assertions on documented ABI facts hold.
# 4. Generic interfaces contain no device-class or GPU-generation assumptions.
# 5. Public headers do not pull in hosted libc headers.
# 6. The public include root is include/ozero; include/o0 is gone and not accepted.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-gcc}
CXX=${CXX:-g++}
CFLAGS="-std=c11 -ffreestanding -Wall -Wextra -Werror -I${ROOT}/include"
CXXFLAGS="-std=c++17 -ffreestanding -Wall -Wextra -Werror -I${ROOT}/include"

status=0

printf '%s\n' "== standalone header compilation =="
for header in "${ROOT}"/include/ozero/*.h; do
    name=$(basename "$header")
    if printf '#include "ozero/%s"\n' "$name" | \
        "$CC" $CFLAGS -x c -fsyntax-only - >/dev/null 2>&1; then
        printf 'PASS  %s\n' "$name"
    else
        printf 'FAIL  %s\n' "$name"
        printf '#include "ozero/%s"\n' "$name" | "$CC" $CFLAGS -x c -fsyntax-only - 2>&1 | sed 's/^/      /'
        status=1
    fi
done

printf '%s\n' "== umbrella header, C and C++ =="
if printf '#include "ozero/o0.h"\nint main(void){return 0;}\n' | \
    "$CC" $CFLAGS -x c -fsyntax-only - >/dev/null 2>&1; then
    printf 'PASS  ozero/o0.h (C11)\n'
else
    printf 'FAIL  ozero/o0.h (C11)\n'
    status=1
fi

if command -v "$CXX" >/dev/null 2>&1; then
    if printf '#include "ozero/o0.h"\nint main(){return 0;}\n' | \
        "$CXX" $CXXFLAGS -x c++ -fsyntax-only - >/dev/null 2>&1; then
        printf 'PASS  ozero/o0.h (C++17)\n'
    else
        printf 'FAIL  ozero/o0.h (C++17)\n'
        status=1
    fi
else
    printf 'SKIP  C++ compiler not found (%s)\n' "$CXX"
fi

printf '%s\n' "== ABI static assertions =="
if "$CC" $CFLAGS -fsyntax-only "${ROOT}/tests/abi_static_asserts.c"; then
    printf 'PASS  tests/abi_static_asserts.c\n'
else
    printf 'FAIL  tests/abi_static_asserts.c\n'
    status=1
fi

printf '%s\n' "== generic-interface device neutrality =="
forbidden='i915|gen8|gen9|gen10|gen11|gen12|amdgpu|radeon|nvidia|nouveau|intel_gpu'
if matches=$(grep -rInE "$forbidden" "${ROOT}/include/ozero" 2>/dev/null); then
    printf 'FAIL  device-class or GPU-generation term in generic headers:\n%s\n' "$matches"
    status=1
else
    printf 'PASS  no device-class or GPU-generation terms in include/ozero\n'
fi

printf '%s\n' "== freestanding headers only =="
if matches=$(grep -rInE '#include[[:space:]]*<(stdio|stdlib|string|errno|unistd|math|time)\.h>' \
    "${ROOT}/include/ozero" 2>/dev/null); then
    printf 'FAIL  hosted libc header in public headers:\n%s\n' "$matches"
    status=1
else
    printf 'PASS  no hosted libc headers in include/ozero\n'
fi

printf '%s\n' "== public namespace =="
if [ -e "${ROOT}/include/o0" ]; then
    printf 'FAIL  include/o0 exists; the canonical public root is include/ozero\n'
    status=1
else
    printf 'PASS  no include/o0\n'
fi
if matches=$(grep -rInE '#include[[:space:]]*[<"]o0/' "${ROOT}/include/ozero" 2>/dev/null); then
    printf 'FAIL  legacy o0/ include in public headers:\n%s\n' "$matches"
    status=1
else
    printf 'PASS  no legacy o0/ includes\n'
fi

if [ "$status" -eq 0 ]; then
    printf '%s\n' "RESULT: PASS"
else
    printf '%s\n' "RESULT: FAIL"
fi
exit "$status"
