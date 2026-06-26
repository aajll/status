/**
 * SPDX-License-Identifier: MIT
 *
 * @file: status_conf.h
 *
 * @brief
 *    Compile-time configuration and atomic-backend selection for the status
 *    library.
 *
 * @details
 *    Every tunable below is wrapped in an `#ifndef` guard, so a consumer can
 *    override any of them *without forking the library* by defining the macro
 *    first, either with a compiler flag (`-DNUM_STATUS_BANKS=8`) or by placing
 *    its own `config/status_conf.h` earlier on the include path. The shipped
 *    values here are the fallback defaults.
 *
 *    The atomic backend is auto-discovered (GCC/Clang `__atomic` -> C11
 *    `<stdatomic.h>` -> degenerate uniprocessor) and mirrors the ladder used by
 *    `seqlock_conf.h` and `embedded-queue`'s `queue_conf.h`. Set/clear of a
 *    single status bit is a genuine atomic read-modify-write on the two
 *    hardware-backed paths, so no caller-supplied critical section is required
 *    for interrupt- or core-concurrent bit operations.
 *
 *    Include order: `status_conf.h` is pulled in automatically by `status.h`,
 *    but it is safe (and recommended) to include it explicitly first.
 */
#ifndef STATUS_CONF_H_
#define STATUS_CONF_H_

#include <stdint.h>

/* ================ TUNABLES ================================================ */

/**
 * @def NUM_STATUS_BANKS
 * @brief The number of internal banks available for each status class.
 *
 * @details
 *    Each bank holds 16 bits. Users must ensure that any status ID encoded via
 *    `STATUS_ENCODE(bank, bit)` uses a `bank` value less than this. This value
 *    must match the maximum `bank + 1` used in the application's status_ids.h.
 */
#ifndef NUM_STATUS_BANKS
#define NUM_STATUS_BANKS (12u)
#endif

/* ================ ATOMIC BACKEND SELECTION =============================== */

/**
 * @def STATUS_USE_GNU_ATOMICS
 * @brief Force the GCC/Clang `__atomic` backend.
 *
 * This is the default backend on GCC and Clang. Define exactly one of
 * STATUS_USE_GNU_ATOMICS, STATUS_USE_C11_ATOMICS, or STATUS_USE_NO_ATOMICS
 * consistently for the status library build and all consumers to override the
 * automatic backend selection.
 */

/**
 * @def STATUS_USE_C11_ATOMICS
 * @brief Force the C11 `<stdatomic.h>` backend.
 *
 * Use when the toolchain provides C11 atomics and the project wants to avoid
 * compiler-specific `__atomic` builtins. Define exactly one backend-selection
 * macro consistently for the library build and all consumers.
 */

/**
 * @def STATUS_USE_NO_ATOMICS
 * @brief Force the degenerate uniprocessor backend.
 *
 * Correct only when the contexts that mutate a status class cannot preempt one
 * another on separate cores. This backend stores each bank as `volatile` and
 * recovers interrupt atomicity for the read-modify-write of set/clear through
 * the optional STATUS_ENTER_CRITICAL / STATUS_EXIT_CRITICAL hooks below (no-ops
 * by default). Define exactly one backend-selection macro consistently for the
 * library build and all consumers.
 */

/*
 * Pick exactly one backend unless the consumer forced one. The ladder mirrors
 * seqlock_conf.h:
 *   GCC/Clang __atomic  ->  C11 <stdatomic.h>  ->  degenerate no-op (uniproc).
 */
#if !defined(STATUS_USE_GNU_ATOMICS) && !defined(STATUS_USE_C11_ATOMICS)       \
    && !defined(STATUS_USE_NO_ATOMICS)
#if defined(__GNUC__) || defined(__clang__)
#define STATUS_USE_GNU_ATOMICS 1
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)               \
    && !defined(__STDC_NO_ATOMICS__)
#define STATUS_USE_C11_ATOMICS 1
#else
#define STATUS_USE_NO_ATOMICS 1
#endif
#endif

#if (defined(STATUS_USE_GNU_ATOMICS) + defined(STATUS_USE_C11_ATOMICS)         \
     + defined(STATUS_USE_NO_ATOMICS))                                         \
    != 1
#error "status: define exactly one atomic backend"
#endif

