/*
 * @file test_status_hooks.c
 * @brief Critical-section coverage for the no-atomics backend.
 */

#include <stdint.h>

#include "status.h"
#include "status_test.h"

static uint32_t critical_enter_count;
static uint32_t critical_exit_count;
static uint32_t critical_depth;
static uint32_t callback_count;

void
status_test_enter_critical(void)
{
        ++critical_enter_count;
        ++critical_depth;
}

void
status_test_exit_critical(void)
{
        TEST_ASSERT(critical_depth > 0u);

        ++critical_exit_count;
        --critical_depth;
}

static void
reset_critical_state(void)
{
        critical_enter_count = 0u;
        critical_exit_count = 0u;
        critical_depth = 0u;
        callback_count = 0u;
}

static void
error_callback(status_err_t err, uint16_t id)
{
        TEST_ASSERT(err == STATUS_ERR_INVALID_BANK);
        TEST_ASSERT(id == STATUS_ENCODE(NUM_STATUS_BANKS, 0u));
        TEST_ASSERT(critical_depth == 0u);

        ++callback_count;
}

int
main(void)
{
        status_reg_t reg;
        const uint16_t id = STATUS_ENCODE(0u, 0u);
        const uint16_t invalid_id = STATUS_ENCODE(NUM_STATUS_BANKS, 0u);

        status_init();
        status_reg_init(&reg);

        reset_critical_state();
        status_set_err_callback(error_callback);
        TEST_ASSERT(critical_enter_count == 1u);
        TEST_ASSERT(critical_exit_count == 1u);
        TEST_ASSERT(critical_depth == 0u);

        reset_critical_state();
        status_set_fault(invalid_id);
        TEST_ASSERT(critical_enter_count == 1u);
        TEST_ASSERT(critical_exit_count == 1u);
        TEST_ASSERT(critical_depth == 0u);
        TEST_ASSERT(callback_count == 1u);

        status_reg_set_fault(&reg, id);
        reset_critical_state();
        TEST_ASSERT(status_reg_test_and_clear_fault(&reg, id));
        TEST_ASSERT(critical_enter_count == 1u);
        TEST_ASSERT(critical_exit_count == 1u);
        TEST_ASSERT(critical_depth == 0u);

        reset_critical_state();
        status_set_err_callback(NULL);
        TEST_ASSERT(critical_enter_count == 1u);
        TEST_ASSERT(critical_exit_count == 1u);
        TEST_ASSERT(critical_depth == 0u);

        TEST_PASS(__FILE__);
        return EXIT_SUCCESS;
}
