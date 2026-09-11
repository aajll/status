# Integration guide

How to add `status` to a firmware project and build it. This guide covers requirements, the supported install methods, toolchain selection, and the build commands. The API itself is in [docs/api.md](api.md); the design and the concurrency contracts are in [docs/design.md](design.md).

## Requirements

- A C11 compiler for the library. C++17 consumers can use the header through `extern "C"`, with the limits in [C++ consumers](#c-consumers).
- One of:
- GCC or Clang with the `__atomic` builtins (the auto-discovered default), or
- a C11 toolchain with `<stdatomic.h>`, or
- neither, in which case you supply the critical-section hooks described in [the no-atomics hooks](#toolchains-without-lock-free-atomics).
- Meson and Ninja only if you build from source or use the Meson subproject.

`status` has no third-party dependencies and does not allocate memory.

## Copy-in (recommended for embedded targets)

Copy three files into your project tree:

```text
include/status.h
include/status_conf.h
src/status.c
```

Compile `src/status.c` as part of your build and put `include/` on the include path. Then include the header:

```c
#include "status.h"
```

The copy-in path is the one to prefer on bare-metal targets, because it keeps the library in your own build and lets you set the configuration macros per translation unit.

## Meson subproject

Add the repository as a wrap dependency or subproject, then use the declared dependency:

```meson
status_dep = dependency('status', fallback : ['status', 'status_dep'])
```

Meson also generates pkg-config metadata, so non-Meson consumers can use the installed package. Configuration macros (see [docs/api.md](api.md#configuration)) must be defined consistently for the library and for every translation unit that includes `status.h`.

## Building from source

```sh
# Library only (release)
meson setup build --buildtype=release
meson compile -C build

# With unit tests (default is tests off)
meson setup build --buildtype=debug -Dbuild_tests=true
meson compile -C build
meson test -C build
```

`meson setup` generates `status_version.h` in the build directory. For the contributor workflow, including sanitisers, coverage, and the MISRA analysis, see [CONTRIBUTING.md](../CONTRIBUTING.md).

## Toolchains without lock-free atomics

The default backends require a 16-bit bank word and the error-callback pointer to be *always* lock-free. A target that cannot satisfy that contract fails to compile rather than silently using a hidden lock; see [docs/design.md](design.md#portability). On such a target select `STATUS_USE_NO_ATOMICS` and supply a matched pair of critical-section hooks. They are consulted only on this backend, and every bank and tracker access is wrapped in them, so a 16-bit access cannot tear on an 8-bit target.

The hooks must **save and restore** interrupt state, not unconditionally re-enable interrupts on exit:

<!-- snippet: skip -->
```c
/* Save PRIMASK, then disable interrupts */
#define STATUS_ENTER_CRITICAL() \
        uint32_t _status_irq_state = __get_PRIMASK(); __disable_irq()

/* Restore PRIMASK to whatever it was before ENTER */
#define STATUS_EXIT_CRITICAL() \
        __set_PRIMASK(_status_irq_state)
```

The snippet is CMSIS-specific, so it is illustrative; adapt it to your target's interrupt API. The full contract for the no-atomics backend is in [docs/design.md](design.md#concurrency).

## C++ consumers

`status.h` is wrapped in `extern "C"` and compiles as C++17 with the auto-discovered backend (GNU/Clang `__atomic`) or with `STATUS_USE_NO_ATOMICS`. The forced `STATUS_USE_C11_ATOMICS` backend is **C-only**: `STATUS_ATOMIC_QUAL` expands to `_Atomic`, which is not a C++ type qualifier even in C++23. A C++ translation unit that cannot use `__atomic` builtins must select `STATUS_USE_NO_ATOMICS` and supply the critical-section hooks above.

## Cross builds and validation

The library is data-model independent: it uses only `uint16_t`, `size_t`, and `bool`, and its correctness does not depend on the host's `int` width. The project does not ship a cross-build matrix. When you cross-compile, you own the memory model, the atomic availability, and the critical-section hooks. Record the results with your own toolchain; the repository's own verification is described in [docs/design.md](design.md#verification).
