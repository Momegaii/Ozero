/* Ozero kernel - Intel Haswell (Gen7.5) GGTT/PPGTT backend.
 *
 * Encoding and lifecycle for the two translation structures only. Nothing here touches
 * hardware: page-table installation (PGTBL_CTL), engine MMIO (PP_DIR_BASE, PP_DCLV) and
 * invalidation commands belong to later phases and stay behind this boundary.
 *
 * Sources: docs/research/haswell-gen75-ppgtt-ggtt.md, which cites the 2013 Haswell PRM
 * Volumes 5, 2b and 2c by page.
 *
 * NOTE: Register offsets and bit layouts for PGTBL_CTL, PGTBL_CTL2, PP_DIR_BASE are NOT
 * fully documented in the available PRM volumes. The values below are derived from the
 * reference implementation (Linux i915) and structural evidence from the PRM, but they
 * MUST be verified against hardware before production use. See
 * docs/research/haswell-gen75-ppgtt-ggtt.md Section 12.3 for details.
 */
#ifndef O0_GPU_HSW_H
#define O0_GPU_HSW_H

#include <stdbool.h>
#include <stdint.h>

#include "ozero/gpu_device.h"
#include "ozero/mm.h"
#include "ozero/status.h"
#include "ozero/types.h"

#define O0_HSW_PAGE_SIZE 4096u
#define O0_HSW_PT_ENTRIES 1024u  /* PTEs per page table: 4 MiB per PDE */
#define O0_HSW_PD_ENTRIES 512u   /* PDEs per directory: PP_DCLV covers 32 x 16 */
#define O0_HSW_PDE_COVERAGE (4u * 1024u * 1024u)
#define O0_HSW_DCLV_COVERAGE (16u * O0_HSW_PDE_COVERAGE) /* one PP_DCLV bit */
#define O0_HSW_MAX_VA_SIZE (2ull * 1024ull * 1024ull * 1024ull)
#define O0_HSW_PTE_MAX_PHYS (512ull * 1024ull * 1024ull * 1024ull)
#define O0_HSW_PDE_MAX_PHYS (1024ull * 1024ull * 1024ull * 1024ull)

/* Cacheability field, PRM Vol 5 p.36 and p.43-45. Encoding 0001 is undefined and is
 * rejected rather than mapped to a default. */
enum o0_hsw_cache {
    O0_HSW_CACHE_UC = 0x0,
    O0_HSW_CACHE_WB_LLC_AGE3 = 0x2,
    O0_HSW_CACHE_WB_LLC_AGE0 = 0x3,
    O0_HSW_CACHE_WT_ELLC_AGE0 = 0x4,
    O0_HSW_CACHE_WT_ELLC_AGE3 = 0x5,
    O0_HSW_CACHE_WT_AGE0 = 0x6,
    O0_HSW_CACHE_WT_AGE3 = 0x7,
    O0_HSW_CACHE_WB_AGE3 = 0x8,
    O0_HSW_CACHE_WB_AGE2 = 0x9,
    O0_HSW_CACHE_WB_AGE1 = 0xA,
    O0_HSW_CACHE_WB_AGE0 = 0xB,
    O0_HSW_CACHE_WB_ELLC_AGE3 = 0xC,
    O0_HSW_CACHE_WB_ELLC_AGE2 = 0xD,
    O0_HSW_CACHE_WB_ELLC_AGE1 = 0xE,
    O0_HSW_CACHE_WB_ELLC_AGE0 = 0xF,
};

/* Supplies physically backed pages. `alloc_pages` must return a contiguous run of a power
 * of two pages; the GGTT table must be physically contiguous (PRM Vol 5 p.33). */
struct o0_hsw_page_pool {
    o0_status (*alloc_pages)(void *ctx, uint32_t pages, o0_paddr_t *phys, void **virt);
    void (*free_pages)(void *ctx, o0_paddr_t phys, uint32_t pages, void *virt);
    void *ctx;
};

