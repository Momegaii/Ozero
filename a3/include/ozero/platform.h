/*
 * Ozero (o0) A3 platform contract - IF-008 version 1.1.
 *
 * A1 owns the kernel mechanisms.  The authoritative Stage 1 surface is
 * A1's include/ozero/mm.h, driver.h and irq.h.  This header includes those
 * declarations directly; it does not redeclare A1 types or invent a second
 * platform ABI.
 *
 * There is no IOMMU domain in Stage 1.  DMA addresses are physical and
 * untranslated.  Only trusted ring-0 drivers may use them.  There is no SG
 * pointer in o0_io_request: one request carries one contiguous buffer, and a
 * larger transfer is split into multiple buffers and requests.
 */
#ifndef O0_PLATFORM_H
#define O0_PLATFORM_H

#define O0_IF008_VERSION 0x00010001u

#include <ozero/types.h>
#include <ozero/status.h>
#include <ozero/irq.h>
#include <ozero/mm.h>
#include <ozero/driver.h>

#endif /* O0_PLATFORM_H */