/*
 * Each status bit is a standalone flag that publishes no companion data, so the
 * relaxed memory order is sufficient and cheapest: set/clear/test impose no
 * ordering on surrounding application loads or stores. A consumer that needs a
 * status bit to release or acquire other state must add its own fence.
 */
#if defined(STATUS_USE_GNU_ATOMICS)

/** Qualifier applied to bank and tracker storage (none needed for __atomic). */
#define STATUS_ATOMIC_QUAL
#define STATUS_ATOMIC_LOAD(ptr) __atomic_load_n((ptr), __ATOMIC_RELAXED)
#define STATUS_ATOMIC_STORE(ptr, val)                                          \
        __atomic_store_n((ptr), (val), __ATOMIC_RELAXED)
#define STATUS_ATOMIC_OR(ptr, val)                                             \
        ((void)__atomic_fetch_or((ptr), (val), __ATOMIC_RELAXED))
#define STATUS_ATOMIC_AND(ptr, val)                                            \
        ((void)__atomic_fetch_and((ptr), (val), __ATOMIC_RELAXED))

#elif defined(STATUS_USE_C11_ATOMICS)

#include <stdatomic.h>
#define STATUS_ATOMIC_QUAL _Atomic
#define STATUS_ATOMIC_LOAD(ptr)                                                \
        atomic_load_explicit((ptr), memory_order_relaxed)
#define STATUS_ATOMIC_STORE(ptr, val)                                          \
        atomic_store_explicit((ptr), (val), memory_order_relaxed)
#define STATUS_ATOMIC_OR(ptr, val)                                             \
        ((void)atomic_fetch_or_explicit((ptr), (val), memory_order_relaxed))
#define STATUS_ATOMIC_AND(ptr, val)                                            \
        ((void)atomic_fetch_and_explicit((ptr), (val), memory_order_relaxed))

#else /* STATUS_USE_NO_ATOMICS */

/*
 * Degenerate uniprocessor fallback. A `volatile` aligned 16-bit load or store
 * is a single indivisible access on the targets this path serves, so LOAD and
 * STORE need no guard. The OR/AND read-modify-write is NOT indivisible, so it
 * is wrapped in the caller's critical section to stay interrupt-safe; with the
 * default no-op hooks it is correct only when set/clear cannot preempt one
 * another.
 */

/*
 * The hooks are a matched pair: defining one without the other is a
 * configuration error. Check before filling in the no-op defaults so the guard
 * can actually observe a half-configured consumer.
 */
#if defined(STATUS_ENTER_CRITICAL) != defined(STATUS_EXIT_CRITICAL)
#error "status: define both STATUS_ENTER_CRITICAL and STATUS_EXIT_CRITICAL, "   \
    "or neither"
#endif

/**
 * @brief Enter critical section (typically: save and disable interrupts).
 * @note Only consulted on the STATUS_USE_NO_ATOMICS backend. Define together
 *       with STATUS_EXIT_CRITICAL or neither.
 */
#ifndef STATUS_ENTER_CRITICAL
#define STATUS_ENTER_CRITICAL()
#endif

/**
 * @brief Exit critical section (typically: restore the saved interrupt state).
 * @note Only consulted on the STATUS_USE_NO_ATOMICS backend.
 */
#ifndef STATUS_EXIT_CRITICAL
#define STATUS_EXIT_CRITICAL()
#endif

#define STATUS_ATOMIC_QUAL            volatile
#define STATUS_ATOMIC_LOAD(ptr)       (*(ptr))
#define STATUS_ATOMIC_STORE(ptr, val) ((void)(*(ptr) = (val)))
#define STATUS_ATOMIC_OR(ptr, val)                                             \
        do {                                                                   \
                STATUS_ENTER_CRITICAL();                                       \
                *(ptr) = (uint16_t)(*(ptr) | (uint16_t)(val));                 \
                STATUS_EXIT_CRITICAL();                                        \
        } while (0)
#define STATUS_ATOMIC_AND(ptr, val)                                            \
        do {                                                                   \
                STATUS_ENTER_CRITICAL();                                       \
                *(ptr) = (uint16_t)(*(ptr) & (uint16_t)(val));                 \
                STATUS_EXIT_CRITICAL();                                        \
        } while (0)

#endif /* backend selection */

#endif /* STATUS_CONF_H_ */
