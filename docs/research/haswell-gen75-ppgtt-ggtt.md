# Haswell Gen7.5 PPGTT/GGTT Research Dossier

Status: Research only; no implementation.

## Executive summary
Haswell Gen7.5 uses a two-level PPGTT (page directory -> page table), 4 KiB pages, and a separate GGTT. Exact entry formats and invalidation sequences must be verified against PRM. Many details UNKNOWN pending PRM access.

## Source inventory
- Intel Haswell Graphics PRM Memory Views (primary)
- Intel SDM
- GitHub Igalia archive (reference)

## PPGTT architecture
- Two-level hierarchy (directory -> table) per PRM.
- Page size 4 KiB.
- Address width and entry formats: UNKNOWN (requires PRM).

## Entry formats
- Bit positions, masks: UNKNOWN.

## GGTT architecture
- Separate global address space; differs from PPGTT. Details UNKNOWN.

## Invalidation
- Exact sequences UNKNOWN; must not be invented.

## Allocation requirements
- Page-directory/table page alignment likely 4 KiB; contingency.

## Ozero mapping
- Generic VM interfaces remain hardware-independent; Haswell backend to encode verified structures.

## Unresolved
- Entry bit layouts, invalidation commands, GGTT registers.

## References
- Haswell PRM Memory Views PDF.