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

static volatile uint16_t fault_banks[NUM_STATUS_BANKS];
static volatile uint16_t warning_banks[NUM_STATUS_BANKS];
static volatile uint16_t info_banks[NUM_STATUS_BANKS];

static volatile uint16_t last_fault_id = STATUS_UNSET_ID;
static volatile uint16_t last_warning_id = STATUS_UNSET_ID;
static volatile uint16_t last_info_id = STATUS_UNSET_ID;

static volatile status_err_cb_t err_cb = NULL;

/* ================ MACROS ================================================== */

/* ================ STATIC FUNCTIONS ======================================== */

static inline size_t
size_min(size_t a, size_t b)
{
        return (a < b) ? a : b;
}

/* Writeable view */
static inline volatile uint16_t *
get_banks_mut(enum status_class cls)
{
        volatile uint16_t *result;

        switch (cls) {
        case STATUS_CLASS_FAULT: result = fault_banks; break;
        case STATUS_CLASS_WARNING: result = warning_banks; break;
        case STATUS_CLASS_INFO: result = info_banks; break;
        default: result = NULL; break;
        }

        return result;
}

/* Read-only view */
static inline const volatile uint16_t *
get_banks_ro(enum status_class cls)
{
        return get_banks_mut(cls); /* adds const qualifier */
}

static void
invoke_err_cb(status_err_t err, uint16_t id)
{
        /*
         * Snapshot the callback pointer under the critical section to avoid a
         * data race with status_set_err_callback() on architectures or RTOS
         * environments where pointer reads are not naturally atomic.  The
         * actual invocation happens outside the critical section so that a
         * callback that re-enters the status API (e.g. to set a secondary
         * fault) does not deadlock.
         */
        STATUS_ENTER_CRITICAL();
        status_err_cb_t cb = err_cb;
        STATUS_EXIT_CRITICAL();

        if (cb != NULL) {
                cb(err, id);
        }
}

static void
set_bit(uint16_t id, enum status_class cls)
{
        uint16_t bank = status_bank(id);
        volatile uint16_t *b = get_banks_mut(cls);

        if (bank >= NUM_STATUS_BANKS) {
                invoke_err_cb(STATUS_ERR_INVALID_BANK, id);
        } else if (b == NULL) {
                invoke_err_cb(STATUS_ERR_INVALID_ID, id);
        } else {
                uint16_t bit = status_bit(id);

                STATUS_ENTER_CRITICAL();
                b[bank] = (uint16_t)(b[bank] | (uint16_t)((uint32_t)1u << (uint32_t)bit));
                switch (cls) {
                case STATUS_CLASS_FAULT: last_fault_id = id; break;
                case STATUS_CLASS_WARNING: last_warning_id = id; break;
                case STATUS_CLASS_INFO: last_info_id = id; break;
                default: break;
                }
                STATUS_EXIT_CRITICAL();
        }
}

static void
clear_bit(uint16_t id, enum status_class cls)
{
        uint16_t bank = status_bank(id);
        volatile uint16_t *b = get_banks_mut(cls);

        if (bank >= NUM_STATUS_BANKS) {
                invoke_err_cb(STATUS_ERR_INVALID_BANK, id);
        } else if (b == NULL) {
                invoke_err_cb(STATUS_ERR_INVALID_ID, id);
        } else {
                uint16_t bit = status_bit(id);

                STATUS_ENTER_CRITICAL();
                b[bank] = (uint16_t)(b[bank] & (uint16_t)(0xFFFFu ^ (uint16_t)((uint32_t)1u << (uint32_t)bit)));
                STATUS_EXIT_CRITICAL();
        }
}

static bool
is_bit_set(uint16_t id, enum status_class cls)
{
        uint16_t bank = status_bank(id);
        const volatile uint16_t *b = get_banks_ro(cls);
        bool result = false;

        if (bank >= NUM_STATUS_BANKS) {
                invoke_err_cb(STATUS_ERR_INVALID_BANK, id);
        } else if (b == NULL) {
                invoke_err_cb(STATUS_ERR_INVALID_ID, id);
        } else {
                uint16_t bit = status_bit(id);

                STATUS_ENTER_CRITICAL();
                result =
                    (b[bank] & (uint16_t)((uint32_t)1u << (uint32_t)bit)) != 0u;
                STATUS_EXIT_CRITICAL();
        }

        return result;
}