struct o0_hsw_ppgtt;
struct o0_hsw_ggtt;

/* Entry encoding. Encoding is pure arithmetic; validation happens in the callers below. */
uint32_t o0_hsw_encode_pte(o0_paddr_t phys, enum o0_hsw_cache cache);
o0_status o0_hsw_decode_pte(uint32_t entry, o0_paddr_t *phys, enum o0_hsw_cache *cache,
                            bool *valid);
uint32_t o0_hsw_encode_pde(o0_paddr_t table_phys);
o0_status o0_hsw_decode_pde(uint32_t entry, o0_paddr_t *table_phys, bool *valid,
                            bool *big_page);

/* Directory-cache mask covering `va_size` bytes. Returns O0_ERANGE above 2 GiB. */
o0_status o0_hsw_dclv_for_size(uint64_t va_size, uint32_t *mask);

/* ---- Per-process GTT (two level) ---- */

o0_status o0_hsw_ppgtt_create(const struct o0_hsw_page_pool *pool, uint64_t va_size,
                              struct o0_hsw_ppgtt **out);
void o0_hsw_ppgtt_destroy(struct o0_hsw_ppgtt *ppgtt);
o0_status o0_hsw_ppgtt_map(struct o0_hsw_ppgtt *ppgtt, uint64_t va, o0_paddr_t phys,
                           enum o0_hsw_cache cache);
o0_status o0_hsw_ppgtt_unmap(struct o0_hsw_ppgtt *ppgtt, uint64_t va);
o0_status o0_hsw_ppgtt_translate(const struct o0_hsw_ppgtt *ppgtt, uint64_t va,
                                 o0_paddr_t *phys);
uint32_t o0_hsw_ppgtt_dclv(const struct o0_hsw_ppgtt *ppgtt);
o0_paddr_t o0_hsw_ppgtt_dir_phys(const struct o0_hsw_ppgtt *ppgtt);

/* Directory entries are only defined to change for a non-active context (Vol 5 p.42), and a
 * PTE change needs an invalidation command this layer cannot emit. Map and unmap therefore fail
 * with O0_EBUSY while the space is marked active; mark it inactive, change it, then invalidate
 * before activating it again. */
void o0_hsw_ppgtt_set_active(struct o0_hsw_ppgtt *ppgtt, bool active);
bool o0_hsw_ppgtt_active(const struct o0_hsw_ppgtt *ppgtt);

/* The backend records that a translation change needs invalidation before an engine may
 * rely on it. It does not emit commands: that needs ring buffers (Phase 4). */
bool o0_hsw_ppgtt_needs_invalidation(const struct o0_hsw_ppgtt *ppgtt);
void o0_hsw_ppgtt_invalidated(struct o0_hsw_ppgtt *ppgtt);

/* ---- Global GTT (single level) ---- */

o0_status o0_hsw_ggtt_create(const struct o0_hsw_page_pool *pool, uint32_t entries,
                             struct o0_hsw_ggtt **out);
void o0_hsw_ggtt_destroy(struct o0_hsw_ggtt *ggtt);
o0_status o0_hsw_ggtt_map(struct o0_hsw_ggtt *ggtt, uint64_t gfx_addr, o0_paddr_t phys,
                          enum o0_hsw_cache cache);
o0_status o0_hsw_ggtt_unmap(struct o0_hsw_ggtt *ggtt, uint64_t gfx_addr);
o0_status o0_hsw_ggtt_translate(const struct o0_hsw_ggtt *ggtt, uint64_t gfx_addr,
                                o0_paddr_t *phys);
/* Fills every entry with the same physical page, as the PRM recommends (Vol 5 p.35). */
o0_status o0_hsw_ggtt_fill_dummy(struct o0_hsw_ggtt *ggtt, o0_paddr_t dummy_phys);
uint64_t o0_hsw_ggtt_aperture(const struct o0_hsw_ggtt *ggtt);

