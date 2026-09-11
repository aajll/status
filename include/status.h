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
 *    certified. Every deviation from the guideline is reviewed and carries a
 *    written rationale: project-level entries and inline suppressions are
 *    recorded in the repository, so the accepted deviations are auditable.
 *
 *    @anchor status_concurrency
 *    ## Concurrency model and threading contract
 *
 *    Setting or clearing a single status bit is performed as an atomic
 *    read-modify-write on the bank word, so one context may set a bit while
 *    another clears a different bit in the same bank without either update
 *    being lost. This is interrupt- and core-safe by construction on the two
 *    hardware-backed atomic backends; @b no caller-supplied critical section
 *    is required for the per-bit operations.
 *
 *    Atomicity applies to each bank operation, not necessarily a whole call.
 *    A setter performs an atomic bank read-modify-write followed in program
 *    order by a separate atomic tracker store. Other contexts need not observe
 *    these relaxed accesses together or in that order.
 *
 *    @anchor status_trackers
 *    Last-ID trackers record tracker-store order, not call-completion order.
 *    A reader during overlapping setters may see an earlier ID or
 *    STATUS_UNSET_ID, even after the bank bits have changed.
 *    For two valid setters of the same class, synchronise with both completions
 *    before reading the tracker. Either ID may remain if no other setter or
 *    initialisation intervenes. Without intervening clears, both bits remain
 *    set. A tracker is not a snapshot of the active bits.
 *
 *    A multi-bit observation such as @c status_any or @c status_snapshot reads
 *    each bank atomically but is not a single consistent snapshot of the whole
 *    class, and a set-then-read sequence across two API calls is not one
 *    transaction. A caller that needs grouped atomicity must serialise the
 *    group itself, and a caller that needs a tracker value consistent with a
 *    bank state must serialise the set against the read.
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
 *    STATUS_ENTER_CRITICAL / STATUS_EXIT_CRITICAL hooks. On this fallback,
 *    concurrent access requires caller-supplied protection for every accessing
 *    context. With default no-op hooks, accesses must neither overlap nor
 *    preempt each other.
 *    Interrupt masking protects one core only, not concurrent accesses from
 *    other cores. All atomicity claims below require this backend contract.
 *    The two atomic backends statically assert that bank storage and the
 *    error-callback pointer are always lock-free on the target. Unsupported
 *    targets fail to compile rather than degrade silently.
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

/**
 * @brief Caller-owned status register storage.
 *
 * @details Allocate this type statically or automatically, then initialise it
 * with status_reg_init(). Its members are implementation-owned: do not inspect,
 * modify, or copy a register while it is active.
 *
 * - ::status_reg_t::fault_banks  Fault bits, 16 per bank.
 * - ::status_reg_t::warning_banks  Warning bits, 16 per bank.
 * - ::status_reg_t::info_banks  Info bits, 16 per bank.
 * - ::status_reg_t::last_fault_id  Last-set fault tracker.
 * - ::status_reg_t::last_warning_id  Last-set warning tracker.
 * - ::status_reg_t::last_info_id  Last-set info tracker.
 * - ::status_reg_t::err_cb  Instance callback, NULL when deregistered.
 */
typedef struct {
        STATUS_ATOMIC_QUAL uint16_t fault_banks[NUM_STATUS_BANKS];
        STATUS_ATOMIC_QUAL uint16_t warning_banks[NUM_STATUS_BANKS];
        STATUS_ATOMIC_QUAL uint16_t info_banks[NUM_STATUS_BANKS];
        STATUS_ATOMIC_QUAL uint16_t last_fault_id;
        STATUS_ATOMIC_QUAL uint16_t last_warning_id;
        STATUS_ATOMIC_QUAL uint16_t last_info_id;
        STATUS_ATOMIC_QUAL status_err_cb_t err_cb;
} status_reg_t;

/* ================ COMPILE-TIME GUARANTEES ================================= */

/*
 * The atomic backends rely on the bank word (uint16_t) and the error-callback
 * pointer being *always* lock-free on the target; otherwise a hidden global
 * lock (or a libatomic call that may not exist on bare metal) would be pulled
 * in behind the set/clear path. Degrade loudly, not silently: a target that
 * cannot satisfy this must select STATUS_USE_NO_ATOMICS and supply the
 * critical-section hooks instead.
 */

