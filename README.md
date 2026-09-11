# status

[![CI](https://github.com/aajll/status/actions/workflows/ci.yml/badge.svg)](https://github.com/aajll/status/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**A lightweight C11 status register library for embedded systems.**

## Quick start

Copy `include/status.h`, `include/status_conf.h`, and `src/status.c` into your project, then define your IDs and use them. See [docs/integration.md](docs/integration.md) for the Meson subproject and pkg-config paths.

```c
#include "status.h"

/* Bank 0 bit 0: over-current fault. Bank 1 bit 0: high-temperature warning. */
#define STATUS_ID_FAULT_OVERCURRENT STATUS_ENCODE(0u, 0u)
#define STATUS_ID_WARN_HIGH_TEMP    STATUS_ENCODE(1u, 0u)

int main(void)
{
        status_init();
        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);
        status_set_warning(STATUS_ID_WARN_HIGH_TEMP);

        if (status_any(STATUS_CLASS_FAULT)) {
                uint16_t last = status_last_fault();
                (void)last;
        }

        return 0;
}
```

## Description

`status` tracks fault, warning, and info bits as compact banked bitfields. Each bank is a `uint16_t`, and a 16-bit status ID packs the bank index and the bit position, so a whole class of flags fits in a few words of RAM and a single ID addresses any bit. Typical uses are fault and warning management in a control loop, diagnostics snapshots for logging or a bus, and state flags shared with an RTOS task or an ISR.

- **Banked bitfields** - `uint16_t` banks, sized by `NUM_STATUS_BANKS`
- **Compact IDs** - `STATUS_ENCODE(bank, bit)` produces one 16-bit ID
- **Three independent classes** - fault, warning, and info state never mix
- **Lock-free per-bit updates** - set/clear is an atomic read-modify-write on the GNU `__atomic` and C11 backends, with no caller-supplied lock
- **Caller-owned instances** - a `status_reg_t` gives a subsystem its own banks, trackers, and callback
- **No dynamic memory** - fixed-size, no heap, no VLAs
- **Error callbacks** - invalid input is reported to a callback, or ignored
- **Snapshots** - copy a class for logging or transmission

Scope: the library provides no lock for multi-bit transactions, no event counting, and no persistence. Status IDs are plain integers, so the class contract is enforced by naming and review, not by the compiler. A caller that needs a consistent view across banks must serialise it.

## Compatibility

| Backend | Target requirement | Validation |
| --- | --- | --- |
| GCC/Clang `__atomic` (auto) | Lock-free 16-bit word and function pointer | Unit tests in CI, ThreadSanitizer, MISRA |
| C11 `<stdatomic.h>` | Same, C only | Unit tests in CI, MISRA |
| `STATUS_USE_NO_ATOMICS` | Caller critical-section hooks | Unit tests with instrumented hooks, MISRA |

Targets that cannot meet the lock-free requirement select the no-atomics backend and supply the hooks; see [docs/integration.md](docs/integration.md#toolchains-without-lock-free-atomics).

## Documentation

- [Integration](docs/integration.md) - requirements, install methods, building, toolchain selection
- [API reference](docs/api.md) - conventions, configuration, functions, recipes
- [Design](docs/design.md) - model, contracts, concurrency, portability, cost
- [Changelog](CHANGELOG.md) - release history

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before you open a pull request. Report security issues as described in [SECURITY.md](.github/SECURITY.md).

## License

MIT. See [LICENSE](LICENSE).
