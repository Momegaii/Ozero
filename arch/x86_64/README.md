# x86-64 architecture area

This directory owns architecture-specific code only: entry/exit assembly, CPU feature discovery, descriptor tables, interrupt handling, low-level context switching, and architecture-defined interfaces.

Keep generic kernel policy outside this directory. The initial implementation target is x86-64; Intel Haswell is an early physical validation target, not a substitute for testing other x86-64 machines.