/* Pool backed by the kernel page allocator and direct map. */
const struct o0_hsw_page_pool *o0_hsw_kernel_pool(void);

/* ---- Haswell MMIO register offsets (need hardware verification) ---- */

/* Global GTT control register (PRM Vol 5 p.33 references PGTBL_CTL but layout is not in
 * extracted Vol 2c). Inferred from i915: offset 0x2020, 32-bit. */
#define O0_HSW_PGTBL_CTL      0x2020u
#define O0_HSW_PGTBL_CTL2     0x2028u  /* PPGTT control, also inferred */

/* PGTBL_CTL bit layout (inferred, NEEDS HARDWARE VERIFICATION):
 * Bit 0: GGTT enable
 * Bits 2:1: GGTT size (0=128KiB/128MiB, 1=256KiB/256MiB, 2=512KiB/512MiB)
 * Bit 31:12: GGTT base address (4 KiB aligned) */
/* PGTBL_CTL2 bit layout (inferred, NEEDS HARDWARE VERIFICATION):
 * Bit 0: PPGTT enable
 * Bits 2:1: PPGTT size (0=64KiB/64MiB, 1=128KiB/128MiB, 2=256KiB/256MiB, 3=512KiB/512MiB)
 * Bit 31:12: PPGTT directory base (4 KiB aligned, within GGTT) */

#define O0_HSW_PGTBL_CTL_GGTT_ENABLE   (1u << 0)
#define O0_HSW_PGTBL_CTL_SIZE_SHIFT    1
#define O0_HSW_PGTBL_CTL_SIZE_MASK     (3u << 1)
#define O0_HSW_PGTBL_CTL_BASE_SHIFT    12

#define O0_HSW_PGTBL_CTL2_PPGTT_ENABLE (1u << 0)
#define O0_HSW_PGTBL_CTL2_SIZE_SHIFT   1
#define O0_HSW_PGTBL_CTL2_SIZE_MASK    (3u << 1)
#define O0_HSW_PGTBL_CTL2_BASE_SHIFT   12

/* PPGTT size encodings for PGTBL_CTL2 */
#define O0_HSW_PPGTT_SIZE_64K   0u  /* 64 KiB table -> 64 MiB aperture */
#define O0_HSW_PPGTT_SIZE_128K  1u  /* 128 KiB table -> 128 MiB aperture */
#define O0_HSW_PPGTT_SIZE_256K  2u  /* 256 KiB table -> 256 MiB aperture */
#define O0_HSW_PPGTT_SIZE_512K  3u  /* 512 KiB table -> 512 MiB aperture */

/* GFX_MODE register (PRM Vol 2c p.91-92) */
#define O0_HSW_GFX_MODE         0x229Cu
#define O0_HSW_GFX_MODE_PPGTT_ENABLE  (1u << 9)
#define O0_HSW_GFX_MODE_FLUSH_TLB_INV (1u << 13)

/* PP_DCLV and PP_DIR_BASE per-engine offsets (PRM Vol 2c documents PP_DCLV at these
 * offsets; PP_DIR_BASE is inferred at PP_DCLV + 8 based on 64-bit PP_DCLV layout).
 * Render:  0x2220 (PP_DCLV), 0x2228 (PP_DIR_BASE) - inside 0x2000-0x2FFF (Vol 3 p.18)
 * VCS:     0x12220 (PP_DCLV), 0x12228 (PP_DIR_BASE) - inside 0x12000-0x123FF
 * VECS:    0x1A220 (PP_DCLV), 0x1A228 (PP_DIR_BASE) - Vol 3 does not list this range
 * BCS:     0x22220 (PP_DCLV), 0x22228 (PP_DIR_BASE) - inside 0x22000-0x22FFF */
