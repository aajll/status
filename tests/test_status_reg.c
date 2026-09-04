#include "status.h"
#include "status_test.h"

#include <stdint.h>

static status_err_t last_err_a;
static uint32_t err_count_a;
static uint32_t err_count_b;

static void
err_a(status_err_t err, uint16_t id)
{
        (void)id;
        last_err_a = err;
        ++err_count_a;
}

static void
err_b(status_err_t err, uint16_t id)
{
        (void)err;
        (void)id;
        ++err_count_b;
}

int
main(void)
{
        status_reg_t a;
        status_reg_t b;
        uint16_t snapshot[NUM_STATUS_BANKS];
        const uint16_t fault = STATUS_ENCODE(0u, 0u);
        const uint16_t warning = STATUS_ENCODE(1u, 1u);
        const uint16_t info = STATUS_ENCODE(2u, 2u);
        const uint16_t invalid = STATUS_ENCODE(NUM_STATUS_BANKS, 0u);

        status_reg_init(&a);
        status_reg_init(&b);
        TEST_ASSERT(status_reg_last_fault(&a) == STATUS_UNSET_ID);
        TEST_ASSERT(status_reg_last_warning(&a) == STATUS_UNSET_ID);
        TEST_ASSERT(status_reg_last_info(&a) == STATUS_UNSET_ID);

        status_reg_set_fault(&a, fault);
        status_reg_set_warning(&a, warning);
        status_reg_set_info(&a, info);
        TEST_ASSERT(status_reg_is_fault_set(&a, fault));
        TEST_ASSERT(status_reg_is_warning_set(&a, warning));
        TEST_ASSERT(status_reg_is_info_set(&a, info));
        TEST_ASSERT(!status_reg_any(&b, STATUS_CLASS_FAULT));
        TEST_ASSERT(status_reg_last_fault(&a) == fault);
        TEST_ASSERT(status_reg_last_warning(&a) == warning);
        TEST_ASSERT(status_reg_last_info(&a) == info);

        status_reg_snapshot(&a, STATUS_CLASS_FAULT, snapshot, NUM_STATUS_BANKS);
        TEST_ASSERT((snapshot[0] & 1u) != 0u);
        status_reg_clear_fault(&a, fault);
        status_reg_clear_warning(&a, warning);
        status_reg_clear_info(&a, info);
        TEST_ASSERT(!status_reg_is_fault_set(&a, fault));
        TEST_ASSERT(!status_reg_is_warning_set(&a, warning));
        TEST_ASSERT(!status_reg_is_info_set(&a, info));

        status_reg_set_fault(&a, fault);
        status_reg_clear_all(&a, STATUS_CLASS_FAULT);
        TEST_ASSERT(!status_reg_any(&a, STATUS_CLASS_FAULT));

        status_reg_set_err_callback(&a, err_a);
        status_reg_set_err_callback(&b, err_b);
        status_reg_set_fault(&a, invalid);
        TEST_ASSERT(err_count_a == 1u);
        TEST_ASSERT(err_count_b == 0u);
        TEST_ASSERT(last_err_a == STATUS_ERR_INVALID_BANK);
        status_reg_set_fault(&b, invalid);
        TEST_ASSERT(err_count_b == 1u);
        status_reg_init(&a);
        status_reg_set_fault(&a, invalid);
        TEST_ASSERT(err_count_a == 1u);

        status_reg_init(NULL);
        status_reg_set_err_callback(NULL, err_a);
        status_reg_set_fault(NULL, fault);
        status_reg_set_warning(NULL, warning);
        status_reg_set_info(NULL, info);
        status_reg_clear_fault(NULL, fault);
        status_reg_clear_warning(NULL, warning);
        status_reg_clear_info(NULL, info);
        status_reg_clear_all(NULL, STATUS_CLASS_FAULT);
        status_reg_snapshot(NULL, STATUS_CLASS_FAULT, snapshot,
                            NUM_STATUS_BANKS);
        TEST_ASSERT(!status_reg_is_fault_set(NULL, fault));
        TEST_ASSERT(!status_reg_is_warning_set(NULL, warning));
        TEST_ASSERT(!status_reg_is_info_set(NULL, info));
        TEST_ASSERT(!status_reg_any(NULL, STATUS_CLASS_FAULT));
        TEST_ASSERT(status_reg_last_fault(NULL) == STATUS_UNSET_ID);
        TEST_ASSERT(status_reg_last_warning(NULL) == STATUS_UNSET_ID);
        TEST_ASSERT(status_reg_last_info(NULL) == STATUS_UNSET_ID);

        TEST_PASS(__FILE__);
        return EXIT_SUCCESS;
}