/**
 * @def STATUS_STATIC_ASSERT
 * @brief Compile-time assertion that compiles as both C11 and C++17.
 *
 * @param condition  Constant expression that must hold.
 * @param message    String literal reported when it does not.
 *
 * @details C++ spells the keyword `static_assert`; C11 spells it
 * `_Static_assert`. Routing both through one macro keeps the public header
 * compilable as both C11 and C++17. The project targets C11, so the non-C++
 * branch can rely on `_Static_assert` being available.
 */
#if defined(__cplusplus)
#define STATUS_STATIC_ASSERT(condition, message)                               \
        static_assert(condition, message)
#else
#define STATUS_STATIC_ASSERT(condition, message)                               \
        _Static_assert(condition, message)
#endif

#if defined(STATUS_USE_GNU_ATOMICS)
/* __atomic_always_lock_free is a compile-time constant but not a "standard"
 * integer constant expression, so -Wpedantic objects to it inside a static
 * assertion; suppress just that diagnostic here. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
STATUS_STATIC_ASSERT(
    __atomic_always_lock_free(sizeof(uint16_t), 0),
    "status: uint16_t bank storage is not lock-free on this target");
STATUS_STATIC_ASSERT(
    __atomic_always_lock_free(sizeof(status_err_cb_t), 0),
    "status: error-callback pointer is not lock-free on this target");
#pragma GCC diagnostic pop
#elif defined(STATUS_USE_C11_ATOMICS)
/* The C11 path has no size-based lock-free query (atomic_is_lock_free is a
 * runtime call), so match the bank width to the matching ATOMIC_*_LOCK_FREE
 * macro and require "always lock-free" (== 2). */
STATUS_STATIC_ASSERT(
    ((sizeof(uint16_t) == sizeof(short)) && (ATOMIC_SHORT_LOCK_FREE == 2))
        || ((sizeof(uint16_t) == sizeof(int)) && (ATOMIC_INT_LOCK_FREE == 2)),
    "status: uint16_t bank storage is not always-lock-free on this target");
