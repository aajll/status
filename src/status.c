/*
 * SPDX-License-Identifier: MIT
 *
 * @file: status.c
 * @brief Implementation of core status tracking functionality.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "status.h"

_Static_assert(NUM_STATUS_BANKS <= 4095u,
               "NUM_STATUS_BANKS must be <= 4095 to avoid collision with "
               "STATUS_UNSET_ID (0xFFFF)");
_Static_assert(NUM_STATUS_BITS == 16u,
               "NUM_STATUS_BITS must equal the width of the bank storage type "
               "(uint16_t)");

static status_reg_t default_reg = {
    .last_fault_id = STATUS_UNSET_ID,
    .last_warning_id = STATUS_UNSET_ID,
    .last_info_id = STATUS_UNSET_ID,
};

static inline size_t
size_min(size_t a, size_t b)
{
        return (a < b) ? a : b;
}

static inline STATUS_ATOMIC_QUAL uint16_t *
get_banks_mut(status_reg_t *reg, enum status_class cls)
{
        STATUS_ATOMIC_QUAL uint16_t *result;

        switch (cls) {
        case STATUS_CLASS_FAULT: result = reg->fault_banks; break;
        case STATUS_CLASS_WARNING: result = reg->warning_banks; break;
        case STATUS_CLASS_INFO: result = reg->info_banks; break;
        default: result = NULL; break;
        }
        return result;
}

static inline const STATUS_ATOMIC_QUAL uint16_t *
get_banks_ro(const status_reg_t *reg, enum status_class cls)
{
        const STATUS_ATOMIC_QUAL uint16_t *result;

        switch (cls) {
        case STATUS_CLASS_FAULT: result = reg->fault_banks; break;
        case STATUS_CLASS_WARNING: result = reg->warning_banks; break;
        case STATUS_CLASS_INFO: result = reg->info_banks; break;
        default: result = NULL; break;
        }
        return result;
}

static status_err_cb_t
load_err_cb(const status_reg_t *reg)
{
#if defined(STATUS_USE_NO_ATOMICS)
        status_err_cb_t cb;

        STATUS_ENTER_CRITICAL();
        cb = reg->err_cb;
        STATUS_EXIT_CRITICAL();
        return cb;
#else
        return STATUS_ATOMIC_LOAD(&reg->err_cb);
#endif
}

static void
store_err_cb(status_reg_t *reg, status_err_cb_t cb)
{
#if defined(STATUS_USE_NO_ATOMICS)
        STATUS_ENTER_CRITICAL();
        reg->err_cb = cb;
        STATUS_EXIT_CRITICAL();
#else
        STATUS_ATOMIC_STORE(&reg->err_cb, cb);
#endif
}

static void
invoke_err_cb(const status_reg_t *reg, status_err_t err, uint16_t id)
{
        status_err_cb_t cb = load_err_cb(reg);

        if (cb != NULL) {
                cb(err, id);
        }
}

static void
reset_reg(status_reg_t *reg)
{
        for (size_t i = 0u; i < NUM_STATUS_BANKS; ++i) {
                STATUS_ATOMIC_STORE(&reg->fault_banks[i], (uint16_t)0u);
                STATUS_ATOMIC_STORE(&reg->warning_banks[i], (uint16_t)0u);
                STATUS_ATOMIC_STORE(&reg->info_banks[i], (uint16_t)0u);
        }
        STATUS_ATOMIC_STORE(&reg->last_fault_id, (uint16_t)STATUS_UNSET_ID);
        STATUS_ATOMIC_STORE(&reg->last_warning_id, (uint16_t)STATUS_UNSET_ID);
        STATUS_ATOMIC_STORE(&reg->last_info_id, (uint16_t)STATUS_UNSET_ID);
}

static void
set_bit(status_reg_t *reg, STATUS_ATOMIC_QUAL uint16_t *banks,
        STATUS_ATOMIC_QUAL uint16_t *last, uint16_t id)
{
        uint16_t bank = status_bank(id);

        if (bank >= NUM_STATUS_BANKS) {
                invoke_err_cb(reg, STATUS_ERR_INVALID_BANK, id);
        } else {
                uint16_t bit = status_bit(id);
                uint16_t mask = (uint16_t)((uint32_t)1u << (uint32_t)bit);

                STATUS_ATOMIC_OR(&banks[bank], mask);
                STATUS_ATOMIC_STORE(last, id);
        }
}

static void
clear_bit(status_reg_t *reg, STATUS_ATOMIC_QUAL uint16_t *banks, uint16_t id)
{
        uint16_t bank = status_bank(id);

        if (bank >= NUM_STATUS_BANKS) {
                invoke_err_cb(reg, STATUS_ERR_INVALID_BANK, id);
        } else {
                uint16_t bit = status_bit(id);
                uint16_t mask = (uint16_t)((uint32_t)1u << (uint32_t)bit);

                STATUS_ATOMIC_AND(&banks[bank], (uint16_t)(0xFFFFu ^ mask));
        }
}

static bool
test_and_clear_bit(status_reg_t *reg, STATUS_ATOMIC_QUAL uint16_t *banks,
                   uint16_t id)
{
        uint16_t bank = status_bank(id);
        uint16_t old;

        if (bank >= NUM_STATUS_BANKS) {
                invoke_err_cb(reg, STATUS_ERR_INVALID_BANK, id);
                return false;
        }

        const uint16_t mask =
            (uint16_t)((uint32_t)1u << (uint32_t)status_bit(id));
        STATUS_ATOMIC_FETCH_AND(&banks[bank], (uint16_t)(0xFFFFu ^ mask), old);
        return (old & mask) != 0u;
}

static bool
is_bit_set(const status_reg_t *reg, const STATUS_ATOMIC_QUAL uint16_t *banks,
           uint16_t id)
{
        uint16_t bank = status_bank(id);

        if (bank >= NUM_STATUS_BANKS) {
                invoke_err_cb(reg, STATUS_ERR_INVALID_BANK, id);
                return false;
        }

        return (STATUS_ATOMIC_LOAD(&banks[bank])
                & (uint16_t)((uint32_t)1u << (uint32_t)status_bit(id)))
               != 0u;
}

void
status_reg_init(status_reg_t *reg)
{
        if (reg == NULL) {
                return;
        }
        for (size_t i = 0u; i < NUM_STATUS_BANKS; ++i) {
                STATUS_ATOMIC_INIT(&reg->fault_banks[i], (uint16_t)0u);
                STATUS_ATOMIC_INIT(&reg->warning_banks[i], (uint16_t)0u);
                STATUS_ATOMIC_INIT(&reg->info_banks[i], (uint16_t)0u);
        }
        STATUS_ATOMIC_INIT(&reg->last_fault_id, (uint16_t)STATUS_UNSET_ID);
        STATUS_ATOMIC_INIT(&reg->last_warning_id, (uint16_t)STATUS_UNSET_ID);
        STATUS_ATOMIC_INIT(&reg->last_info_id, (uint16_t)STATUS_UNSET_ID);
        STATUS_ATOMIC_INIT(&reg->err_cb, NULL);
}

void
status_reg_set_err_callback(status_reg_t *reg, status_err_cb_t cb)
{
        if (reg != NULL) {
                store_err_cb(reg, cb);
        }
}

void
status_reg_set_warning(status_reg_t *r, uint16_t id)
{
        if (r != NULL) {
                set_bit(r, r->warning_banks, &r->last_warning_id, id);
        }
}
void
status_reg_set_fault(status_reg_t *r, uint16_t id)
{
        if (r != NULL) {
                set_bit(r, r->fault_banks, &r->last_fault_id, id);
        }
}
void
status_reg_set_info(status_reg_t *r, uint16_t id)
{
        if (r != NULL) {
                set_bit(r, r->info_banks, &r->last_info_id, id);
        }
}
void
status_reg_clear_warning(status_reg_t *r, uint16_t id)
{
        if (r != NULL) {
                clear_bit(r, r->warning_banks, id);
        }
}
void
status_reg_clear_fault(status_reg_t *r, uint16_t id)
{
        if (r != NULL) {
                clear_bit(r, r->fault_banks, id);
        }
}
void
status_reg_clear_info(status_reg_t *r, uint16_t id)
{
        if (r != NULL) {
                clear_bit(r, r->info_banks, id);
        }
}
bool
status_reg_test_and_clear_warning(status_reg_t *r, uint16_t id)
{
        return r != NULL && test_and_clear_bit(r, r->warning_banks, id);
}
bool
status_reg_test_and_clear_fault(status_reg_t *r, uint16_t id)
{
        return r != NULL && test_and_clear_bit(r, r->fault_banks, id);
}
bool
status_reg_test_and_clear_info(status_reg_t *r, uint16_t id)
{
        return r != NULL && test_and_clear_bit(r, r->info_banks, id);
}
bool
status_reg_is_warning_set(const status_reg_t *r, uint16_t id)
{
        return r != NULL && is_bit_set(r, r->warning_banks, id);
}
bool
status_reg_is_fault_set(const status_reg_t *r, uint16_t id)
{
        return r != NULL && is_bit_set(r, r->fault_banks, id);
}
bool
status_reg_is_info_set(const status_reg_t *r, uint16_t id)
{
        return r != NULL && is_bit_set(r, r->info_banks, id);
}

bool
status_reg_any(const status_reg_t *reg, enum status_class cls)
{
        if (reg == NULL) {
                return false;
        }
        const STATUS_ATOMIC_QUAL uint16_t *banks = get_banks_ro(reg, cls);
        if (banks == NULL) {
                invoke_err_cb(reg, STATUS_ERR_INVALID_ID, STATUS_UNSET_ID);
                return false;
        }
        for (size_t i = 0u; i < NUM_STATUS_BANKS; ++i) {
                if (STATUS_ATOMIC_LOAD(&banks[i]) != 0u) {
                        return true;
                }
        }
        return false;
}

void
status_reg_clear_all(status_reg_t *reg, enum status_class cls)
{
        if (reg == NULL) {
                return;
        }
        STATUS_ATOMIC_QUAL uint16_t *banks = get_banks_mut(reg, cls);
        if (banks == NULL) {
                invoke_err_cb(reg, STATUS_ERR_INVALID_ID, STATUS_UNSET_ID);
                return;
        }
        for (size_t i = 0u; i < NUM_STATUS_BANKS; ++i) {
                STATUS_ATOMIC_STORE(&banks[i], (uint16_t)0u);
        }
}

uint16_t
status_reg_last_fault(const status_reg_t *r)
{
        return r == NULL ? STATUS_UNSET_ID
                         : STATUS_ATOMIC_LOAD(&r->last_fault_id);
}
uint16_t
status_reg_last_warning(const status_reg_t *r)
{
        return r == NULL ? STATUS_UNSET_ID
                         : STATUS_ATOMIC_LOAD(&r->last_warning_id);
}
uint16_t
status_reg_last_info(const status_reg_t *r)
{
        return r == NULL ? STATUS_UNSET_ID
                         : STATUS_ATOMIC_LOAD(&r->last_info_id);
}

void
status_reg_snapshot(const status_reg_t *reg, enum status_class cls,
                    uint16_t *dst, size_t len)
{
        if (reg == NULL) {
                return;
        }
        const STATUS_ATOMIC_QUAL uint16_t *src = get_banks_ro(reg, cls);
        if (src == NULL) {
                invoke_err_cb(reg, STATUS_ERR_INVALID_ID, STATUS_UNSET_ID);
                return;
        }
        if (dst == NULL) {
                invoke_err_cb(reg, STATUS_ERR_NULL_PTR, STATUS_UNSET_ID);
                return;
        }
        if (len == 0u) {
                invoke_err_cb(reg, STATUS_ERR_INVALID_LEN, STATUS_UNSET_ID);
                return;
        }
        for (size_t i = 0u; i < size_min(len, NUM_STATUS_BANKS); ++i) {
                dst[i] = STATUS_ATOMIC_LOAD(&src[i]);
        }
}

void
status_init(void)
{
        reset_reg(&default_reg);
}
void
status_set_err_callback(status_err_cb_t cb)
{
        status_reg_set_err_callback(&default_reg, cb);
}
void
status_set_warning(uint16_t id)
{
        status_reg_set_warning(&default_reg, id);
}
void
status_set_fault(uint16_t id)
{
        status_reg_set_fault(&default_reg, id);
}
void
status_set_info(uint16_t id)
{
        status_reg_set_info(&default_reg, id);
}
void
status_clear_warning(uint16_t id)
{
        status_reg_clear_warning(&default_reg, id);
}
void
status_clear_fault(uint16_t id)
{
        status_reg_clear_fault(&default_reg, id);
}
void
status_clear_info(uint16_t id)
{
        status_reg_clear_info(&default_reg, id);
}
bool
status_test_and_clear_warning(uint16_t id)
{
        return status_reg_test_and_clear_warning(&default_reg, id);
}
bool
status_test_and_clear_fault(uint16_t id)
{
        return status_reg_test_and_clear_fault(&default_reg, id);
}
bool
status_test_and_clear_info(uint16_t id)
{
        return status_reg_test_and_clear_info(&default_reg, id);
}
bool
status_is_warning_set(uint16_t id)
{
        return status_reg_is_warning_set(&default_reg, id);
}
bool
status_is_fault_set(uint16_t id)
{
        return status_reg_is_fault_set(&default_reg, id);
}
bool
status_is_info_set(uint16_t id)
{
        return status_reg_is_info_set(&default_reg, id);
}
bool
status_any(enum status_class cls)
{
        return status_reg_any(&default_reg, cls);
}
void
status_clear_all(enum status_class cls)
{
        status_reg_clear_all(&default_reg, cls);
}
uint16_t
status_last_fault(void)
{
        return status_reg_last_fault(&default_reg);
}
uint16_t
status_last_warning(void)
{
        return status_reg_last_warning(&default_reg);
}
uint16_t
status_last_info(void)
{
        return status_reg_last_info(&default_reg);
}
void
status_snapshot(enum status_class cls, uint16_t *dst, size_t len)
{
        status_reg_snapshot(&default_reg, cls, dst, len);
}
