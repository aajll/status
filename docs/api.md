# API reference

Reference for the `status` public interface. The single source of truth is [`include/status.h`](../include/status.h); this document restates the contracts and adds the conventions and recipes a caller needs. The design and the concurrency model are in [docs/design.md](design.md); integration is in [docs/integration.md](integration.md).

## Conventions

- **Status IDs** are plain `uint16_t` values produced by `STATUS_ENCODE(bank, bit)`. The library cannot tell a fault ID from a warning ID at compile time; keep the `STATUS_ID_FAULT_*`, `STATUS_ID_WARN_*`, and `STATUS_ID_INFO_*` naming convention and prevent cross-class use in review.
- **Invalid input is non-fatal.** A bad ID, class, pointer, or length invokes the registered error callback, if any, and otherwise does nothing. A `NULL` register is always silent, because there is no instance callback to report through. Predicate functions return `false`; the last-ID getters return `STATUS_UNSET_ID`.
- **No dynamic memory.** Every object is caller-allocated, statically or automatically, and fixed in size.
- **Callbacks run synchronously** in the calling context, which may be an ISR. They must be short, non-blocking, and safe for every calling context. They run outside internal protection and may re-enter the status API.
- **Ownership.** A `status_reg_t` is caller-owned. Do not inspect, modify, or copy its members while it is active.

## Configuration

Override these with compiler definitions, applied consistently to `status.c` and to every translation unit that includes `status.h` (for example, `-DNUM_STATUS_BANKS=8`).

| Option | Description | Default |
| --- | --- | --- |
| `NUM_STATUS_BANKS` | Number of `uint16_t` banks per status class. Must be 1–4095. | `12` |
| `STATUS_USE_GNU_ATOMICS` | Force the GCC/Clang `__atomic` backend. | auto |
| `STATUS_USE_C11_ATOMICS` | Force the C11 `<stdatomic.h>` backend (C only). | auto |
| `STATUS_USE_NO_ATOMICS` | Force the uniprocessor fallback and require the critical-section hooks. | auto |
| `STATUS_ENTER_CRITICAL()` | Critical-section entry hook, consulted **only** on the no-atomics backend. | no-op |
| `STATUS_EXIT_CRITICAL()` | Critical-section exit hook, the matched partner of the entry hook. | no-op |

Exactly one backend is selected. Out-of-range `NUM_STATUS_BANKS` fails at compile time. See [docs/integration.md](integration.md) for the hooks' contract and the backend ladder.

## Types

The header defines `enum status_class` (`STATUS_CLASS_FAULT`, `STATUS_CLASS_WARNING`, `STATUS_CLASS_INFO`), `status_err_t`, the callback type `status_err_cb_t`, and the caller-owned register `status_reg_t`. Their exact definitions are in [`include/status.h`](../include/status.h); a register's fields are implementation-owned.

`status_err_t` values:

| Value | Meaning |
| --- | --- |
| `STATUS_ERR_INVALID_ID` | Unrecognised `enum status_class` value |
| `STATUS_ERR_INVALID_BANK` | Bank index greater than or equal to `NUM_STATUS_BANKS` |
| `STATUS_ERR_INVALID_LEN` | Zero-length argument to a snapshot call |
| `STATUS_ERR_NULL_PTR` | `NULL` pointer argument |

## Singleton API

The singleton register is the library's default instance. It is statically allocated, and every `status_*` function below operates on it.

### Lifecycle

```c
void status_init(void);
void status_set_err_callback(status_err_cb_t cb);
```

`status_init()` clears every bank and resets all three trackers to `STATUS_UNSET_ID`. It deliberately preserves the registered callback, so errors during re-initialisation are still reported. Deregister with `status_set_err_callback(NULL)`.

### Set and clear

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

Test-and-clear returns the previous bit state and clears it at one atomic point. Repeated sets before consumption coalesce; this is not an event counter.

### Query

```c
bool status_is_fault_set(uint16_t id);
bool status_is_warning_set(uint16_t id);
bool status_is_info_set(uint16_t id);

bool status_any(enum status_class cls);
void status_clear_all(enum status_class cls);
```

`status_any()` reports whether at least one bit of the class is set. `status_clear_all()` clears every bank of the class and leaves the trackers untouched.

### Last set

```c
uint16_t status_last_fault(void);
uint16_t status_last_warning(void);
uint16_t status_last_info(void);
```

These return the class tracker, not a snapshot of the currently active bits; use `status_any()` to check for active bits. Clear operations preserve the tracker, and initialisation resets it to `STATUS_UNSET_ID`.

The tracker records tracker-store order, not call-completion order. A read during overlapping setters may still return an earlier ID or `STATUS_UNSET_ID`, even after the bank bits have changed. For two valid setters of the same class, synchronise with both completions before reading the tracker. Either ID may remain if no other setter or initialisation intervenes. Without intervening clears, both bits remain set.

### Snapshot