/* ================ GLOBAL FUNCTIONS ======================================== */

void
status_init(void)
{
        STATUS_ENTER_CRITICAL();
        for (size_t i = 0u; i < NUM_STATUS_BANKS; ++i) {
                fault_banks[i] = 0u;
                warning_banks[i] = 0u;
                info_banks[i] = 0u;
        }
        last_fault_id = STATUS_UNSET_ID;
        last_warning_id = STATUS_UNSET_ID;
        last_info_id = STATUS_UNSET_ID;
        STATUS_EXIT_CRITICAL();
}

void
status_set_err_callback(status_err_cb_t cb)
{
        STATUS_ENTER_CRITICAL();
        err_cb = cb;
        STATUS_EXIT_CRITICAL();
}

void
status_set_warning(uint16_t id)
{
        set_bit(id, STATUS_CLASS_WARNING);
}

void
status_set_fault(uint16_t id)
{
        set_bit(id, STATUS_CLASS_FAULT);
}

void
status_set_info(uint16_t id)
{
        set_bit(id, STATUS_CLASS_INFO);
}

void
status_clear_warning(uint16_t id)
{
        clear_bit(id, STATUS_CLASS_WARNING);
}

void
status_clear_fault(uint16_t id)
{
        clear_bit(id, STATUS_CLASS_FAULT);
}

void
status_clear_info(uint16_t id)
{
        clear_bit(id, STATUS_CLASS_INFO);
}

bool
status_is_warning_set(uint16_t id)
{
        return is_bit_set(id, STATUS_CLASS_WARNING);
}

bool
status_is_fault_set(uint16_t id)
{
        return is_bit_set(id, STATUS_CLASS_FAULT);
}

bool
status_is_info_set(uint16_t id)
{
        return is_bit_set(id, STATUS_CLASS_INFO);
}

bool
status_any(enum status_class cls)
{
        const volatile uint16_t *b = get_banks_ro(cls);
        bool result = false;

        if (b == NULL) {
                invoke_err_cb(STATUS_ERR_INVALID_ID, STATUS_UNSET_ID);
        } else {
                STATUS_ENTER_CRITICAL();
                for (size_t i = 0u; (i < NUM_STATUS_BANKS) && !result; ++i) {
                        if (b[i] != 0u) {
                                result = true;
                        }
                }
                STATUS_EXIT_CRITICAL();
        }

        return result;
}

void
status_clear_all(enum status_class cls)
{
        volatile uint16_t *b = get_banks_mut(cls);

        if (b == NULL) {
                invoke_err_cb(STATUS_ERR_INVALID_ID, STATUS_UNSET_ID);
        } else {
                STATUS_ENTER_CRITICAL();
                for (size_t i = 0u; i < NUM_STATUS_BANKS; ++i) {
                        b[i] = 0u;
                }
                STATUS_EXIT_CRITICAL();
        }
}

uint16_t
status_last_fault(void)
{
        STATUS_ENTER_CRITICAL();
        uint16_t id = last_fault_id;
        STATUS_EXIT_CRITICAL();
        return id;
}

uint16_t
status_last_warning(void)
{
        STATUS_ENTER_CRITICAL();
        uint16_t id = last_warning_id;
        STATUS_EXIT_CRITICAL();
        return id;
}

uint16_t
status_last_info(void)
{
        STATUS_ENTER_CRITICAL();
        uint16_t id = last_info_id;
        STATUS_EXIT_CRITICAL();
        return id;
}

void
status_snapshot(enum status_class cls, uint16_t *dst, size_t len)
{
        const volatile uint16_t *src = get_banks_ro(cls);

        if (src == NULL) {
                invoke_err_cb(STATUS_ERR_INVALID_ID, STATUS_UNSET_ID);
        } else if (dst == NULL) {
                invoke_err_cb(STATUS_ERR_NULL_PTR, STATUS_UNSET_ID);
        } else if (len == 0u) {
                invoke_err_cb(STATUS_ERR_INVALID_LEN, STATUS_UNSET_ID);
        } else {
                const size_t copy_len = size_min(len, NUM_STATUS_BANKS);

                STATUS_ENTER_CRITICAL();
                for (size_t i = 0u; i < copy_len; ++i) {
                        dst[i] = src[i];
                }
                STATUS_EXIT_CRITICAL();
        }
}
