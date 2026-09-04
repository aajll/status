/**
 * SPDX-License-Identifier: MIT
 *
 * @file: status.c
 *
 * @brief
 *    Implementation of core status tracking functionality.
 */

/* ================ INCLUDES ================================================ */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "status.h"

/* ================ DEFINES ================================================= */

/* ---------------- Configuration ------------------------------------------- */

/*
 * NUM_STATUS_BANKS must be strictly less than 4096 so that the maximum valid
 * encoded ID (STATUS_ENCODE(NUM_STATUS_BANKS-1, 15)) cannot equal
 * STATUS_UNSET_ID (0xFFFF = STATUS_ENCODE(4095, 15)).
 */
_Static_assert(NUM_STATUS_BANKS <= 4095u,
               "NUM_STATUS_BANKS must be <= 4095 to avoid collision with "
               "STATUS_UNSET_ID (0xFFFF)");

/*
 * NUM_STATUS_BITS is fixed at 16 to match the uint16_t bank storage type.
 * status_bit() always returns a value in 0..15, guaranteed by the 4-bit mask
 * in STATUS_ENCODE and the extraction in status_bit(). No runtime bit-range
 * check is needed.
 */
_Static_assert(NUM_STATUS_BITS == 16u,
               "NUM_STATUS_BITS must equal the width of the bank storage type "
               "(uint16_t)");

/* ================ STRUCTURES ============================================== */

/* ================ TYPEDEFS ================================================ */

/* ================ STATIC PROTOTYPES ======================================= */

/* ================ STATIC VARIABLES ======================================== */

static STATUS_ATOMIC_QUAL uint16_t fault_banks[NUM_STATUS_BANKS];
static STATUS_ATOMIC_QUAL uint16_t warning_banks[NUM_STATUS_BANKS];
static STATUS_ATOMIC_QUAL uint16_t info_banks[NUM_STATUS_BANKS];

static STATUS_ATOMIC_QUAL uint16_t last_fault_id = STATUS_UNSET_ID;
static STATUS_ATOMIC_QUAL uint16_t last_warning_id = STATUS_UNSET_ID;
static STATUS_ATOMIC_QUAL uint16_t last_info_id = STATUS_UNSET_ID;

static STATUS_ATOMIC_QUAL status_err_cb_t err_cb = NULL;

/* ================ MACROS ================================================== */

/* ================ STATIC FUNCTIONS ======================================== */

static inline size_t
size_min(size_t a, size_t b)
{
        return (a < b) ? a : b;
}

/* Writeable view */
static inline STATUS_ATOMIC_QUAL uint16_t *
get_banks_mut(enum status_class cls)
{
        STATUS_ATOMIC_QUAL uint16_t *result;

        switch (cls) {
        case STATUS_CLASS_FAULT: result = fault_banks; break;
        case STATUS_CLASS_WARNING: result = warning_banks; break;
        case STATUS_CLASS_INFO: result = info_banks; break;
        default: result = NULL; break;
        }

        return result;
}

/* Read-only view */
static inline const STATUS_ATOMIC_QUAL uint16_t *
get_banks_ro(enum status_class cls)
{
        return get_banks_mut(cls); /* adds const qualifier */
}

static status_err_cb_t
load_err_cb(void)
{
#if defined(STATUS_USE_NO_ATOMICS)
        status_err_cb_t cb;

        /* A function pointer may exceed the target's atomic access width. */
        STATUS_ENTER_CRITICAL();
        cb = err_cb;
        STATUS_EXIT_CRITICAL();

        return cb;
#else
        return STATUS_ATOMIC_LOAD(&err_cb);
#endif
}

static void
store_err_cb(status_err_cb_t cb)
{
#if defined(STATUS_USE_NO_ATOMICS)
        /* A function pointer may exceed the target's atomic access width. */
        STATUS_ENTER_CRITICAL();
        err_cb = cb;
        STATUS_EXIT_CRITICAL();
#else
        STATUS_ATOMIC_STORE(&err_cb, cb);
#endif
}

static void
invoke_err_cb(status_err_t err, uint16_t id)
{
        /*
         * The callback pointer is loaded with the backend's concurrency
         * protection, but invocation happens after that protection ends. A
         * callback may therefore safely re-enter the status API.
         */
        status_err_cb_t cb = load_err_cb();

        if (cb != NULL) {
                cb(err, id);
        }
}

/*
 * The private bit helpers operate on an already-resolved bank array (and, for
 * set_bit, the matching last-set tracker). The public wrappers pass the storage
 * for a fixed, valid class, so these pointers are never NULL and no class
 * validation is needed here; the only runtime check is the user-supplied bank
 * range. Class validation lives in the public functions that take a class
 * argument (status_any, status_clear_all, status_snapshot).
 */
static void
set_bit(STATUS_ATOMIC_QUAL uint16_t *banks, STATUS_ATOMIC_QUAL uint16_t *last,
        uint16_t id)
{
        uint16_t bank = status_bank(id);

        if (bank >= NUM_STATUS_BANKS) {
                invoke_err_cb(STATUS_ERR_INVALID_BANK, id);
        } else {
                uint16_t bit = status_bit(id);
                uint16_t mask = (uint16_t)((uint32_t)1u << (uint32_t)bit);

                STATUS_ATOMIC_OR(&banks[bank], mask);
                /*
                 * The tracker store is a separate atomic op, so the bit and the
                 * "last set" record are not updated as one transaction; the
                 * tracker is a best-effort most-recent-wins audit hint.
                 */
                STATUS_ATOMIC_STORE(last, id);
        }
}

