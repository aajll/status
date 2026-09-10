# status

[![CI](https://github.com/aajll/status/actions/workflows/ci.yml/badge.svg)](https://github.com/aajll/status/actions/workflows/ci.yml)

A lightweight C11 status register library for embedded systems.
Tracks faults, warnings, and info bits using banked bitfields encoded as compact 16-bit status IDs.

## Features

- **Banked bitfields** - Efficient storage using arrays of `uint16_t` across configurable banks
- **Compact IDs** - 16-bit IDs encode both bank and bit index via `STATUS_ENCODE`
- **Three status classes** - Separate fault, warning, and info registers
- **No dynamic memory** - Fixed-size operations, no `malloc` / `free`
- **Atomic bit operations** - Set/clear of a single bit is a lock-free atomic read-modify-write; interrupt- and core-safe with no caller hooks
- **MISRA-oriented** - no VLAs, no dynamic allocation, written with MISRA C:2023 / IEC 61508 in mind
- **Error callbacks** - Runtime notification of invalid IDs or null pointers
- **Snapshot API** - Bulk-copy registers for logging or diagnostics

## Installation

### Copy-in (recommended for embedded targets)

Copy these files into your project tree:

```
include/status.h
include/status_conf.h
src/status.c
```

Then include the header:

```c
#include "status.h"
```

### Meson subproject

Add this repo as a wrap dependency or subproject:

```meson
status_dep = dependency('status', fallback : ['status', 'status_dep'])
```

## Quick Start

### 1. Define Status IDs

Create a `status_ids.h` for your application using the `STATUS_ENCODE` macro:

```c
/* status_ids.h */
#include "status.h"

// Bank 0: Power Faults
#define STATUS_ID_FAULT_OVERCURRENT    STATUS_ENCODE(0u, 0u)
#define STATUS_ID_FAULT_OVERVOLTAGE    STATUS_ENCODE(0u, 1u)

// Bank 1: Thermal Warnings
#define STATUS_ID_WARN_HIGH_TEMP       STATUS_ENCODE(1u, 0u)
```

Each bank holds 16 bits. `bank` must be less than `NUM_STATUS_BANKS`; `bit` must be 0–15.

### 2. Integrate

```c
#include "status_ids.h"

void app_init(void)
{
    status_init();

    /* Optional: register error callback */
    status_set_err_callback(my_error_handler);
}

void check_power(void)
{
    if (voltage > MAX_VOLTAGE) {
        status_set_fault(STATUS_ID_FAULT_OVERVOLTAGE);
    } else {
        status_clear_fault(STATUS_ID_FAULT_OVERVOLTAGE);
    }
}

void check_system(void)
{
    if (status_any(STATUS_CLASS_FAULT)) {
        /* at least one fault is active */
        uint16_t last = status_last_fault();
    }
}
```

### Caller-owned registers

Use `status_reg_t` when subsystems need independent status spaces. Initialise
storage before concurrent use; the existing `status_*` functions continue to
use the library's default register.

```c
static status_reg_t motor_status;

void motor_init(void)
{
    status_reg_init(&motor_status);
    status_reg_set_err_callback(&motor_status, my_error_handler);
}

void motor_check(void)
{
    status_reg_set_fault(&motor_status, STATUS_ID_FAULT_OVERCURRENT);
}
```

### Compose active and latched faults

An active fault means "true now". A latched fault means "occurred since the last acknowledgement". Keep these policies outside the primitive by using two caller-owned registers.

```c
static status_reg_t active;
static status_reg_t latched;

void fault_registers_init(void)
{
    status_reg_init(&active);
    status_reg_init(&latched);
}

/* The primitive supplies no lock. Serialise the producer and acknowledgement
   paths with one appropriate to the target: an interrupt-masking section on a
   single core, or an ISR-safe lock when faults can be reported from an ISR. */
static void faults_lock(void);
static void faults_unlock(void);

/* Producer write path: record both states while holding the lock. */
void fault_report(uint16_t id)
{
    faults_lock();
    status_reg_set_fault(&active, id);
    status_reg_set_fault(&latched, id);
    faults_unlock();
}

/* Condition recovery clears only the live state. This path cannot discard
   history, so it needs no lock. */
void fault_recover(uint16_t id)
{
    status_reg_clear_fault(&active, id);
}

/* The acknowledgement authority clears history after recovery. The check and
   the clear must be one serialised unit. */
void fault_ack(uint16_t id)
{
    faults_lock();
    if (!status_reg_is_fault_set(&active, id)) {
        status_reg_clear_fault(&latched, id);
    }
    faults_unlock();
}
```

Update both registers in the producer write path. A polling reader can miss a transient condition and cannot maintain the latch reliably.

The two set calls are individually atomic but are not one transaction. Without a lock the acknowledgement can clear history the producer has just recorded:

1. `fault_ack` observes `active` as clear.
2. The producer sets `active`, then sets `latched`.
3. `fault_ack` clears `latched`.

The result is an active fault with no latched history. Holding `faults_lock()` across the producer's two writes and across the acknowledgement's check and clear removes that interleaving. `fault_recover` needs no lock, because clearing `active` cannot create the state this composition must avoid.

Serialising the group upholds one invariant: **if `active` is set, `latched` is set**. The producer's two writes are atomic only with respect to lock holders, so an unlocked reader can still observe `active` set while `latched` is clear. The window spans the producer's second `status_reg_set_fault()` call and closes when the producer releases the lock; a reader that needs a consistent view of both registers must take the same lock.

Two lock-free alternatives carry assumptions worth stating. Clearing `latched` and then re-reading `active`, re-latching if it has become set, is correct provided the clear is not reordered with the re-read: no interleaving can then lose history, because the producer writes `active` before `latched` while the acknowledgement reads `active` after clearing. The argument is subtle to review, so prefer the lock. Otherwise require producer quiescence through an explicit contract: `fault_ack` is called only when no producer for `id` can run. If producers can run in an ISR, the lock must be ISR-safe or mask interrupts, and `fault_ack` must not be called from an ISR when the lock can block.

## Configuration

Override options with compiler definitions passed consistently when compiling
`status.c` and every translation unit that includes `status.h` (for example,
`-DNUM_STATUS_BANKS=8`):

| Option                                                                        | Description                                                                       | Default |
| ----------------------------------------------------------------------------- | --------------------------------------------------------------------------------- | ------- |
| `NUM_STATUS_BANKS`                                                            | Number of `uint16_t` banks per status class                                       | `12`    |
| `STATUS_USE_GNU_ATOMICS` / `STATUS_USE_C11_ATOMICS` / `STATUS_USE_NO_ATOMICS` | Force the atomic backend instead of auto-discovery                                | auto    |
| `STATUS_ENTER_CRITICAL()` / `STATUS_EXIT_CRITICAL()`                          | Critical-section hooks, consulted **only** on the `STATUS_USE_NO_ATOMICS` backend | no-op   |

### C++ consumers

`status.h` is wrapped in `extern "C"` and compiles as C++17 with the default
auto-discovered backend (GNU/Clang `__atomic`) or with `STATUS_USE_NO_ATOMICS`.
The forced `STATUS_USE_C11_ATOMICS` backend is **C-only**: `STATUS_ATOMIC_QUAL`
expands to `_Atomic`, which is not a C++ type qualifier even in C++23. A C++
translation unit that cannot use `__atomic` builtins must select
`STATUS_USE_NO_ATOMICS` and supply the [critical-section hooks](#targets-without-lock-free-atomics).

## Development error handling

Invalid input is a no-op when no error callback is registered. During development and testing, register a loud application callback so a bad ID, class, pointer, or length fails visibly. The handler can log, assert, or stop the test according to the target's policy.

Keep the handler safe for every calling context. It can run synchronously from an ISR, so do not block or call an ISR-unsafe logger there. Production can replace it with a nonfatal diagnostic handler or deregister it with `status_set_err_callback(NULL)`.

## Concurrency

Setting or clearing a single status bit is a genuine atomic read-modify-write on the bank word, so one context may set a bit while another clears a different bit in the same bank without either update being lost. On the two hardware-backed backends this is interrupt- and core-safe by construction, and **no caller-supplied critical section is required** for per-bit operations.

The atomic mechanism is chosen at compile time in `status_conf.h`, auto-discovered as GCC/Clang `__atomic` → C11 `<stdatomic.h>` → a degenerate uniprocessor fallback. The two atomic backends statically assert that the bank word (`uint16_t`) and the error-callback pointer are _always_ lock-free on the target: a target that cannot satisfy that contract fails to compile rather than silently pulling in a hidden lock.

Atomicity applies to **each bank operation, not necessarily a whole call**. A setter performs an atomic bank read-modify-write followed in program order by a separate atomic tracker store. Other contexts need not observe these relaxed accesses together or in that order. See [Last Set](#last-set) for the tracker contract.

Scans and snapshots read each bank atomically, but do not capture one consistent instant of the whole class. This applies to both singleton and caller-owned functions. Callers must serialise operations when they need a consistent view across banks or between banks and trackers.

On the no-atomics backend, these guarantees require protection for every accessing context. With default no-op hooks, accesses must neither overlap nor preempt each other. Interrupt masking protects one core only, not concurrent accesses from other cores.

### Targets without lock-free atomics

On a toolchain with no `<stdatomic.h>` and no `__atomic` builtins (or where 16-bit atomics are not lock-free, e.g. some Cortex-M0-class cores), select `STATUS_USE_NO_ATOMICS` and supply the critical-section hooks. They are consulted only on this backend, must be defined as a matched pair, and must **save and restore** interrupt state rather than unconditionally re-enabling interrupts on exit. Every bank and tracker load and store on this backend is wrapped in the hooks: an 8-bit target cannot guarantee that a 16-bit access is indivisible, so the section is what makes the access atomic:

```c
/* Save PRIMASK, then disable interrupts */
#define STATUS_ENTER_CRITICAL() \
        uint32_t _status_irq_state = __get_PRIMASK(); __disable_irq()

/* Restore PRIMASK to whatever it was before ENTER */
#define STATUS_EXIT_CRITICAL() \
        __set_PRIMASK(_status_irq_state)
```

## Building

```sh
# Library only (release)
meson setup build --buildtype=release
meson compile -C build

# With unit tests (default)
meson setup build --buildtype=debug
meson compile -C build
meson test -C build

# Disable tests
meson setup build -Dbuild_tests=false
```

## API Reference

### Lifecycle

```c
void status_init(void);
void status_set_err_callback(status_err_cb_t cb);
```

### Set / Clear

```c
void status_set_fault(uint16_t id);
void status_set_warning(uint16_t id);
void status_set_info(uint16_t id);

void status_clear_fault(uint16_t id);
void status_clear_warning(uint16_t id);
void status_clear_info(uint16_t id);

bool status_test_and_clear_fault(uint16_t id);
bool status_test_and_clear_warning(uint16_t id);
bool status_test_and_clear_info(uint16_t id);
```

Test-and-clear returns the previous bit state and clears it at one atomic point.
Repeated sets before consumption coalesce; this is not an event counter.

### Query

```c
bool status_is_fault_set(uint16_t id);
bool status_is_warning_set(uint16_t id);
bool status_is_info_set(uint16_t id);

bool status_any(enum status_class cls);
void status_clear_all(enum status_class cls);
```

### Last Set

```c
uint16_t status_last_fault(void);
uint16_t status_last_warning(void);
uint16_t status_last_info(void);
```

Returns the class tracker, not a snapshot of currently active bits. Use `status_any()` to check for active bits. Clear operations preserve trackers. Initialisation resets them to `STATUS_UNSET_ID`.

The tracker records tracker-store order, not call-completion order. A read during overlapping setters may still return an earlier ID or `STATUS_UNSET_ID`, even after the bank bits have changed. For two valid setters of the same class, synchronise with both completions before reading the tracker. Either ID may remain if no other setter or initialisation intervenes. Without intervening clears, both bits remain set. These rules also apply to the caller-owned last-ID getters.

### Snapshot

```c
void status_snapshot(enum status_class cls, uint16_t *dst, size_t len);
bool status_snapshot_next(const uint16_t *snapshot, size_t len, size_t *cursor,
                          uint16_t *id);
```

Copies up to `len` banks for the given class into `dst`, capped at
`NUM_STATUS_BANKS`. Passing `len == 0` reports an error.

`status_snapshot_next()` enumerates a buffer filled by `status_snapshot()` or `status_reg_snapshot()`. It returns `true` with the next active ID, or `false` at the end or for a NULL pointer argument. `len` is the number of valid banks in the buffer, capped at `NUM_STATUS_BANKS`. Initialise `cursor` to zero and preserve it between calls. Keep the buffer unchanged throughout traversal. Enumeration does not read the live register. Take a new snapshot to include later changes.

### Caller-Owned Registers

A `status_reg_t` provides banks, trackers, and an error callback independent of the singleton. Allocate it statically or automatically and initialise it before use. Initialisation and re-initialisation require exclusive access: no other context may read or write the register. Initialisation does not use concurrent atomic stores on the GNU and C11 backends. Synchronise with other users before access resumes.

```c
void status_reg_init(status_reg_t *reg);
void status_reg_set_err_callback(status_reg_t *reg, status_err_cb_t cb);

void status_reg_set_fault(status_reg_t *reg, uint16_t id);
void status_reg_set_warning(status_reg_t *reg, uint16_t id);
void status_reg_set_info(status_reg_t *reg, uint16_t id);

void status_reg_clear_fault(status_reg_t *reg, uint16_t id);
void status_reg_clear_warning(status_reg_t *reg, uint16_t id);
void status_reg_clear_info(status_reg_t *reg, uint16_t id);

bool status_reg_test_and_clear_fault(status_reg_t *reg, uint16_t id);
bool status_reg_test_and_clear_warning(status_reg_t *reg, uint16_t id);
bool status_reg_test_and_clear_info(status_reg_t *reg, uint16_t id);

bool status_reg_is_fault_set(const status_reg_t *reg, uint16_t id);
bool status_reg_is_warning_set(const status_reg_t *reg, uint16_t id);
bool status_reg_is_info_set(const status_reg_t *reg, uint16_t id);

bool status_reg_any(const status_reg_t *reg, enum status_class cls);
void status_reg_clear_all(status_reg_t *reg, enum status_class cls);

uint16_t status_reg_last_fault(const status_reg_t *reg);
uint16_t status_reg_last_warning(const status_reg_t *reg);
uint16_t status_reg_last_info(const status_reg_t *reg);

void status_reg_snapshot(const status_reg_t *reg, enum status_class cls,
                         uint16_t *dst, size_t len);
```

Every function takes the register as its first argument and otherwise mirrors the singleton of the same name, including the invalid-input model and the [concurrency contract](#concurrency). Two differences matter:

- A NULL register is silent rather than reported, because there is no instance callback to report through. Mutators, `status_reg_init()`, `status_reg_clear_all()`, and `status_reg_snapshot()` do nothing; predicates and test-and-clear return `false`; the last-ID getters return `STATUS_UNSET_ID`.
- `status_reg_init()` also clears that register's error callback. The singleton `status_init()` deliberately preserves its callback so errors during re-initialisation are still reported; re-register with `status_reg_set_err_callback()` after initialising a caller-owned register.

Do not copy or move a register while it is active. Multiple subsystems may share it under the [concurrency contract](#concurrency). Callbacks are per register and run synchronously in the calling context, which may be an ISR. They must be short, non-blocking, and safe for every calling context. They run outside internal protection and may re-enter the status API.

### ID Encoding Helpers

`STATUS_ENCODE` packs a bank index and bit position into a single 16-bit value:

![STATUS_ENCODE bit layout](docs/img/status_encode_breakdown.svg)

```c
#define STATUS_ENCODE(bank, bit)   /* compile-time: encode bank + bit → uint16_t */

static inline uint16_t status_bank(uint16_t id);  /* extract bank index */
static inline uint16_t status_bit(uint16_t id);   /* extract bit index  */
```

## Use Cases

1. **Fault management** - Track and query active faults in safety-critical control loops
2. **Warning escalation** - Separate warning state from hard fault state
3. **Diagnostics** - Snapshot registers for logging or transmission over CAN/UART
4. **State encoding** - Compact event/status flags in RTOS tasks or state machines
5. **ISR-safe signalling** - Set status bits from interrupt context with critical section hooks

## Notes

| Topic                 | Note                                                                                                                                                                                                                                                                                                                                    |
| --------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Memory**            | All storage is statically allocated; no heap use                                                                                                                                                                                                                                                                                        |
| **Thread safety**     | Single-bit set/clear is atomic and lock-free on the default backends; multi-bit observations are not a single consistent snapshot. See [Concurrency](#concurrency)                                                                                                                                                                      |
| **Error handling**    | Invalid IDs invoke the registered error callback (if any) and are otherwise ignored                                                                                                                                                                                                                                                     |
| **Version header**    | `status_version.h` is auto-generated by Meson and placed in the build output directory                                                                                                                                                                                                                                                  |
| **Status classes**    | Three independent register sets: `STATUS_CLASS_FAULT`, `STATUS_CLASS_WARNING`, `STATUS_CLASS_INFO`                                                                                                                                                                                                                                      |
| **ID class contract** | Status IDs are plain `uint16_t` values encoding only bank + bit. The library cannot enforce at compile time that a fault ID is passed to `status_set_fault()` rather than `status_set_warning()`. Use the naming convention (`STATUS_ID_FAULT_*`, `STATUS_ID_WARN_*`, `STATUS_ID_INFO_*`) and code review to prevent cross-class usage. |
