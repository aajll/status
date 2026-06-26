/**
 * SPDX-License-Identifier: MIT
 *
 * @file: status.h
 *
 * @brief
 *    Provides runtime logic for setting, clearing, and querying fault,
 *    warning, and info status bits defined by the application.
 *
 * @details
 *    A banked-bitfield status register: faults, warnings, and info bits are
 *    stored as arrays of 16-bit banks and addressed by compact encoded IDs.
 *
 *    ## MISRA C:2023 / IEC 61508 awareness
 *
 *    The implementation is written with MISRA C:2023 in mind and is intended
 *    to be used in IEC 61508 environments. The codebase is not formally
 *    certified. The one intentional, repository-wide advisory deviation is
 *    Rule 15.5 (single point of exit); guard clauses use early @c return at
 *    API boundaries.
 *
 *    ## Concurrency model and threading contract
 *
 *    Setting or clearing a single status bit is performed as an atomic
 *    read-modify-write on the bank word, so one context may set a bit while
 *    another clears a different bit in the same bank without either update
 *    being lost. This is interrupt- and core-safe by construction on the two
 *    hardware-backed atomic backends; @b no caller-supplied critical section
 *    is required for the per-bit operations.
 *
 *    Atomicity is per call, not per logical group. A multi-bit observation
 *    such as @c status_any or @c status_snapshot reads each bank atomically
 *    but is not a single consistent snapshot of the whole class, and a
 *    set-then-read sequence across two API calls is not one transaction. A
 *    caller that needs grouped atomicity must serialise the group itself.
 *
 *    Memory ordering (known limitation): bit operations use the @e relaxed
 *    order. A set or cleared bit therefore establishes no happens-before
 *    relationship with any other memory, so a consumer that observes a bit is
 *    not guaranteed to observe data the producer wrote before setting it.
 *    This is intentional for a standalone status register; if a bit must
 *    publish or acquire companion state, the caller must add its own fence.
 *
 *    The atomic mechanism is selected at compile time in @c status_conf.h:
 *    GCC/Clang @c __atomic, C11 @c <stdatomic.h>, or a degenerate
 *    uniprocessor fallback that recovers interrupt atomicity through the
 *    optional STATUS_ENTER_CRITICAL / STATUS_EXIT_CRITICAL hooks. The two
 *    atomic backends statically assert that the bank and tracker storage are
 *    always lock-free on the target, so a target that cannot satisfy that
 *    contract fails to compile rather than degrading silently.
 */

#ifndef STATUS_H
#define STATUS_H

