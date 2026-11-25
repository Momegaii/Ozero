# Physical boot gate — operator checklist

Status: **prepared, not authorised, not executed.** Only the Project Director decides when a
physical boot happens. This checklist is not a schedule and not permission to run one.

Target: the Director's Windows 8.1 host, Intel Core i5-4460, HD 4600 (`8086:0412` rev 06).
Physical evidence is separate from QEMU and CI evidence; nothing below may be inferred from an
emulator run.

## Before power-on

- [ ] Director explicitly authorises the boot and the exact ISO checksum.
- [ ] Record the build commit from `OZERO: build commit=` in the serial log and from the boot
      header. A log without a commit is not accepted.
- [ ] Build with `make iso`; record the SHA-256 of `build/ozero.iso`.
- [ ] BIOS: Secure Boot off, CSM/UEFI mode recorded, USB boot enabled.
- [ ] Serial capture connected and logging before power-on. Framebuffer capture ready.
- [ ] No GPU engine command, reset or MMIO write is planned. The boot is a diagnostic run only.

## What must appear

- [ ] `OZERO: build commit=` and the CPU/platform header.
- [ ] `OZERO: firmware=BIOS` or `firmware=UEFI` matching the actual boot mode.
- [ ] `OZERO: acpi cpus=… ioapics=… overrides=… lapic=… bsp_apic=…`.
- [ ] `OZERO: smp cpus=N online=N` with N equal to the number of logical CPUs the firmware
      reports. If an AP fails, the log must show the shortfall; do not edit the number.
- [ ] `OZERO: tlb shootdown OK` when more than one CPU is online.
- [ ] `OZERO: timer=lapic hz=100` (or `timer=pit` with the reason).
- [ ] `OZERO: smep=on smap=on/off` reflecting the real CPU. The i5-4460 is expected to have
      SMEP and may or may not have SMAP; the log decides, not this document.
- [ ] The Wave 2 markers (`user entry OK`, `syscall exit status=0`, `user fault contained`,
      `syscall bad pointer rejected`, `user churn OK processes=16`).
- [ ] The eight-bar framebuffer pattern and its checksum. A serial marker without a matching
      framebuffer is not enough.
- [ ] The kernel reaches its final hold state and does not reboot.
- [ ] No `PANIC`, no unexpected `EXCEPTION`, and no AP left offline without a logged reason.

## Stop conditions

Stop and preserve the log if any of these occurs:

- A marker is missing or an AP never comes online.
- `EXCEPTION` appears outside the three deliberate user-fault tests.
- `smep=on` is printed on a CPU that CPUID says lacks SMEP, or `smap=on` on a CPU that lacks
  SMAP. That would indicate a kernel reporting error.
- The framebuffer pattern differs from the serial checksum.
- The machine reboots instead of holding.
- Any GPU MMIO access is observed. The GPU discovery path is host-tested to perform none; a
  physical write would contradict that test and must be investigated before any engine work.

## Evidence record

Record, in `docs/PROJECT_STATUS.md`, a separate **physical** row with:

- date, machine, CPU, GPU, firmware mode, BIOS version;
- ISO SHA-256 and build commit;
- serial log path and framebuffer image path;
- every marker above, or the first failure;
- the operator's name and whether the Director observed it.

Do not merge this row into the CI or QEMU tables.
