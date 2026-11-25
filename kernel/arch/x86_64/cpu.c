/* Ozero kernel - x86-64 CPU feature discovery.
 * CPUID leaf and bit assignments follow the Intel SDM.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ozero/cpu.h"
#include "ozero/status.h"
#include "ozero/types.h"

static struct o0_cpu_features features;
static struct o0_cpu_info boot_cpu;
static bool features_detected;

static inline void cpuid(uint32_t leaf, uint32_t subleaf,
                         uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(subleaf));
}

static inline uint64_t xgetbv(uint32_t index) {
    uint32_t eax, edx;
    __asm__ volatile ("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
}

o0_status o0_cpu_detect(void) {
    if (features_detected) return O0_EBUSY;

    uint32_t eax, ebx, ecx, edx;
    bool avx_hw = false, avx2_hw = false, avx512f_hw = false;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    features.max_basic_leaf = eax;
    boot_cpu.vendor[0]=(char)ebx; boot_cpu.vendor[1]=(char)(ebx>>8);
    boot_cpu.vendor[2]=(char)(ebx>>16); boot_cpu.vendor[3]=(char)(ebx>>24);
    boot_cpu.vendor[4]=(char)edx; boot_cpu.vendor[5]=(char)(edx>>8);
    boot_cpu.vendor[6]=(char)(edx>>16); boot_cpu.vendor[7]=(char)(edx>>24);
    boot_cpu.vendor[8]=(char)ecx; boot_cpu.vendor[9]=(char)(ecx>>8);
    boot_cpu.vendor[10]=(char)(ecx>>16); boot_cpu.vendor[11]=(char)(ecx>>24);
    boot_cpu.vendor[12]='\0';

    if (features.max_basic_leaf >= 1) {
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        uint32_t base_family=(eax>>8)&0xfu;
        boot_cpu.family=base_family;
        boot_cpu.model=(eax>>4)&0xfu;
        boot_cpu.stepping=eax&0xfu;
        if (base_family==0xfu) boot_cpu.family+=(eax>>20)&0xffu;
        if (base_family==0x6u || base_family==0xfu)
            boot_cpu.model+=((eax>>16)&0xfu)<<4;

        features.tsc=((edx>>4)&1u)!=0;
        features.msr=((edx>>5)&1u)!=0;
        features.pae=((edx>>6)&1u)!=0;
        features.cx8=((edx>>8)&1u)!=0;
        features.apic=((edx>>9)&1u)!=0;
        features.mtrr=((edx>>12)&1u)!=0;
        features.pge=((edx>>13)&1u)!=0;
        features.cmov=((edx>>15)&1u)!=0;
        features.pat=((edx>>16)&1u)!=0;
        features.clfsh=((edx>>19)&1u)!=0;
        features.fxsr=((edx>>24)&1u)!=0;
        features.sse=((edx>>25)&1u)!=0;
        features.sse2=((edx>>26)&1u)!=0;

        features.aesni=((ecx>>25)&1u)!=0;
        features.xsave=((ecx>>26)&1u)!=0;
        bool osxsave=((ecx>>27)&1u)!=0;
        avx_hw=((ecx>>28)&1u)!=0;
        features.rdrand=((ecx>>30)&1u)!=0;
        features.pcid=((ecx>>17)&1u)!=0;
        features.x2apic=((ecx>>21)&1u)!=0;
        features.tsc_deadline=((ecx>>24)&1u)!=0;
        features.hypervisor=((ecx>>31)&1u)!=0;
        if (features.clfsh) {
            uint32_t bytes=((ebx>>8)&0xffu)*8u;
            features.clflush_size=bytes<=UINT8_MAX?(uint8_t)bytes:0;
        }
        /* XSAVE alone is insufficient: XGETBV requires OSXSAVE as well. */
        if (features.xsave && osxsave) features.xcr0_supported=xgetbv(0);
    }

    if (features.max_basic_leaf >= 7) {
        cpuid(7, 0, &eax, &ebx, &ecx, &edx);
        features.fsgsbase=((ebx>>0)&1u)!=0;
        avx2_hw=((ebx>>5)&1u)!=0;
        features.smep=((ebx>>7)&1u)!=0;
        features.invpcid=((ebx>>10)&1u)!=0;
        avx512f_hw=((ebx>>16)&1u)!=0;
        features.rdseed=((ebx>>18)&1u)!=0;
        features.smap=((ebx>>20)&1u)!=0;
        features.umip=((ecx>>2)&1u)!=0;
        features.pku=((ecx>>3)&1u)!=0;
        features.ospke=((ecx>>4)&1u)!=0;
        features.cet_ss=((ecx>>7)&1u)!=0;
        features.la57=((ecx>>16)&1u)!=0;
        features.serialize=((edx>>14)&1u)!=0;
        features.cet_ibt=((edx>>20)&1u)!=0;
    }

    cpuid(0x80000000u,0,&eax,&ebx,&ecx,&edx);
    features.max_extended_leaf=eax;
    if (eax>=0x80000001u) {
        cpuid(0x80000001u,0,&eax,&ebx,&ecx,&edx);
        features.syscall=((edx>>11)&1u)!=0;
        features.nx=((edx>>20)&1u)!=0;
        features.rdtscp=((edx>>27)&1u)!=0;
        features.long_mode=((edx>>29)&1u)!=0;
        features.page_1gb=((edx>>26)&1u)!=0;
    }
    if (features.max_extended_leaf>=0x80000007u) {
        cpuid(0x80000007u,0,&eax,&ebx,&ecx,&edx);
        features.invariant_tsc=((edx>>8)&1u)!=0;
    }
    if (features.max_extended_leaf>=0x80000008u) {
        cpuid(0x80000008u,0,&eax,&ebx,&ecx,&edx);
        features.phys_addr_bits=(uint8_t)(eax&0xffu);
        features.virt_addr_bits=(uint8_t)((eax>>8)&0xffu);
    }
    if (features.max_extended_leaf >= 0x80000004u) {
        uint32_t regs[12];
        for (uint32_t i = 0; i < 3u; ++i) {
            cpuid(0x80000002u + i, 0, &regs[i * 4u], &regs[i * 4u + 1u],
                  &regs[i * 4u + 2u], &regs[i * 4u + 3u]);
        }
        for (uint32_t i = 0; i < 12u; ++i) {
            for (uint32_t b = 0; b < 4u; ++b) {
                boot_cpu.brand[i * 4u + b] = (char)(regs[i] >> (b * 8u));
            }
        }
        boot_cpu.brand[48] = '\0';
        uint32_t first = 0;
        while (first < 48u && boot_cpu.brand[first] == ' ') {
            ++first;
        }
        uint32_t end = 48u;
        while (end > first && (boot_cpu.brand[end - 1u] == ' ' ||
                               boot_cpu.brand[end - 1u] == '\0')) {
            --end;
        }
        for (uint32_t i = first; i < end; ++i) {
            boot_cpu.brand[i - first] = boot_cpu.brand[i];
        }
        boot_cpu.brand[end - first] = '\0';
    }
    if (features.hypervisor) {
        cpuid(0x40000000u, 0, &eax, &ebx, &ecx, &edx);
        boot_cpu.hypervisor[0] = (char)ebx;
        boot_cpu.hypervisor[1] = (char)(ebx >> 8);
        boot_cpu.hypervisor[2] = (char)(ebx >> 16);
        boot_cpu.hypervisor[3] = (char)(ebx >> 24);
        boot_cpu.hypervisor[4] = (char)ecx;
        boot_cpu.hypervisor[5] = (char)(ecx >> 8);
        boot_cpu.hypervisor[6] = (char)(ecx >> 16);
        boot_cpu.hypervisor[7] = (char)(ecx >> 24);
        boot_cpu.hypervisor[8] = (char)edx;
        boot_cpu.hypervisor[9] = (char)(edx >> 8);
        boot_cpu.hypervisor[10] = (char)(edx >> 16);
        boot_cpu.hypervisor[11] = (char)(edx >> 24);
        boot_cpu.hypervisor[12] = '\0';
        cpuid(0x40000001u, 0, &eax, &ebx, &ecx, &edx);
        boot_cpu.hypervisor_signature = eax;
    }
    /* Report AVX-family instructions only when the OS has enabled and
     * configured the corresponding extended register state in XCR0. */
    bool avx_state = features.xsave && features.xcr0_supported != 0 &&
                     (features.xcr0_supported & 0x6u) == 0x6u;
    bool avx512_state = (features.xcr0_supported & 0xe6u) == 0xe6u;
    features.avx = avx_hw && avx_state;
    features.avx2 = avx2_hw && features.avx;
    features.avx512f = avx512f_hw && avx512_state;

    if (features.clfsh && features.clflush_size==0) features.clflush_size=64;

    boot_cpu.index=0; boot_cpu.apic_id=0; boot_cpu.acpi_uid=0;
    boot_cpu.flags=O0_CPU_F_BSP;
    features_detected=true;
    return O0_OK;
}

const struct o0_cpu_features *o0_cpu_features(void) {
    return features_detected ? &features : NULL;
}
const struct o0_cpu_info *o0_cpu_info(uint32_t index) {
    return index==0 && features_detected ? &boot_cpu : NULL;
}
uint32_t o0_cpu_count(void) { return 1; }
uint32_t o0_cpu_index(void) { return 0; }
uint64_t o0_rdtsc(void) {
    uint32_t lo,hi;
    __asm__ volatile ("rdtsc":"=a"(lo),"=d"(hi));
    return ((uint64_t)hi<<32)|lo;
}
void o0_cpu_relax(void) { __asm__ volatile ("pause" ::: "memory"); }
void o0_cpu_idle(void) { __asm__ volatile ("sti; hlt" ::: "memory"); }
O0_NORETURN void o0_cpu_halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
}
