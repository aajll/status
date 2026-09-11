# Design

`status` is a banked-bitfield status register library for embedded firmware. It stores fault, warning, and info state as bits in arrays of `uint16_t`, addresses each bit with a compact encoded ID, and makes single-bit updates atomic without a caller-visible lock. This document records the model, the contracts, and the reasoning. The callable interface is in [docs/api.md](api.md); integration is in [docs/integration.md](integration.md).

## Goals and non-goals

Goals:

- A fixed, tiny memory footprint with no dynamic allocation.
- Per-bit set/clear that is atomic on hardware that offers lock-free 16-bit access, and interrupt-safe on the atomic backends without caller hooks.
- Loose coupling: the application defines its own ID names and class policy, and the library never allocates, never blocks, and never calls out except through the registered callback.
- Multiple independent instances through caller-owned `status_reg_t` storage.
- A single C11 source that builds for an 8-bit MCU and for a host.

Non-goals:

- No transaction over a group of bits. A caller that needs one supplies the lock; see [Concurrency](#concurrency).
- No event counting. Repeated sets coalesce into one bit.
- No compile-time guarantee that a fault ID is not passed to a warning setter; the ID is a plain integer.
- No persistence. State is RAM-only and is lost on reset.
- No general-purpose critical section. The hooks are the caller's, and the library only calls them on the backend that needs them.

## Model

A status ID is a `uint16_t` that packs a bank index in the high 12 bits and a bit position in the low 4 bits:

<!-- snippet: skip -->
```c
#define STATUS_ENCODE(bank, bit) \
        ((uint16_t)(((uint32_t)(bank) << 4u) | ((uint32_t)(bit) & 0x0Fu)))
```

Each bank holds 16 bits, so `NUM_STATUS_BANKS` banks hold `NUM_STATUS_BANKS * 16` IDs per class. Bank indices are valid from 0 to `NUM_STATUS_BANKS - 1`; `STATUS_UNSET_ID` (`0xFFFF`) deliberately encodes bank 4095 / bit 15, which the configurable range (1–4095 banks) can never produce, so it is unambiguous as a sentinel.

Three classes are independent: fault, warning, and info. Each class has its own bank array and its own tracker, which records the ID of the most recent set of that class. A `status_reg_t` holds all three arrays, the three trackers, and an error callback.

## Contracts

- **Error model.** Invalid input is non-fatal. The library calls the registered error callback, if any, with a `status_err_t` reason and an ID, and otherwise ignores the call. The callback runs synchronously in the calling context and may be an ISR, so it must be short and non-blocking. It runs outside internal protection and may re-enter the API.
- **NULL instances.** A `NULL` `status_reg_t` is silent, because there is no instance callback to report through. Mutators do nothing, predicates return `false`, and the last-ID getters return `STATUS_UNSET_ID`.
- **Trackers.** A set updates the bank and then the tracker; a clear updates the bank only, so trackers survive acknowledgement. A tracker records the ID of a set, and after two overlapping sets of the same class either ID may remain.
- **Initialisation.** `status_init()` and `status_reg_init()` require exclusive access: no other context may read or write the register during the call. The singleton keeps its callback across `status_init()`; a caller-owned register clears its callback on init.
- **Instances.** A `status_reg_t` must not be copied or moved while active. Its fields are implementation-owned.

## Concurrency

The atomic mechanism is selected at compile time in `status_conf.h`, as GCC/Clang `__atomic` → C11 `<stdatomic.h>` → a degenerate uniprocessor fallback. The two hardware-backed backends statically assert that the bank word (`uint16_t`) and the error-callback pointer are *always* lock-free on the target. A target that cannot satisfy that contract fails to compile rather than silently pulling in a hidden lock or a `libatomic` call that may not exist on bare metal.

A single-bit set or clear is a genuine atomic read-modify-write on the bank word. One context may therefore set a bit while another clears a different bit in the same bank without losing an update. The entire setter or clearer is not one transaction, however: a setter performs the bank read-modify-write and then a separate tracker store, and relaxed accesses need not be observed together or in order. A tracker read during overlapping setters can return an earlier ID or `STATUS_UNSET_ID`.

Scans and snapshots read each bank atomically but do not capture one consistent instant of a class. A caller that needs a consistent view across banks, or between banks and trackers, must serialise the operations itself.

On the no-atomics backend every bank and tracker access is wrapped in the critical-section hooks. With the default no-op hooks, accesses must neither overlap nor preempt each other. Interrupt masking protects one core only, not concurrent accesses from other cores.

### Contract: if `active` is set, `latched` is set

An active fault means "true now"; a latched fault means "occurred since the last acknowledgement". The library deliberately provides neither policy. A caller composes them from two `status_reg_t` instances and one lock; the recipe is in [docs/api.md](api.md#active-and-latched-faults).

The composition must uphold one invariant: **if `active` is set, `latched` is set**. The producer writes `active` and then `latched`; the acknowledgement clears `latched` only when `active` is clear. The two set calls are individually atomic but are not one transaction, so without serialisation this interleaving loses history:

1. `fault_ack` observes `active` as clear.
2. The producer sets `active`, then sets `latched`.
3. `fault_ack` clears `latched`.

The result is an active fault with no latched history. Holding one lock across the producer's two writes and across the acknowledgement's check-and-clear removes the interleaving. `fault_recover` needs no lock, because clearing `active` cannot create the state the invariant forbids.

Two lock-free alternatives exist, and both carry assumptions worth stating. Clearing `latched` and then re-reading `active`, re-latching if it has become set, is correct provided the clear is not reordered with the re-read: no interleaving can then lose history, because the producer writes `active` before `latched` while the acknowledgement reads `active` after clearing. The argument is subtle to review, so prefer the lock. Otherwise require producer quiescence through an explicit contract: call `fault_ack` only when no producer for that `id` can run.

An unlocked reader can still observe `active` set while `latched` is clear, because the producer's two writes are atomic only with respect to lock holders. The window spans the producer's second set call and closes when the producer releases the lock; a reader that needs a consistent view of both registers must take the same lock. If producers can run in an ISR, the lock must be ISR-safe or mask interrupts, and `fault_ack` must not be called from an ISR when the lock can block.

## Portability

The library uses only `uint16_t`, `size_t`, and `bool`, so it does not depend on the host's `int` width and builds for 8-bit MCUs where `int` is 16 bits. It allocates nothing and uses no recursion, `errno`, or exceptions.

The atomic backends require lock-free 16-bit access and a lock-free function pointer. Some cores (for example, some Cortex-M0-class parts) are 32-bit but cannot do a lock-free 16-bit read-modify-write, and some 8-bit targets cannot do an indivisible 16-bit access at all. Those targets select `STATUS_USE_NO_ATOMICS` and supply the hooks; see [docs/integration.md](integration.md#toolchains-without-lock-free-atomics).

The forced C11 backend is C-only: `STATUS_ATOMIC_QUAL` expands to `_Atomic`, which is not a C++ type qualifier. C++ consumers use the `__atomic` backend or `STATUS_USE_NO_ATOMICS`.

## Performance and cost

Storage is fixed at compile time. For one register the bank arrays cost `3 * NUM_STATUS_BANKS * sizeof(uint16_t)` bytes and the three trackers cost `3 * sizeof(uint16_t)`. With the default `NUM_STATUS_BANKS = 12` that is 72 bytes of banks plus 6 bytes of trackers, then the callback pointer and any padding to the pointer's alignment: 80 bytes where a pointer is 16-bit, and 84 bytes on a 32-bit target, where the 6 tracker bytes are padded to the pointer's 4-byte alignment. The singleton adds one more such struct.

Per-bit set and clear are a single atomic read-modify-write plus, for a set, one tracker store. No operation allocates, blocks, or loops over more than `NUM_STATUS_BANKS` words: `status_any()` scans one class and `status_snapshot()` copies one class, so both are `O(NUM_STATUS_BANKS)`.

## Verification

The unit suite drives the whole public API on every backend: the auto-selected default, the forced C11 `<stdatomic.h>` backend, and the forced no-atomics backend, including an instrumented-hooks build that checks that callback-pointer accesses are protected and that the callback runs after the critical section exits. A copied-source smoke test checks that the documented `-D` overrides apply to the header and the implementation alike. Concurrency tests race adjacent bits and the active/latched composition, and the same binaries are rebuilt under ThreadSanitizer.

Static analysis runs with `misch` (cppcheck-backed MISRA C:2023) across all three backends and must report zero findings. Coverage gates are 80% line and 70% branch. The exact commands, the toolchain requirements, and the analysis configuration are in [CONTRIBUTING.md](../CONTRIBUTING.md).