#define O0_HSW_RENDER_PP_DCLV   0x2220u
#define O0_HSW_RENDER_PP_DIR_BASE 0x2228u
#define O0_HSW_VCS_PP_DCLV      0x12220u
#define O0_HSW_VCS_PP_DIR_BASE  0x12228u
#define O0_HSW_VECS_PP_DCLV     0x1A220u
#define O0_HSW_VECS_PP_DIR_BASE 0x1A228u
#define O0_HSW_BCS_PP_DCLV      0x22220u
#define O0_HSW_BCS_PP_DIR_BASE  0x22228u

/* Engine base offsets for MMIO */
#define O0_HSW_RENDER_BASE      0x00002000u
#define O0_HSW_VCS_BASE         0x00012000u
#define O0_HSW_VECS_BASE        0x0001A000u
#define O0_HSW_BCS_BASE         0x00022000u

/* Engine identifiers */
enum o0_hsw_engine {
    O0_HSW_ENGINE_RENDER = 0,
    O0_HSW_ENGINE_VCS    = 1,
    O0_HSW_ENGINE_VECS   = 2,
    O0_HSW_ENGINE_BCS    = 3,
    O0_HSW_ENGINE_COUNT  = 4,
};

/* ---- Command encoding for invalidation and setup ---- */

/* PIPE_CONTROL (render engine, PRM Vol 2b p.962-963) */
#define O0_HSW_PIPE_CONTROL_OPCODE      (0x3u << 29)  /* GFXPIPE 3D opcode */
#define O0_HSW_PIPE_CONTROL_TYPE        (0x3u << 27)  /* 3DSTATE_PIPE_CONTROL */
#define O0_HSW_PIPE_CONTROL_TLB_INV     (1u << 18)    /* DW1 bit 18: TLB Invalidate */
#define O0_HSW_PIPE_CONTROL_STALL       (1u << 20)    /* DW1 bit 20: Stall at Pixel Scoreboard */
#define O0_HSW_PIPE_CONTROL_POST_SYNC   (1u << 14)    /* DW1 bit 14: Post-Sync Operation */
#define O0_HSW_PIPE_CONTROL_DEPTH_STALL (1u << 13)    /* DW1 bit 13: Depth Stall */
#define O0_HSW_PIPE_CONTROL_CS_STALL    (1u << 12)    /* DW1 bit 12: CS Stall */

/* MI_FLUSH_DW (all engines except render, PRM Vol 2b p.267-268) */
#define O0_HSW_MI_FLUSH_DW_OPCODE       0x26u
#define O0_HSW_MI_FLUSH_DW_TLB_INV      (1u << 18)    /* DW0 bit 18: TLB Invalidate */
#define O0_HSW_MI_FLUSH_DW_POST_SYNC_1  (1u << 14)    /* Post-Sync Operation = 1h (write imm data) */
#define O0_HSW_MI_FLUSH_DW_POST_SYNC_3  (3u << 14)    /* Post-Sync Operation = 3h (write timestamp) */

/* MI_LOAD_REGISTER_IMM (PRM Vol 2b p.248-250) */
#define O0_HSW_MI_LRI_OPCODE            0x22u
#define O0_HSW_MI_LRI_BYTE_DISABLE_SHIFT 8

/* MI_UPDATE_GTT (PRM Vol 2b p.256) - privileged, updates GGTT entries */
#define O0_HSW_MI_UPDATE_GTT_OPCODE     0x23u
#define O0_HSW_MI_UPDATE_GTT_USE_GGTT   (1u << 1)     /* Must be 1 for GGTT updates */

/* Hardware installation functions (require MMIO access via engine) */

struct o0_hsw_mmio {
    void *mmio_base;  /* Mapped MMIO window base (from BAR) */
};

/* Install GGTT into hardware via PGTBL_CTL. Requires GGTT to be physically contiguous
 * and 4 KiB aligned. Returns O0_ENOTSUP if register layout not verified on hardware. */
o0_status o0_hsw_ggtt_install(struct o0_hsw_ggtt *ggtt, const struct o0_hsw_mmio *mmio);

/* Install PPGTT directory base and DCLV into an engine via LRI commands.
 * Requires engine MMIO access and ring buffer (Phase 4). This is a preparation
 * function that encodes the required LRI sequence; actual emission needs ring buffer. */
