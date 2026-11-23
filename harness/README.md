# Boot harness

| Field | Value |
| --- | --- |
| Owner | A4 |
| Status | Tested on the reference toolchain |

## What this is

`fixture/boot.S` is a 512-byte BIOS MBR that initialises COM1, prints the Stage 0 markers and halts.
It exists so the QEMU harness can be tested before A1 delivers a kernel.

**It is not the Ozero kernel and it is not a boot-protocol decision.** It is a harness self-test.
A1 owns the real boot path. Limine is a candidate in the briefing, not an approved decision, and
nothing here depends on it.

## Build

```sh
make build
```

Produces:

- `build/ozero-stage0.elf` — ELF with debug symbols
- `build/ozero-stage0.img` — raw 512-byte image
- `build/ozero-stage0.map` — linker map
- `build/manifest.json` — commit, toolchain and hashes

The linker script asserts that code, rodata, data and bss fit in 510 bytes, then writes the `55 AA`
signature at offset 510.

## Run against a real image

```sh
IMAGE=/path/to/ozero.img MARKERS=tests/qemu/expected-markers.txt make test
```

The harness only needs the image to be a multiple of 512 bytes with a valid boot signature. When A1
changes the image format, update `scripts/test-qemu.sh` and the artifact contract together.

## Changing the markers

Markers are a contract (IF-002). To change them:

1. Update `tests/qemu/expected-markers.txt` and the fixture together.
2. Update `docs/architecture/interfaces.md` if the meaning changes.
3. Run the negative test in `docs/process/integration-checklist.md`: a missing marker must fail.