static void
clear_bit(STATUS_ATOMIC_QUAL uint16_t *banks, uint16_t id)
{
        uint16_t bank = status_bank(id);

        if (bank >= NUM_STATUS_BANKS) {
                invoke_err_cb(STATUS_ERR_INVALID_BANK, id);
        } else {
                uint16_t bit = status_bit(id);
                uint16_t mask = (uint16_t)((uint32_t)1u << (uint32_t)bit);

                STATUS_ATOMIC_AND(&banks[bank], (uint16_t)(0xFFFFu ^ mask));
        }
}

static bool
is_bit_set(const STATUS_ATOMIC_QUAL uint16_t *banks, uint16_t id)
{
        uint16_t bank = status_bank(id);
        bool result = false;

        if (bank >= NUM_STATUS_BANKS) {
                invoke_err_cb(STATUS_ERR_INVALID_BANK, id);
        } else {
                uint16_t bit = status_bit(id);
                uint16_t mask = (uint16_t)((uint32_t)1u << (uint32_t)bit);

                result = (STATUS_ATOMIC_LOAD(&banks[bank]) & mask) != 0u;
        }

        return result;
}

/* ================ GLOBAL FUNCTIONS ======================================== */

void
status_init(void)
{
        /*
         * Each store is individually atomic. status_init() is not itself a
         * transaction; it is intended to run before any concurrent producer is
         * active (e.g. at start-up or a coordinated re-init).
         */
        for (size_t i = 0u; i < NUM_STATUS_BANKS; ++i) {
                STATUS_ATOMIC_STORE(&fault_banks[i], (uint16_t)0u);
                STATUS_ATOMIC_STORE(&warning_banks[i], (uint16_t)0u);
                STATUS_ATOMIC_STORE(&info_banks[i], (uint16_t)0u);
        }
        STATUS_ATOMIC_STORE(&last_fault_id, (uint16_t)STATUS_UNSET_ID);
        STATUS_ATOMIC_STORE(&last_warning_id, (uint16_t)STATUS_UNSET_ID);
        STATUS_ATOMIC_STORE(&last_info_id, (uint16_t)STATUS_UNSET_ID);
}

void
status_set_err_callback(status_err_cb_t cb)
{
        store_err_cb(cb);
}

void
status_set_warning(uint16_t id)
{
        set_bit(warning_banks, &last_warning_id, id);
}

void
status_set_fault(uint16_t id)
{
        set_bit(fault_banks, &last_fault_id, id);
}

void
status_set_info(uint16_t id)
{
        set_bit(info_banks, &last_info_id, id);
}

void
status_clear_warning(uint16_t id)
{
        clear_bit(warning_banks, id);
}

void
status_clear_fault(uint16_t id)
{
        clear_bit(fault_banks, id);
}

void
status_clear_info(uint16_t id)
{
        clear_bit(info_banks, id);
}

bool
status_is_warning_set(uint16_t id)
{
        return is_bit_set(warning_banks, id);
}

bool
status_is_fault_set(uint16_t id)
{
        return is_bit_set(fault_banks, id);
}

bool
status_is_info_set(uint16_t id)
{
        return is_bit_set(info_banks, id);
}

bool
status_any(enum status_class cls)
{
        const STATUS_ATOMIC_QUAL uint16_t *b = get_banks_ro(cls);
        bool result = false;

        if (b == NULL) {
                invoke_err_cb(STATUS_ERR_INVALID_ID, STATUS_UNSET_ID);
        } else {
                /*
                 * Each bank is read atomically, but the scan is not a single
                 * consistent snapshot of the class (see the header note).
                 */
                for (size_t i = 0u; (i < NUM_STATUS_BANKS) && !result; ++i) {
                        if (STATUS_ATOMIC_LOAD(&b[i]) != 0u) {
                                result = true;
                        }
                }
        }

        return result;
}

void
status_clear_all(enum status_class cls)
{
        STATUS_ATOMIC_QUAL uint16_t *b = get_banks_mut(cls);

        if (b == NULL) {
                invoke_err_cb(STATUS_ERR_INVALID_ID, STATUS_UNSET_ID);
        } else {
                for (size_t i = 0u; i < NUM_STATUS_BANKS; ++i) {
                        STATUS_ATOMIC_STORE(&b[i], (uint16_t)0u);
                }
        }
}

uint16_t
status_last_fault(void)
{
        return STATUS_ATOMIC_LOAD(&last_fault_id);
}

uint16_t
status_last_warning(void)
{
        return STATUS_ATOMIC_LOAD(&last_warning_id);
}

uint16_t
status_last_info(void)
{
        return STATUS_ATOMIC_LOAD(&last_info_id);
}

void
status_snapshot(enum status_class cls, uint16_t *dst, size_t len)
{
        const STATUS_ATOMIC_QUAL uint16_t *src = get_banks_ro(cls);

        if (src == NULL) {
                invoke_err_cb(STATUS_ERR_INVALID_ID, STATUS_UNSET_ID);
        } else if (dst == NULL) {
                invoke_err_cb(STATUS_ERR_NULL_PTR, STATUS_UNSET_ID);
        } else if (len == 0u) {
                invoke_err_cb(STATUS_ERR_INVALID_LEN, STATUS_UNSET_ID);
        } else {
                const size_t copy_len = size_min(len, NUM_STATUS_BANKS);

                /*
                 * Bank-by-bank atomic copy. Each bank is coherent; the whole
                 * snapshot is not a single instant of the class.
                 */
                for (size_t i = 0u; i < copy_len; ++i) {
                        dst[i] = STATUS_ATOMIC_LOAD(&src[i]);
                }
        }
}