o0_status o0_hsw_ppgtt_install_prepare(struct o0_hsw_ppgtt *ppgtt, enum o0_hsw_engine engine,
                                       uint32_t *out_lri_dclv, uint32_t *out_lri_base);

/* Encode PIPE_CONTROL with TLB invalidate and stall for render engine. */
uint32_t o0_hsw_encode_pipe_control_tlb_inv(void);

/* Encode MI_FLUSH_DW with TLB invalidate for non-render engines. */
uint32_t o0_hsw_encode_mi_flush_dw_tlb_inv(void);

/* Encode MI_LOAD_REGISTER_IMM for a register offset and value.
 * Returns the number of dwords written (2 for single register). */
uint32_t o0_hsw_encode_lri(uint32_t reg_offset, uint32_t value, uint32_t *out_dwords);

/* Encode the full LRI sequence to load PP_DCLV and PP_DIR_BASE for an engine.
 * Caller must provide a buffer of at least 4 dwords (2 LRI commands = 4 dwords).
 * Returns number of dwords written, or 0 on error. */
uint32_t o0_hsw_encode_ppgtt_context_lri(enum o0_hsw_engine engine, uint32_t dclv, uint32_t dir_base,
                                         uint32_t *buffer, uint32_t buffer_dwords);

/* ---- Invalidation emission (requires ring buffer - Phase 4) ---- */

/* Ring buffer abstraction for command emission. The backend does not own the ring;
 * it only encodes commands into a provided buffer. */
struct o0_hsw_ring {
    uint32_t *buffer;
    uint32_t capacity_dwords;
    uint32_t head;  /* Next write position in dwords */
    uint32_t tail;  /* Hardware read position (updated by hardware) */
};

/* Initialize a ring buffer. Buffer must be allocated by caller. */
void o0_hsw_ring_init(struct o0_hsw_ring *ring, uint32_t *buffer, uint32_t capacity_dwords);

/* Space available in ring (in dwords). */
uint32_t o0_hsw_ring_space(const struct o0_hsw_ring *ring);

/* Emit a TLB invalidation for a PPGTT address space on a specific engine.
 * Encodes the required commands (PIPE_CONTROL for render, MI_FLUSH_DW for others)
 * into the ring buffer. Returns number of dwords emitted, or 0 on error/no space.
 * The caller must ensure the PPGTT is marked active and the invalidation is needed.
 * After emission, caller should call o0_hsw_ppgtt_invalidated() when the invalidation
 * is considered complete (after hardware processes the commands). */
uint32_t o0_hsw_emit_ppgtt_invalidation(struct o0_hsw_ring *ring, enum o0_hsw_engine engine,
                                        const struct o0_hsw_ppgtt *ppgtt);

/* Emit a GGTT invalidation (for GGTT PTE updates). Uses MI_FLUSH_DW with TLB invalidate. */
uint32_t o0_hsw_emit_ggtt_invalidation(struct o0_hsw_ring *ring, enum o0_hsw_engine engine);

/* Emit MI_UPDATE_GTT to update GGTT entries from the command stream (privileged).
 * Requires ring buffer and GGTT to be installed. Returns dwords emitted or 0 on error. */
uint32_t o0_hsw_emit_mi_update_gtt(struct o0_hsw_ring *ring, uint64_t gfx_addr,
                                   o0_paddr_t phys, enum o0_hsw_cache cache);

/* ---- Engine command submission and fences (Phase 4) ---- */

/* Command buffer for GPU work submission. */
struct o0_hsw_cmd_buffer {
    struct o0_gpu_memory *memory;  /* Backing memory object */
    uint64_t gpu_va;               /* GPU virtual address of the buffer */
    uint32_t size_dwords;          /* Size in dwords */
    uint32_t used_dwords;          /* Dwords written so far */
};