/* Add C bindings if being compiled with C++ compiler */
#ifdef __cplusplus
extern "C" {
#endif

/* ================ INCLUDES ================================================ */

#include "status_conf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ================ DEFINES ================================================= */

/* ---------------  Configuration ------------------------------------------- */

/*
 * NUM_STATUS_BANKS and the atomic backend live in status_conf.h so a consumer
 * can override them without forking this header.
 */

/**
 * @def NUM_STATUS_BITS
 * @brief Number of bit positions within each bank. Fixed at 16 to match the
 *        uint16_t bank storage type.
 */
#define NUM_STATUS_BITS (16u)

/**
 * @def STATUS_UNSET_ID
 * @brief Sentinel returned by status_last_fault(), status_last_warning(), and
 *        status_last_info() when no status of that class has been set since the
 *        last status_init() call.
 *
 * @note This value encodes bank 4095 / bit 15, which exceeds NUM_STATUS_BANKS
 *       and is therefore not a valid application-defined status ID.
 */
#define STATUS_UNSET_ID (0xFFFFu)

/* ================ STRUCTURES ============================================== */

/**
 * @brief Status class for categorization.
 */
enum status_class {
        STATUS_CLASS_FAULT = 0,
        STATUS_CLASS_WARNING = 1,
        STATUS_CLASS_INFO = 2,
};

/* ================ TYPEDEFS ================================================ */

/**
 * @brief Error types for status_err_callback.
 */
typedef enum {
        STATUS_ERR_INVALID_ID = 0, /**< Unrecognised status_class value */
        STATUS_ERR_INVALID_BANK,   /**< Bank index >= NUM_STATUS_BANKS */
        STATUS_ERR_INVALID_LEN,    /**< Zero-length argument to snapshot */
        STATUS_ERR_NULL_PTR        /**< NULL pointer argument */
} status_err_t;

/**
 * @brief Callback function type for error handling.
 */
typedef void (*status_err_cb_t)(status_err_t err, uint16_t id);

/* ================ COMPILE-TIME GUARANTEES ================================= */

/*
 * The atomic backends rely on the bank word (uint16_t) and the error-callback
 * pointer being *always* lock-free on the target; otherwise a hidden global
 * lock (or a libatomic call that may not exist on bare metal) would be pulled
 * in behind the set/clear path. Degrade loudly, not silently: a target that
 * cannot satisfy this must select STATUS_USE_NO_ATOMICS and supply the
 * critical-section hooks instead.
 */
#if defined(STATUS_USE_GNU_ATOMICS)
/* __atomic_always_lock_free is a compile-time constant but not a "standard"
 * integer constant expression, so -Wpedantic objects to it inside a
 * _Static_assert; suppress just that diagnostic here. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
_Static_assert(__atomic_always_lock_free(sizeof(uint16_t), 0),
               "status: uint16_t bank storage is not lock-free on this target");
_Static_assert(
    __atomic_always_lock_free(sizeof(status_err_cb_t), 0),
    "status: error-callback pointer is not lock-free on this target");
#pragma GCC diagnostic pop
#elif defined(STATUS_USE_C11_ATOMICS)
/* The C11 path has no size-based lock-free query (atomic_is_lock_free is a
 * runtime call), so match the bank width to the matching ATOMIC_*_LOCK_FREE
 * macro and require "always lock-free" (== 2). */
_Static_assert(
    (sizeof(uint16_t) == sizeof(short) && ATOMIC_SHORT_LOCK_FREE == 2)
        || (sizeof(uint16_t) == sizeof(int) && ATOMIC_INT_LOCK_FREE == 2),
    "status: uint16_t bank storage is not always-lock-free on this target");
_Static_assert(
    ATOMIC_POINTER_LOCK_FREE == 2,
    "status: error-callback pointer is not always-lock-free on this target");
#endif

/* ================ MACROS ================================================== */

/**
 * @def STATUS_ENCODE
 * @brief Encodes a status bank and bit index into a single 16-bit status ID.
 *
 * @param bank      Logical bank index (0-based). Typically used to group
 *                  faults/warnings.
 *
 * @param bit       Bit position within the bank (0–15). Each bank can store up
 *                  to 16 bits.
 *
 * @details
 *    Each bank is assumed to store 16 bits. This macro encodes a `bank` and
 *    `bit` index into a compact 16-bit ID used for indexing status registers.
 *    The result can be passed to `status_set_fault()`,
 * `status_clear_warning()`, etc.
 *
 * @return          A compact 16-bit status ID, suitable for use in the
 *                  `status` module.
 *
 * @note
 *    The maximum bit index is 15. Higher values are masked off automatically.
 *    The bank value is NOT masked; passing a value >= 4096 will silently
 *    truncate on the cast to uint16_t. Callers must ensure bank <
 * NUM_STATUS_BANKS.
 */
#define STATUS_ENCODE(bank, bit)                                               \
        ((uint16_t)(((uint32_t)(bank) << 4u) | ((uint32_t)(bit) & 0x0Fu)))

/* ================ GLOBAL VARIABLES ======================================== */

/* ================ GLOBAL PROTOTYPES ======================================= */

/**
 * @brief Extracts the bank number from an encoded status ID.
 *
 * @param id        A status ID encoded using `STATUS_ENCODE()`.
 *
 * @return          The bank index (0-based).
 */
static inline uint16_t
status_bank(uint16_t id)
{
        return (uint16_t)(id >> 4u);
}

/**
 * @brief Extracts the bit index from an encoded status ID.
 *
 * @param id        A status ID encoded using `STATUS_ENCODE()`.
 *
 * @return          The bit index (0–15) within the bank.
 */
static inline uint16_t
status_bit(uint16_t id)
{
        return (uint16_t)(id & 0x0Fu);
}

/*
 * Error model (applies to every public function below): no function ever
 * dereferences a bad pointer or accesses storage out of range. Invalid input
 * (an unrecognised class, an out-of-range bank, or a NULL / zero-length
 * buffer) is reported through the registered error callback and is otherwise a
 * no-op: setters and clearers change nothing, queries return false, and the
 * trackers are untouched. With no callback registered, invalid input is
 * silently ignored.
 */

/**
 * @brief Initialise the status module: clear all banks and reset all trackers.
 *
 * @note Resets every register bank and the last-set ID tracker of all three
 *       classes to STATUS_UNSET_ID. The registered error callback is preserved
 *       so that errors during re-initialisation are still reported.
 *
 * @warning Not a transaction: status_init() issues many individual atomic
 *          stores. Call it before any concurrent producer is active (start-up
 *          or a coordinated quiesce), not while another context may be setting
 *          bits.
 */
void status_init(void);

/**
 * @brief Register, replace, or clear the error callback.
 *
 * @param cb  Function pointer to the error handler, or NULL to deregister.
 *            The pointer is stored atomically, so it may be changed while other
 *            contexts are running.
 *
 * @note Execution context: the callback is invoked synchronously from whichever
 *       context hit the error, which may be an ISR (e.g. status_set_fault()
 *       called from an interrupt handler). It must be short, non-blocking, and
 *       must not assume task context. It runs outside any internal lock and may
 *       safely re-enter the status API.
 */
void status_set_err_callback(status_err_cb_t cb);

/**
 * @brief Set the given warning status bit and update the warning tracker.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @note Atomic, interrupt- and core-safe (see the file-level concurrency
 *       contract). An out-of-range bank invokes the error callback with
 *       STATUS_ERR_INVALID_BANK and changes nothing.
 */
void status_set_warning(uint16_t id);

/**
 * @brief Set the given fault status bit and update the fault tracker.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @note Atomic, interrupt- and core-safe. An out-of-range bank invokes the
 *       error callback with STATUS_ERR_INVALID_BANK and changes nothing.
 */
void status_set_fault(uint16_t id);

/**
 * @brief Set the given info status bit and update the info tracker.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @note Atomic, interrupt- and core-safe. An out-of-range bank invokes the
 *       error callback with STATUS_ERR_INVALID_BANK and changes nothing.
 */
void status_set_info(uint16_t id);

/**
 * @brief Clear the given warning status bit.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @note Atomic, interrupt- and core-safe. The status_last_warning() tracker is
 *       NOT modified; call status_init() to reset it. An out-of-range bank
 *       invokes the error callback with STATUS_ERR_INVALID_BANK.
 */
void status_clear_warning(uint16_t id);

/**
 * @brief Clear the given fault status bit.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @note Atomic, interrupt- and core-safe. The status_last_fault() tracker is
 *       NOT modified; call status_init() to reset it. An out-of-range bank
 *       invokes the error callback with STATUS_ERR_INVALID_BANK.
 */
void status_clear_fault(uint16_t id);

/**
 * @brief Clear the given info status bit.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @note Atomic, interrupt- and core-safe. The status_last_info() tracker is
 *       NOT modified; call status_init() to reset it. An out-of-range bank
 *       invokes the error callback with STATUS_ERR_INVALID_BANK.
 */
void status_clear_info(uint16_t id);

/**
 * @brief Check whether a given warning status bit is set.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @return true if the bit is set; false if it is clear or @p id is invalid.
 *
 * @note An out-of-range bank invokes the error callback with
 *       STATUS_ERR_INVALID_BANK and returns false.
 */
bool status_is_warning_set(uint16_t id);

/**
 * @brief Check whether a given fault status bit is set.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @return true if the bit is set; false if it is clear or @p id is invalid.
 *
 * @note An out-of-range bank invokes the error callback with
 *       STATUS_ERR_INVALID_BANK and returns false.
 */
bool status_is_fault_set(uint16_t id);

/**
 * @brief Check whether a given info status bit is set.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @return true if the bit is set; false if it is clear or @p id is invalid.
 *
 * @note An out-of-range bank invokes the error callback with
 *       STATUS_ERR_INVALID_BANK and returns false.
 */
bool status_is_info_set(uint16_t id);

/**
 * @brief Check whether any bit in the given class is set.
 *
 * @param cls  Status class to scan.
 *
 * @return true if at least one bit in @p cls is set; false if none are or
 *         @p cls is unrecognised.
 *
 * @note Reads each bank atomically (O(NUM_STATUS_BANKS) with early exit) but is
 *       not a single consistent snapshot of the class: a bit set in an
 *       already-scanned bank after the scan passed it is not observed.
 * @note An unrecognised @p cls invokes the error callback with
 *       STATUS_ERR_INVALID_ID and returns false.
 */
bool status_any(enum status_class cls);

/**
 * @brief Clear all bits in the given class.
 *
 * @param cls  Status class to clear.
 *
 * @note Each bank is cleared by an individual atomic store; this is not a
 *       single transaction against concurrent producers.
 * @note The last-set ID tracker for the class (e.g. status_last_fault()) is
 *       preserved as an audit trail; call status_init() to reset all trackers.
 * @note An unrecognised @p cls invokes the error callback with
 *       STATUS_ERR_INVALID_ID and changes nothing.
 */
void status_clear_all(enum status_class cls);

/**
 * @brief Get the most recently set fault ID.
 *
 * @return The last ID passed to status_set_fault(), or STATUS_UNSET_ID if no
 *         fault has been set since the last status_init(). Reflects only the
 *         most recently set fault, not all active faults; use
 *         status_any(STATUS_CLASS_FAULT) to test for any active fault.
 */
uint16_t status_last_fault(void);

/**
 * @brief Get the most recently set warning ID.
 *
 * @return The last ID passed to status_set_warning(), or STATUS_UNSET_ID if no
 *         warning has been set since the last status_init().
 */
uint16_t status_last_warning(void);

/**
 * @brief Get the most recently set info ID.
 *
 * @return The last ID passed to status_set_info(), or STATUS_UNSET_ID if no
 *         info bit has been set since the last status_init().
 */
uint16_t status_last_info(void);

/**
 * @brief Copy the banks of a class into a caller-supplied buffer.
 *
 * @param cls  Status class to snapshot.
 * @param dst  Destination array with space for at least @p len entries.
 * @param len  Number of banks to copy; capped at NUM_STATUS_BANKS. A value
 *             below NUM_STATUS_BANKS produces a partial snapshot.
 *
 * @note Each bank is read atomically, but the snapshot as a whole is not a
 *       single instant of the class.
 * @note On error the callback is invoked and no copy is performed:
 *       - invalid @p cls  → STATUS_ERR_INVALID_ID
 *       - NULL @p dst     → STATUS_ERR_NULL_PTR
 *       - @p len == 0     → STATUS_ERR_INVALID_LEN
 */
void status_snapshot(enum status_class cls, uint16_t *dst, size_t len);

/* End of C bindings for C++ compilers */
#ifdef __cplusplus
}
#endif

#endif /* STATUS_H */
