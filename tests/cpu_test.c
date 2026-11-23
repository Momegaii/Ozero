/* Host regression test: compare the decoder with raw CPUID registers. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ozero/cpu.h"
#include "../kernel/arch/x86_64/cpu.c"

static void raw_cpuid(uint32_t leaf,uint32_t subleaf,
                      uint32_t *a,uint32_t *b,uint32_t *c,uint32_t *d) {
    __asm__ volatile ("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d)
                      :"a"(leaf),"c"(subleaf));
}
#define CHECK(x,m) do { if (!(x)) { fprintf(stderr,"FAIL: %s\n",m); return 1; } } while (0)

int main(void) {
    uint32_t a,b,c,d;
    raw_cpuid(0,0,&a,&b,&c,&d);
    uint32_t max_leaf=a;
    CHECK(o0_cpu_detect()==O0_OK,"detection failed");
    const struct o0_cpu_features *f=o0_cpu_features();
    const struct o0_cpu_info *info=o0_cpu_info(0);
    CHECK(f && info,"accessors failed");
    CHECK(f->max_basic_leaf==max_leaf,"maximum basic leaf mismatch");
    CHECK(info->vendor[12]=='\0',"vendor string not terminated");
    CHECK(o0_cpu_detect()==O0_EBUSY,"repeat detection not rejected");
    if (max_leaf>=1) {
        raw_cpuid(1,0,&a,&b,&c,&d);
        CHECK(f->pae==(((d>>6)&1u)!=0),"PAE mismatch");
        CHECK(f->msr==(((d>>5)&1u)!=0),"MSR mismatch");
        CHECK(f->clfsh==(((d>>19)&1u)!=0),"CLFLUSH mismatch");
        CHECK(f->xsave==(((c>>26)&1u)!=0),"XSAVE mismatch");
        CHECK(f->pcid==(((c>>17)&1u)!=0),"PCID mismatch");
        CHECK(f->xcr0_supported==0 || (f->xsave && ((c>>27)&1u)),
              "XGETBV result without XSAVE+OSXSAVE");
    }
    if (max_leaf>=7) {
        raw_cpuid(7,0,&a,&b,&c,&d);
        CHECK(f->smep==(((b>>7)&1u)!=0),"SMEP mismatch");
        CHECK(f->smap==(((b>>20)&1u)!=0),"SMAP mismatch");
        CHECK(f->umip==(((c>>2)&1u)!=0),"UMIP mismatch");
        CHECK(f->serialize==(((d>>14)&1u)!=0),"SERIALIZE mismatch");
    }
    puts("CPU feature tests: PASS");
    return 0;
}