/* Fence for GPU completion tracking. */
struct o0_hsw_fence {
    uint64_t seqno;           /* Sequence number */
    uint32_t engine;          /* Engine this fence belongs to */
    bool signaled;            /* Whether the fence has been signaled */
    o0_spinlock_t lock;
};

/* Engine state for a single Haswell engine (render, blitter, video, etc.). */
struct o0_hsw_engine_state {
    enum o0_hsw_engine id;
    struct o0_hsw_ring ring;
    uint32_t *ring_buffer;        /* Ring buffer memory (kernel virtual) */
    o0_paddr_t ring_phys;         /* Physical address of ring buffer */
    uint32_t ring_size_dwords;
    uint32_t head;                /* Software head (next write) */
    uint32_t tail;                /* Hardware tail (last parsed) */
    uint64_t next_seqno;          /* Next fence sequence number */
    struct o0_hsw_fence *fences;  /* Array of in-flight fences */
    uint32_t fence_count;
    uint32_t fence_capacity;
    o0_spinlock_t lock;
};

/* Initialize an engine state with a ring buffer.
 * Ring buffer must be allocated as a GPU memory object and mapped in GGTT. */
o0_status o0_hsw_engine_init(struct o0_hsw_engine_state *engine, enum o0_hsw_engine id,
                             struct o0_gpu_memory *ring_mem, uint64_t ring_gpu_va,
                             uint32_t ring_size_dwords);

/* Destroy an engine state. */
void o0_hsw_engine_destroy(struct o0_hsw_engine_state *engine);

/* Submit a command buffer to an engine.
 * Encodes the batch buffer start command and updates the ring tail.
 * Returns a fence sequence number for completion tracking, or 0 on error. */
uint64_t o0_hsw_engine_submit(struct o0_hsw_engine_state *engine,
                              struct o0_hsw_cmd_buffer *cmd_buffer);

/* Poll for fence completion. Returns true if fence is signaled. */
bool o0_hsw_fence_poll(struct o0_hsw_engine_state *engine, uint64_t seqno);

/* Wait for fence completion with timeout (in nanoseconds).
 * Returns O0_OK if signaled, O0_ETIMEDOUT if timeout, O0_EINVAL if invalid. */
o0_status o0_hsw_fence_wait(struct o0_hsw_engine_state *engine, uint64_t seqno,
                            uint64_t deadline_ns);

/* Signal a fence (called from interrupt handler when GPU reports completion). */
void o0_hsw_fence_signal(struct o0_hsw_engine_state *engine, uint64_t seqno);

/* Get the current hardware tail pointer (updated by GPU). */
uint32_t o0_hsw_engine_get_tail(const struct o0_hsw_engine_state *engine);

/* Update the engine's tail pointer from hardware (MMIO read). */
void o0_hsw_engine_update_tail(struct o0_hsw_engine_state *engine, uint32_t hw_tail);

/* Encode a batch buffer start command for the render engine (MI_BATCH_BUFFER_START).
 * PRM Vol 2b: opcode 0x31, address space bit 0=PPGTT, 1=GGTT. */
uint32_t o0_hsw_encode_batch_buffer_start(uint64_t gpu_va, bool use_ppgtt,
                                          uint32_t *out_dwords);

/* Encode a batch buffer start for non-render engines (MI_BATCH_BUFFER_START variant). */
uint32_t o0_hsw_encode_batch_buffer_start_generic(uint64_t gpu_va, bool use_ppgtt,
                                                  uint32_t *out_dwords);

/* Backend registration for the Stage 5 discovery walk (see include/ozero/gpu_device.h).
 * The match function claims only explicitly verified Haswell device IDs (currently the
 * Intel HD Graphics 4600, PCI 8086:0412, confirmed on a real host); every other device
 * returns O0_GPU_BACKEND_NONE. fill_caps records the generation-correct capability set
 * attached to a matched device. */
extern const struct o0_gpu_backend_ops o0_gpu_hsw_backend;

#endif /* O0_GPU_HSW_H */
