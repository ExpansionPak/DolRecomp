# core

Shared CPU support code used by generated output and tests.

This layer owns:

- fixed-width types and endian helpers
- `CPUState`
- guest memory access helpers
- CPU behavior helpers that are awkward to inline in emitted C
- external memory/MMIO and host-call hooks.