```c
void status_snapshot(enum status_class cls, uint16_t *dst, size_t len);
bool status_snapshot_next(const uint16_t *snapshot, size_t len, size_t *cursor,
                          uint16_t *id);
```

`status_snapshot()` copies up to `len` banks of the class into `dst`, capped at `NUM_STATUS_BANKS`. Passing `len == 0` reports an error.

`status_snapshot_next()` enumerates a buffer filled by `status_snapshot()` or `status_reg_snapshot()`. It returns `true` with the next active ID, or `false` at the end or for a `NULL` pointer argument. `len` is the number of valid banks in the buffer, capped at `NUM_STATUS_BANKS`. Initialise `cursor` to zero and preserve it between calls, and keep the buffer unchanged throughout traversal. Enumeration does not read the live register; take a new snapshot to include later changes.

### ID encoding helpers

`STATUS_ENCODE` packs a bank index and a bit position into a single 16-bit value; `status_bank()` and `status_bit()` unpack it.

![STATUS_ENCODE bit layout](img/status_encode_breakdown.svg)

<!-- snippet: skip -->
```c
#define STATUS_ENCODE(bank, bit)   /* compile-time: encode bank + bit */
```

```c
static inline uint16_t status_bank(uint16_t id);  /* extract bank index */
static inline uint16_t status_bit(uint16_t id);   /* extract bit index  */
```

Each bank holds 16 bits. `bank` must be less than `NUM_STATUS_BANKS` and `bit` must be 0–15; `STATUS_ENCODE` masks `bit` but not `bank`. `STATUS_UNSET_ID` (`0xFFFF`) encodes bank 4095 / bit 15, which is never a valid application ID.

## Caller-owned registers

A `status_reg_t` provides banks, trackers, and an error callback independent of the singleton. Allocate it statically or automatically and initialise it before use.

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

- A `NULL` register is silent rather than reported, because there is no instance callback to report through. Mutators, `status_reg_init()`, `status_reg_clear_all()`, and `status_reg_snapshot()` do nothing; predicates and test-and-clear return `false`; the last-ID getters return `STATUS_UNSET_ID`.
- `status_reg_init()` also clears that register's error callback. The singleton `status_init()` preserves its callback instead. Re-register with `status_reg_set_err_callback()` after initialising a caller-owned register.

Do not copy or move a register while it is active. Multiple subsystems may share one under the concurrency contract. Initialisation and re-initialisation require exclusive access: no other context may read or write the register, and initialisation does not use atomic stores on the GNU and C11 backends.

## Concurrency

Setting or clearing a single bit is a genuine atomic read-modify-write on the bank word, so one context may set a bit while another clears a different bit in the same bank without losing an update. On the two hardware-backed backends this is interrupt- and core-safe, and **no caller-supplied critical section is required** for per-bit operations. On the no-atomics backend every access must be protected by the caller's critical-section hooks.

Atomicity applies to **each bank operation, not necessarily a whole call**. A setter performs an atomic bank read-modify-write followed by a separate atomic tracker store; other contexts need not observe the two together or in that order. Scans and snapshots read each bank atomically but do not capture one consistent instant of the whole class, for both the singleton and caller-owned functions. Serialise operations when you need a consistent view across banks or between banks and trackers.

The full model, the tracker contract, and the reasons behind it are in [docs/design.md](design.md#concurrency).

## Recipes

### Caller-owned register

Use a `status_reg_t` when a subsystem needs its own status space.

```c
static status_reg_t motor_status;

void motor_init(void)
{
        status_reg_init(&motor_status);
        status_reg_set_err_callback(&motor_status, my_error_handler);
}

void motor_check(void)
{
        status_reg_set_fault(&motor_status, STATUS_ENCODE(0u, 0u));
}
```

### Active and latched faults

An active fault means "true now". A latched fault means "occurred since the last acknowledgement". Keep these policies outside the primitive by using two caller-owned registers and one lock.

```c
static status_reg_t active;
static status_reg_t latched;

void fault_registers_init(void)
{
        status_reg_init(&active);
        status_reg_init(&latched);
}

/* The primitive supplies no lock. Define these for your target: an
   interrupt-masking section on a single core, or an ISR-safe lock when faults
   can be reported from an ISR. */
void faults_lock(void);
void faults_unlock(void);

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

The two set calls are individually atomic but are not one transaction. Without the lock the acknowledgement can clear history the producer has just recorded. The invariant, the interleaving, and the lock-free alternatives are analysed in [docs/design.md](design.md#contract-if-active-is-set-latched-is-set).

### Development error handling

Invalid input is a no-op when no callback is registered. During development and testing, register a loud callback so a bad ID, class, pointer, or length fails visibly.

```c
static void loud_error_handler(status_err_t err, uint16_t id)
{
        (void)id;
        /* Log, assert, or halt according to the target's policy. Keep it safe
           for ISR context; do not block or call an ISR-unsafe logger. */
        (void)err;
}
```

Production can replace it with a nonfatal diagnostic handler or deregister it with `status_set_err_callback(NULL)`.