STATUS_STATIC_ASSERT(
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

/**
 * @brief Return the next active ID from a caller-owned snapshot.
 *
 * @param snapshot  Bank array produced by status_snapshot() or
 *                  status_reg_snapshot().
 * @param len       Number of banks in @p snapshot; capped at NUM_STATUS_BANKS,
 *                  so a smaller value scans only that many banks.
 * @param cursor    Bit offset; initialise to zero and preserve between calls.
 * @param id        Destination for the next active ID.
 * @return true when an ID was returned; false for a NULL argument or when the
 *         scan reaches the end of the buffer.
 *
 * @note The snapshot must not be modified during the traversal. This function
 *       scans only the supplied buffer, so enumeration is a deterministic pass
 *       over a caller-owned copy: it never observes concurrent updates to a
 *       live register, and it never picks up a bit set after the snapshot was
 *       taken. Call status_snapshot() or status_reg_snapshot() again to
 *       refresh the copy.
 */
static inline bool
status_snapshot_next(const uint16_t *snapshot, size_t len, size_t *cursor,
                     uint16_t *id)
{
        const size_t bit_count =
            ((len < NUM_STATUS_BANKS) ? len : NUM_STATUS_BANKS)
            * NUM_STATUS_BITS;

        if ((snapshot == NULL) || (cursor == NULL) || (id == NULL)) {
                return false;
        }
        while (*cursor < bit_count) {
                const size_t bit = *cursor;
                const uint16_t bank = (uint16_t)(bit / NUM_STATUS_BITS);
                const uint16_t offset = (uint16_t)(bit % NUM_STATUS_BITS);

                ++(*cursor);
                if ((snapshot[bank]
                     & (uint16_t)((uint32_t)1u << (uint32_t)offset))
                    != 0u) {
                        *id = STATUS_ENCODE(bank, offset);
                        return true;
                }
        }
        return false;
}

/*
 * Caller-owned register interface.
 *
 * Allocate a status_reg_t statically or automatically and pass its address to
 * status_reg_init() before first use. Each register has its own banks,
 * trackers, and callback. Do not copy or move it while in use. Multiple
 * subsystems may use the same register under the concurrency contract.
 *
 * Contract shared by every function below:
 *
 * - A NULL register is silent, because there is no instance callback to report
 *   through. Mutators, init, clear-all, and snapshots do nothing, predicates
 *   and test-and-clear return false, and the last-ID getters return
 *   STATUS_UNSET_ID.
 * - Per-bit set, clear, and test-and-clear touch one bank word with a single
 *   atomic read-modify-write and inherit the file-level concurrency contract.
 *   Init, clear-all, scans, and snapshots are bank-by-bank and are not a
 *   transaction against concurrent producers.
 * - Set functions update the last-set tracker and init resets it. Clearing a
 *   bit never rewrites it, and clear-all and snapshot leave it alone.
 * - A callback is per register, runs synchronously in the calling context
 *   (which may be an ISR), outside any internal protection, and may re-enter
 *   this API.
 */

/**
 * @brief Initialise a caller-owned register.
 *
 * @param reg  Register to initialise, or NULL for a no-op.
 *
 * @note Clears every bank, resets all three trackers to STATUS_UNSET_ID, and
 *       deregisters the error callback. The singleton status_init() instead
 *       preserves its callback so that errors during re-initialisation are
 *       still reported; re-register with status_reg_set_err_callback() after
 *       initialising a caller-owned register.
 *
 * @warning Requires exclusive access to the register. No other context may
 *          read or write it during initialisation, including re-initialisation.
 *          The GNU backend uses ordinary assignments, and C11 atomic_init()
 *          does not provide concurrent atomic access. Initialise before use or
 *          after all users have quiesced, then synchronise before use resumes.
 */
void status_reg_init(status_reg_t *reg);

/**
 * @brief Register, replace, or clear one register's error callback.
 *
 * @param reg  Register to modify, or NULL for a no-op.
 * @param cb   Handler to store, or NULL to deregister.
 *
 * @note The pointer is stored atomically, so it may be changed while other
 *       contexts are using the register. Callbacks are not shared: this
 *       affects @p reg only, not the singleton or any other register.
 * @note The callback runs synchronously in the context that encounters the
 *       error, which may be an ISR. It must be short, non-blocking, and safe
 *       for every calling context. It runs outside internal protection and
 *       may re-enter the status API. A NULL @p reg returns without a callback.
 * @note Pointer access follows the backend requirements in
 *       @ref status_concurrency.
 */
void status_reg_set_err_callback(status_reg_t *reg, status_err_cb_t cb);

/**
 * @brief Set the given warning bit and update the warning tracker.
 *
 * @param reg  Register to modify, or NULL for a no-op.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @note The bank update is one atomic read-modify-write; the tracker then
 *       receives a separate atomic store, so the call as a whole is not one
 *       transaction (see the file-level concurrency contract). Setting an
 *       already-set bit still updates the tracker.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback and changes nothing, including the tracker.
 */
void status_reg_set_warning(status_reg_t *reg, uint16_t id);

/**
 * @brief Set the given fault bit and update the fault tracker.
 *
 * @param reg  Register to modify, or NULL for a no-op.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @note The bank update is one atomic read-modify-write; the tracker then
 *       receives a separate atomic store, so the call as a whole is not one
 *       transaction (see the file-level concurrency contract). Setting an
 *       already-set bit still updates the tracker.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback and changes nothing, including the tracker.
 */
void status_reg_set_fault(status_reg_t *reg, uint16_t id);

/**
 * @brief Set the given info bit and update the info tracker.
 *
 * @param reg  Register to modify, or NULL for a no-op.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @note The bank update is one atomic read-modify-write; the tracker then
 *       receives a separate atomic store, so the call as a whole is not one
 *       transaction (see the file-level concurrency contract). Setting an
 *       already-set bit still updates the tracker.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback and changes nothing, including the tracker.
 */
void status_reg_set_info(status_reg_t *reg, uint16_t id);

/**
 * @brief Clear the given warning bit.
 *
 * @param reg  Register to modify, or NULL for a no-op.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @note One atomic bank read-modify-write under @ref status_concurrency.
 *       The warning tracker is not modified. Only status_reg_init() resets it.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback and changes nothing.
 */
void status_reg_clear_warning(status_reg_t *reg, uint16_t id);

/**
 * @brief Clear the given fault bit.
 *
 * @param reg  Register to modify, or NULL for a no-op.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @note One atomic bank read-modify-write under @ref status_concurrency.
 *       The fault tracker is not modified. Only status_reg_init() resets it.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback and changes nothing.
 */
void status_reg_clear_fault(status_reg_t *reg, uint16_t id);

/**
 * @brief Clear the given info bit.
 *
 * @param reg  Register to modify, or NULL for a no-op.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @note One atomic bank read-modify-write under @ref status_concurrency.
 *       The info tracker is not modified. Only status_reg_init() resets it.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback and changes nothing.
 */
void status_reg_clear_info(status_reg_t *reg, uint16_t id);

/**
 * @brief Atomically test and clear one warning bit.
 *
 * @param reg  Register to modify, or NULL for a no-op.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @return true if the bit was set before this call cleared it; false if it was
 *         already clear, @p id is invalid, or @p reg is NULL.
 *
 * @note One atomic linearization point on the bank word. Repeated sets before
 *       consumption coalesce into a single true result; this is not an event
 *       counter. The warning tracker is not modified.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback.
 */
bool status_reg_test_and_clear_warning(status_reg_t *reg, uint16_t id);

/**
 * @brief Atomically test and clear one fault bit.
 *
 * @param reg  Register to modify, or NULL for a no-op.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @return true if the bit was set before this call cleared it; false if it was
 *         already clear, @p id is invalid, or @p reg is NULL.
 *
 * @note One atomic linearization point on the bank word. Repeated sets before
 *       consumption coalesce into a single true result; this is not an event
 *       counter. The fault tracker is not modified.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback.
 */
bool status_reg_test_and_clear_fault(status_reg_t *reg, uint16_t id);

/**
 * @brief Atomically test and clear one info bit.
 *
 * @param reg  Register to modify, or NULL for a no-op.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @return true if the bit was set before this call cleared it; false if it was
 *         already clear, @p id is invalid, or @p reg is NULL.
 *
 * @note One atomic linearization point on the bank word. Repeated sets before
 *       consumption coalesce into a single true result; this is not an event
 *       counter. The info tracker is not modified.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback.
 */
bool status_reg_test_and_clear_info(status_reg_t *reg, uint16_t id);

/**
 * @brief Test whether one warning bit is set.
 *
 * @param reg  Register to read, or NULL.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @return true if the bit is set; false if it is clear, @p id is invalid, or
 *         @p reg is NULL.
 *
 * @note A single atomic bank read.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback.
 */
bool status_reg_is_warning_set(const status_reg_t *reg, uint16_t id);

/**
 * @brief Test whether one fault bit is set.
 *
 * @param reg  Register to read, or NULL.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @return true if the bit is set; false if it is clear, @p id is invalid, or
 *         @p reg is NULL.
 *
 * @note A single atomic bank read.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback.
 */
bool status_reg_is_fault_set(const status_reg_t *reg, uint16_t id);

/**
 * @brief Test whether one info bit is set.
 *
 * @param reg  Register to read, or NULL.
 * @param id   Status ID from STATUS_ENCODE(); its bank must be
 *             < NUM_STATUS_BANKS.
 *
 * @return true if the bit is set; false if it is clear, @p id is invalid, or
 *         @p reg is NULL.
 *
 * @note A single atomic bank read.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK through @p reg's
 *       callback.
 */
bool status_reg_is_info_set(const status_reg_t *reg, uint16_t id);

/**
 * @brief Test whether any bit of a class is set in this register.
 *
 * @param reg  Register to scan, or NULL.
 * @param cls  Status class to scan.
 *
 * @return true if at least one bit of @p cls is set; false if none are, @p reg
 *         is NULL, or @p cls is unrecognised.
 *
 * @note Reads each bank atomically but is not one consistent instant of the
 *       class, so a bit set in an already-scanned bank is not observed.
 * @note An unrecognised @p cls reports STATUS_ERR_INVALID_ID through @p reg's
 *       callback; the reported id is STATUS_UNSET_ID.
 */
bool status_reg_any(const status_reg_t *reg, enum status_class cls);

/**
 * @brief Clear every bit of one class in this register.
 *
 * @param reg  Register to modify, or NULL for a no-op.
 * @param cls  Status class to clear.
 *
 * @note Each bank is cleared by an individual atomic store; this is not a
 *       single transaction against concurrent producers.
 * @note The class tracker is preserved as an audit trail; only
 *       status_reg_init() resets it.
 * @note An unrecognised @p cls reports STATUS_ERR_INVALID_ID through @p reg's
 *       callback and changes nothing; the reported id is STATUS_UNSET_ID.
 */
void status_reg_clear_all(status_reg_t *reg, enum status_class cls);

/**
 * @brief Read this register's last-set fault tracker.
 *
 * @param reg  Register to read, or NULL.
 *
 * @return A valid ID recorded by status_reg_set_fault(), or STATUS_UNSET_ID
 *         if the tracker is still at its initial value. NULL @p reg also
 *         returns STATUS_UNSET_ID without a callback.
 *
 * @note An atomic tracker read does not describe the current bank state.
 *       Clear operations preserve the tracker. During overlapping setters,
 *       this may return an earlier ID or STATUS_UNSET_ID. See
 *       @ref status_trackers for completion and synchronisation requirements.
 */
uint16_t status_reg_last_fault(const status_reg_t *reg);

/**
 * @brief Read this register's last-set warning tracker.
 *
 * @param reg  Register to read, or NULL.
 *
 * @return A valid ID recorded by status_reg_set_warning(), or STATUS_UNSET_ID
 *         if the tracker is still at its initial value. NULL @p reg also
 *         returns STATUS_UNSET_ID without a callback.
 *
 * @note An atomic tracker read does not describe the current bank state.
 *       Clear operations preserve the tracker. During overlapping setters,
 *       this may return an earlier ID or STATUS_UNSET_ID. See
 *       @ref status_trackers for completion and synchronisation requirements.
 */
uint16_t status_reg_last_warning(const status_reg_t *reg);

/**
 * @brief Read this register's last-set info tracker.
 *
 * @param reg  Register to read, or NULL.
 *
 * @return A valid ID recorded by status_reg_set_info(), or STATUS_UNSET_ID
 *         if the tracker is still at its initial value. NULL @p reg also
 *         returns STATUS_UNSET_ID without a callback.
 *
 * @note An atomic tracker read does not describe the current bank state.
 *       Clear operations preserve the tracker. During overlapping setters,
 *       this may return an earlier ID or STATUS_UNSET_ID. See
 *       @ref status_trackers for completion and synchronisation requirements.
 */
uint16_t status_reg_last_info(const status_reg_t *reg);

/**
 * @brief Copy the banks of one class into a caller-supplied buffer.
 *
 * @param reg  Register to read, or NULL for a no-op.
 * @param cls  Status class to snapshot.
 * @param dst  Destination array with space for at least @p len entries.
 * @param len  Number of banks to copy; capped at NUM_STATUS_BANKS. A value
 *             below NUM_STATUS_BANKS produces a partial snapshot.
 *
 * @note Each bank is read atomically, but the snapshot as a whole is not a
 *       single instant of the class.
 * @note A NULL @p reg returns silently without a copy or callback. Otherwise,
 *       the checks below run in order. The first failure stops the copy and
 *       invokes the instance callback, if registered, with STATUS_UNSET_ID:
 *       - unrecognised @p cls  → STATUS_ERR_INVALID_ID
 *       - NULL @p dst      → STATUS_ERR_NULL_PTR
 *       - @p len == 0      → STATUS_ERR_INVALID_LEN
 *
 * @note The result can be handed to status_snapshot_next() for deterministic
 *       enumeration, provided the buffer is not modified during the traversal.
 */
void status_reg_snapshot(const status_reg_t *reg, enum status_class cls,
                         uint16_t *dst, size_t len);

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
 * @note One atomic bank read-modify-write under @ref status_concurrency,
 *       followed by a separate atomic tracker store. The call is not one
 *       transaction. Setting an already-set bit still updates the tracker.
 * @note An out-of-range bank invokes the error callback with
 *       STATUS_ERR_INVALID_BANK and changes nothing, including the tracker.
 */
void status_set_warning(uint16_t id);

/**
 * @brief Set the given fault status bit and update the fault tracker.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @note One atomic bank read-modify-write under @ref status_concurrency,
 *       followed by a separate atomic tracker store. The call is not one
 *       transaction. Setting an already-set bit still updates the tracker.
 * @note An out-of-range bank invokes the error callback with
 *       STATUS_ERR_INVALID_BANK and changes nothing, including the tracker.
 */
void status_set_fault(uint16_t id);

/**
 * @brief Set the given info status bit and update the info tracker.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @note One atomic bank read-modify-write under @ref status_concurrency,
 *       followed by a separate atomic tracker store. The call is not one
 *       transaction. Setting an already-set bit still updates the tracker.
 * @note An out-of-range bank invokes the error callback with
 *       STATUS_ERR_INVALID_BANK and changes nothing, including the tracker.
 */
void status_set_info(uint16_t id);

/**
 * @brief Clear the given warning status bit.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @note One atomic bank read-modify-write under @ref status_concurrency.
 *       The status_last_warning() tracker is not modified. Call status_init()
 *       to reset it. An out-of-range bank invokes the error callback with
 *       STATUS_ERR_INVALID_BANK.
 */
void status_clear_warning(uint16_t id);

/**
 * @brief Clear the given fault status bit.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @note One atomic bank read-modify-write under @ref status_concurrency.
 *       The status_last_fault() tracker is not modified. Call status_init()
 *       to reset it. An out-of-range bank invokes the error callback with
 *       STATUS_ERR_INVALID_BANK.
 */
void status_clear_fault(uint16_t id);

/**
 * @brief Clear the given info status bit.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @note One atomic bank read-modify-write under @ref status_concurrency.
 *       The status_last_info() tracker is not modified. Call status_init()
 *       to reset it. An out-of-range bank invokes the error callback with
 *       STATUS_ERR_INVALID_BANK.
 */
void status_clear_info(uint16_t id);

/**
 * @brief Atomically test and clear one warning status bit.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @return true if the bit was set before this call cleared it; false if it was
 *         already clear or @p id is invalid.
 *
 * @note Each operation has one atomic linearization point. Repeated sets before
 *       consumption coalesce into one true result; this is not an event
 *       counter. The warning tracker is not modified.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK and @p id through
 *       the registered callback, if any. It returns false without changing the
 *       bank or tracker. Atomicity requires @ref status_concurrency.
 */
bool status_test_and_clear_warning(uint16_t id);

/**
 * @brief Atomically test and clear one fault status bit.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @return true if the bit was set before this call cleared it; false if it was
 *         already clear or @p id is invalid.
 *
 * @note Each operation has one atomic linearization point. Repeated sets before
 *       consumption coalesce into one true result; this is not an event
 *       counter. The fault tracker is not modified.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK and @p id through
 *       the registered callback, if any. It returns false without changing the
 *       bank or tracker. Atomicity requires @ref status_concurrency.
 */
bool status_test_and_clear_fault(uint16_t id);

/**
 * @brief Atomically test and clear one info status bit.
 *
 * @param id  Status ID from STATUS_ENCODE(); its bank must be
 *            < NUM_STATUS_BANKS.
 *
 * @return true if the bit was set before this call cleared it; false if it was
 *         already clear or @p id is invalid.
 *
 * @note Each operation has one atomic linearization point. Repeated sets before
 *       consumption coalesce into one true result; this is not an event
 *       counter. The info tracker is not modified.
 * @note An out-of-range bank reports STATUS_ERR_INVALID_BANK and @p id through
 *       the registered callback, if any. It returns false without changing the
 *       bank or tracker. Atomicity requires @ref status_concurrency.
 */
bool status_test_and_clear_info(uint16_t id);

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
 * @brief Read the singleton's last-set fault tracker.
 *
 * @return A valid ID recorded by status_set_fault(), or STATUS_UNSET_ID if the
 *         tracker is still at its initial value.
 *
 * @note An atomic tracker read does not describe the current bank state.
 *       Clear operations preserve the tracker. During overlapping setters,
 *       this may return an earlier ID or STATUS_UNSET_ID. See
 *       @ref status_trackers for completion and synchronisation requirements.
 */
uint16_t status_last_fault(void);

/**
 * @brief Read the singleton's last-set warning tracker.
 *
 * @return A valid ID recorded by status_set_warning(), or STATUS_UNSET_ID if
 *         the tracker is still at its initial value.
 *
 * @note An atomic tracker read does not describe the current bank state.
 *       Clear operations preserve the tracker. During overlapping setters,
 *       this may return an earlier ID or STATUS_UNSET_ID. See
 *       @ref status_trackers for completion and synchronisation requirements.
 */
uint16_t status_last_warning(void);

/**
 * @brief Read the singleton's last-set info tracker.
 *
 * @return A valid ID recorded by status_set_info(), or STATUS_UNSET_ID if the
 *         tracker is still at its initial value.
 *
 * @note An atomic tracker read does not describe the current bank state.
 *       Clear operations preserve the tracker. During overlapping setters,
 *       this may return an earlier ID or STATUS_UNSET_ID. See
 *       @ref status_trackers for completion and synchronisation requirements.
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
