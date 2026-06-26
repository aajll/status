/*
 * @file: test_status.c
 * @brief Unit tests for the status module.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * The host build auto-selects the GCC/Clang atomic backend, so set/clear are
 * genuinely atomic and no critical-section hooks are required.
 */
#include "status.h"
#include "status_ids.h"

/* ------------------------------------------------------------------ */
/* Test infrastructure                                                  */
/* ------------------------------------------------------------------ */

/*
 * TEST_ASSERT always fires regardless of NDEBUG, unlike <assert.h>.
 */
#define TEST_ASSERT(expr)                                                      \
        do {                                                                   \
                if (!(expr)) {                                                 \
                        fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__,         \
                                __LINE__, #expr);                              \
                        exit(EXIT_FAILURE);                                    \
                }                                                              \
        } while (0)

#define TEST_PASS(name) fprintf(stdout, "PASS  %s\n", (name))

/* ------------------------------------------------------------------ */
/* Error-callback fixture                                               */
/* ------------------------------------------------------------------ */

static status_err_t g_last_err;
static uint16_t g_last_err_id;
static unsigned int g_err_count;

static void
test_err_cb(status_err_t err, uint16_t id)
{
        g_last_err = err;
        g_last_err_id = id;
        ++g_err_count;
}

static void
reset_err_state(void)
{
        g_last_err = STATUS_ERR_INVALID_ID;
        g_last_err_id = 0u;
        g_err_count = 0u;
}

static void
setUp(void)
{
        status_init();
        status_set_err_callback(test_err_cb);
        reset_err_state();
}

/* ------------------------------------------------------------------ */
/* Tests — existing behaviour                                           */
/* ------------------------------------------------------------------ */

static void
test_fault_set_and_clear(void)
{
        setUp();

        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);
        TEST_ASSERT(status_is_fault_set(STATUS_ID_FAULT_OVERCURRENT) == true);

        status_clear_fault(STATUS_ID_FAULT_OVERCURRENT);
        TEST_ASSERT(status_is_fault_set(STATUS_ID_FAULT_OVERCURRENT) == false);

        TEST_PASS(__func__);
}

static void
test_warn_set_and_clear(void)
{
        setUp();

        status_set_warning(STATUS_ID_WARN_TEMP_NEAR_LIMIT);
        TEST_ASSERT(status_is_warning_set(STATUS_ID_WARN_TEMP_NEAR_LIMIT)
                    == true);

        status_clear_warning(STATUS_ID_WARN_TEMP_NEAR_LIMIT);
        TEST_ASSERT(status_is_warning_set(STATUS_ID_WARN_TEMP_NEAR_LIMIT)
                    == false);

        TEST_PASS(__func__);
}

static void
test_info_set_and_clear(void)
{
        setUp();

        status_set_info(STATUS_ID_INFO_AC_LIVE);
        TEST_ASSERT(status_is_info_set(STATUS_ID_INFO_AC_LIVE) == true);

        status_clear_info(STATUS_ID_INFO_AC_LIVE);
        TEST_ASSERT(status_is_info_set(STATUS_ID_INFO_AC_LIVE) == false);

        TEST_PASS(__func__);
}

static void
test_is_fault_set(void)
{
        setUp();

        status_set_fault(STATUS_ID_FAULT_OVERVOLTAGE);
        TEST_ASSERT(status_is_fault_set(STATUS_ID_FAULT_OVERVOLTAGE) == true);
        TEST_ASSERT(status_is_fault_set(STATUS_ID_FAULT_OVERCURRENT) == false);

        TEST_PASS(__func__);
}

static void
test_is_warn_set(void)
{
        setUp();

        status_set_warning(STATUS_ID_WARN_FAN_PERF_DROP);
        TEST_ASSERT(status_is_warning_set(STATUS_ID_WARN_FAN_PERF_DROP)
                    == true);
        TEST_ASSERT(status_is_warning_set(STATUS_ID_WARN_CAN_LOAD_HIGH)
                    == false);

        TEST_PASS(__func__);
}

static void
test_is_info_set(void)
{
        setUp();

        status_set_info(STATUS_ID_INFO_TEMP_CHANGING);
        TEST_ASSERT(status_is_info_set(STATUS_ID_INFO_TEMP_CHANGING) == true);
        TEST_ASSERT(status_is_info_set(STATUS_ID_INFO_AC_LIVE) == false);

        TEST_PASS(__func__);
}

static void
test_status_any(void)
{
        setUp();

        TEST_ASSERT(status_any(STATUS_CLASS_WARNING) == false);

        status_set_fault(STATUS_ID_FAULT_UNDERVOLTAGE);
        TEST_ASSERT(status_any(STATUS_CLASS_FAULT) == true);

        status_clear_fault(STATUS_ID_FAULT_UNDERVOLTAGE);
        TEST_ASSERT(status_any(STATUS_CLASS_FAULT) == false);

        status_set_warning(STATUS_ID_WARN_TEMP_NEAR_LIMIT);
        TEST_ASSERT(status_any(STATUS_CLASS_FAULT) == false);

        TEST_PASS(__func__);
}

static void
test_status_clear_all_faults(void)
{
        setUp();

        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);
        TEST_ASSERT(status_any(STATUS_CLASS_FAULT) == true);

        status_clear_all(STATUS_CLASS_FAULT);
        TEST_ASSERT(status_any(STATUS_CLASS_FAULT) == false);

        TEST_PASS(__func__);
}

static void
test_status_clear_all_warnings(void)
{
        setUp();

        status_set_warning(STATUS_ID_WARN_TEMP_NEAR_LIMIT);
        TEST_ASSERT(status_any(STATUS_CLASS_WARNING) == true);

        status_clear_all(STATUS_CLASS_WARNING);
        TEST_ASSERT(status_any(STATUS_CLASS_WARNING) == false);

        TEST_PASS(__func__);
}

static void
test_status_clear_all_info(void)
{
        setUp();

        status_set_info(STATUS_ID_INFO_CAN_ACTIVE);
        TEST_ASSERT(status_any(STATUS_CLASS_INFO) == true);

        status_clear_all(STATUS_CLASS_INFO);
        TEST_ASSERT(status_any(STATUS_CLASS_INFO) == false);

        TEST_PASS(__func__);
}

static void
test_status_last_fault(void)
{
        setUp();

        status_set_fault(STATUS_ID_FAULT_CAN_TIMEOUT);
        TEST_ASSERT(status_last_fault() == STATUS_ID_FAULT_CAN_TIMEOUT);

        TEST_PASS(__func__);
}

static void
test_status_last_warning(void)
{
        setUp();

        status_set_warning(STATUS_ID_WARN_TEMP_NEAR_LIMIT);
        TEST_ASSERT(status_last_warning() == STATUS_ID_WARN_TEMP_NEAR_LIMIT);

        TEST_PASS(__func__);
}

static void
test_status_last_info(void)
{
        setUp();

        status_set_info(STATUS_ID_INFO_CAN_ACTIVE);
        TEST_ASSERT(status_last_info() == STATUS_ID_INFO_CAN_ACTIVE);

        TEST_PASS(__func__);
}

/* ------------------------------------------------------------------ */
/* Tests — new coverage                                                 */
/* ------------------------------------------------------------------ */

/*
 * Before any status_set_* call, last_*() must return STATUS_UNSET_ID.
 */
static void
test_last_unset_before_first_set(void)
{
        setUp();

        TEST_ASSERT(status_last_fault() == STATUS_UNSET_ID);
        TEST_ASSERT(status_last_warning() == STATUS_UNSET_ID);
        TEST_ASSERT(status_last_info() == STATUS_UNSET_ID);

        TEST_PASS(__func__);
}

/*
 * Passing an out-of-range bank must fire the error callback with
 * STATUS_ERR_INVALID_BANK and leave the register unchanged.
 */
static void
test_err_cb_invalid_bank(void)
{
        setUp();

        uint16_t bad_id = STATUS_ENCODE((uint16_t)NUM_STATUS_BANKS, 0u);
        status_set_fault(bad_id);

        TEST_ASSERT(g_err_count == 1u);
        TEST_ASSERT(g_last_err == STATUS_ERR_INVALID_BANK);
        TEST_ASSERT(g_last_err_id == bad_id);
        TEST_ASSERT(status_any(STATUS_CLASS_FAULT) == false);

        TEST_PASS(__func__);
}

/*
 * Same for clear and query — an invalid bank fires the callback for each call.
 */
static void
test_err_cb_invalid_bank_all_ops(void)
{
        setUp();

        uint16_t bad_id = STATUS_ENCODE((uint16_t)NUM_STATUS_BANKS, 0u);

        status_clear_fault(bad_id);
        TEST_ASSERT(g_err_count == 1u);
        reset_err_state();

        (void)status_is_fault_set(bad_id);
        TEST_ASSERT(g_err_count == 1u);

        TEST_PASS(__func__);
}

/*
 * The maximum valid bank and bit must not fire the error callback.
 */
static void
test_boundary_ids_valid(void)
{
        setUp();

        uint16_t max_id = STATUS_ENCODE((uint16_t)(NUM_STATUS_BANKS - 1u), 15u);

        status_set_fault(max_id);
        TEST_ASSERT(g_err_count == 0u);
        TEST_ASSERT(status_is_fault_set(max_id) == true);

        status_clear_fault(max_id);
        TEST_ASSERT(status_is_fault_set(max_id) == false);
        TEST_ASSERT(g_err_count == 0u);

        TEST_PASS(__func__);
}

/*
 * status_snapshot copies the live register state into a caller buffer.
 */
static void
test_snapshot_captures_state(void)
{
        setUp();

        uint16_t snap[NUM_STATUS_BANKS];

        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);
        status_set_fault(STATUS_ID_FAULT_OVERVOLTAGE);

        status_snapshot(STATUS_CLASS_FAULT, snap, NUM_STATUS_BANKS);

        /* Both IDs reside in bank 0 — verify the captured word. */
        uint16_t bank0_expect =
            (uint16_t)(((uint32_t)1u
                        << (uint32_t)status_bit(STATUS_ID_FAULT_OVERCURRENT))
                       | ((uint32_t)1u << (uint32_t)status_bit(
                              STATUS_ID_FAULT_OVERVOLTAGE)));
        TEST_ASSERT((snap[0] & bank0_expect) == bank0_expect);
        TEST_ASSERT(g_err_count == 0u);

        TEST_PASS(__func__);
}

/*
 * snapshot with a NULL destination must fire the error callback and not crash.
 */
static void
test_snapshot_null_dst(void)
{
        setUp();

        status_snapshot(STATUS_CLASS_FAULT, NULL, NUM_STATUS_BANKS);

        TEST_ASSERT(g_err_count == 1u);
        TEST_ASSERT(g_last_err == STATUS_ERR_NULL_PTR);

        TEST_PASS(__func__);
}

/*
 * snapshot with zero length must fire STATUS_ERR_INVALID_LEN (not NULL_PTR).
 */
static void
test_snapshot_zero_len(void)
{
        setUp();

        uint16_t snap[NUM_STATUS_BANKS];
        status_snapshot(STATUS_CLASS_FAULT, snap, 0u);

        TEST_ASSERT(g_err_count == 1u);
        TEST_ASSERT(g_last_err == STATUS_ERR_INVALID_LEN);

        TEST_PASS(__func__);
}

/*
 * snapshot with an invalid class must fire STATUS_ERR_INVALID_ID and not crash.
 */
static void
test_snapshot_invalid_class(void)
{
        setUp();

        uint16_t snap[NUM_STATUS_BANKS];
        status_snapshot((enum status_class)99, snap, NUM_STATUS_BANKS);

        TEST_ASSERT(g_err_count == 1u);
        TEST_ASSERT(g_last_err == STATUS_ERR_INVALID_ID);

        TEST_PASS(__func__);
}

/*
 * zero-length and null-dst must produce distinct error codes.
 */
static void
test_snapshot_error_codes_distinct(void)
{
        setUp();

        uint16_t snap[NUM_STATUS_BANKS];

        /* NULL dst → STATUS_ERR_NULL_PTR */
        status_snapshot(STATUS_CLASS_FAULT, NULL, NUM_STATUS_BANKS);
        TEST_ASSERT(g_last_err == STATUS_ERR_NULL_PTR);
        reset_err_state();

        /* len == 0 → STATUS_ERR_INVALID_LEN */
        status_snapshot(STATUS_CLASS_FAULT, snap, 0u);
        TEST_ASSERT(g_last_err == STATUS_ERR_INVALID_LEN);

        TEST_PASS(__func__);
}

/*
 * status_init() must preserve the registered error callback so that errors
 * arising during re-initialisation are still reported.
 */
static void
test_init_preserves_err_cb(void)
{
        setUp(); /* registers test_err_cb */

        status_init();

        /* callback should still be active after re-init */
        uint16_t bad_id = STATUS_ENCODE((uint16_t)NUM_STATUS_BANKS, 0u);
        status_set_fault(bad_id);

        TEST_ASSERT(g_err_count == 1u);
        TEST_ASSERT(g_last_err == STATUS_ERR_INVALID_BANK);

        TEST_PASS(__func__);
}

/*
 * Setting a fault must not affect warning or info registers, and vice-versa.
 */
static void
test_class_isolation(void)
{
        setUp();

        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);
        TEST_ASSERT(status_any(STATUS_CLASS_WARNING) == false);
        TEST_ASSERT(status_any(STATUS_CLASS_INFO) == false);
        status_clear_all(STATUS_CLASS_FAULT);

        status_set_warning(STATUS_ID_WARN_TEMP_NEAR_LIMIT);
        TEST_ASSERT(status_any(STATUS_CLASS_FAULT) == false);
        TEST_ASSERT(status_any(STATUS_CLASS_INFO) == false);
        status_clear_all(STATUS_CLASS_WARNING);

        status_set_info(STATUS_ID_INFO_AC_LIVE);
        TEST_ASSERT(status_any(STATUS_CLASS_FAULT) == false);
        TEST_ASSERT(status_any(STATUS_CLASS_WARNING) == false);

        TEST_PASS(__func__);
}

/*
 * status_any returns false for every class after status_init.
 */
static void
test_any_false_after_init(void)
{
        setUp();

        TEST_ASSERT(status_any(STATUS_CLASS_FAULT) == false);
        TEST_ASSERT(status_any(STATUS_CLASS_WARNING) == false);
        TEST_ASSERT(status_any(STATUS_CLASS_INFO) == false);

        TEST_PASS(__func__);
}

/*
 * Invalid status classes must report an error and leave state unchanged.
 */
static void
test_invalid_class_ops(void)
{
        setUp();

        TEST_ASSERT(status_any((enum status_class)99) == false);
        TEST_ASSERT(g_err_count == 1u);
        TEST_ASSERT(g_last_err == STATUS_ERR_INVALID_ID);
        TEST_ASSERT(g_last_err_id == STATUS_UNSET_ID);
        reset_err_state();

        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);
        status_clear_all((enum status_class)99);
        TEST_ASSERT(g_err_count == 1u);
        TEST_ASSERT(g_last_err == STATUS_ERR_INVALID_ID);
        TEST_ASSERT(g_last_err_id == STATUS_UNSET_ID);
        TEST_ASSERT(status_is_fault_set(STATUS_ID_FAULT_OVERCURRENT) == true);

        TEST_PASS(__func__);
}

/*
 * Multiple bits across multiple banks can coexist and clear independently.
 */
static void
test_multi_bit_multi_bank(void)
{
        setUp();

        status_set_fault(STATUS_ID_FAULT_OVERCURRENT); /* bank 0, bit 0 */
        status_set_fault(STATUS_ID_FAULT_CAN_TIMEOUT); /* bank 2, bit 0 */

        TEST_ASSERT(status_is_fault_set(STATUS_ID_FAULT_OVERCURRENT) == true);
        TEST_ASSERT(status_is_fault_set(STATUS_ID_FAULT_CAN_TIMEOUT) == true);

        status_clear_fault(STATUS_ID_FAULT_OVERCURRENT);
        TEST_ASSERT(status_is_fault_set(STATUS_ID_FAULT_OVERCURRENT) == false);
        TEST_ASSERT(status_is_fault_set(STATUS_ID_FAULT_CAN_TIMEOUT) == true);

        TEST_PASS(__func__);
}

/*
 * Verify that status_init properly resets all last_*_id trackers.
 */
static void
test_init_resets_last_ids(void)
{
        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);
        status_set_warning(STATUS_ID_WARN_TEMP_NEAR_LIMIT);
        status_set_info(STATUS_ID_INFO_AC_LIVE);

        TEST_ASSERT(status_last_fault() == STATUS_ID_FAULT_OVERCURRENT);
        TEST_ASSERT(status_last_warning() == STATUS_ID_WARN_TEMP_NEAR_LIMIT);
        TEST_ASSERT(status_last_info() == STATUS_ID_INFO_AC_LIVE);

        status_init();

        TEST_ASSERT(status_last_fault() == STATUS_UNSET_ID);
        TEST_ASSERT(status_last_warning() == STATUS_UNSET_ID);
        TEST_ASSERT(status_last_info() == STATUS_UNSET_ID);

        TEST_PASS(__func__);
}

/*
 * status_snapshot with len > NUM_STATUS_BANKS must cap the copy at
 * NUM_STATUS_BANKS and not fire the error callback.
 */
static void
test_snapshot_oversized_len(void)
{
        setUp();

        uint16_t snap[NUM_STATUS_BANKS + 8u];
        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);

        status_snapshot(STATUS_CLASS_FAULT, snap, NUM_STATUS_BANKS + 8u);

        TEST_ASSERT(g_err_count == 0u);
        uint16_t expect = (uint16_t)((uint32_t)1u << (uint32_t)status_bit(
                                         STATUS_ID_FAULT_OVERCURRENT));
        TEST_ASSERT((snap[0] & expect) == expect);

        TEST_PASS(__func__);
}

/*
 * status_clear_all must NOT reset the last-set ID tracker; the audit trail is
 * preserved until status_init() is called.
 */
static void
test_clear_all_preserves_last_id(void)
{
        setUp();

        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);
        status_set_warning(STATUS_ID_WARN_TEMP_NEAR_LIMIT);
        status_set_info(STATUS_ID_INFO_AC_LIVE);

        status_clear_all(STATUS_CLASS_FAULT);
        status_clear_all(STATUS_CLASS_WARNING);
        status_clear_all(STATUS_CLASS_INFO);

        TEST_ASSERT(status_any(STATUS_CLASS_FAULT) == false);
        TEST_ASSERT(status_any(STATUS_CLASS_WARNING) == false);
        TEST_ASSERT(status_any(STATUS_CLASS_INFO) == false);

        TEST_ASSERT(status_last_fault() == STATUS_ID_FAULT_OVERCURRENT);
        TEST_ASSERT(status_last_warning() == STATUS_ID_WARN_TEMP_NEAR_LIMIT);
        TEST_ASSERT(status_last_info() == STATUS_ID_INFO_AC_LIVE);

        TEST_PASS(__func__);
}

/*
 * status_clear_fault must NOT reset last_fault_id.
 */
static void
test_clear_fault_preserves_last_id(void)
{
        setUp();

        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);
        status_clear_fault(STATUS_ID_FAULT_OVERCURRENT);

        TEST_ASSERT(status_is_fault_set(STATUS_ID_FAULT_OVERCURRENT) == false);
        TEST_ASSERT(status_last_fault() == STATUS_ID_FAULT_OVERCURRENT);

        TEST_PASS(__func__);
}

/*
 * Passing NULL to status_set_err_callback deregisters the callback; subsequent
 * errors must not reach the old handler.
 */
static void
test_null_callback_deregisters(void)
{
        setUp(); /* registers test_err_cb */

        status_set_err_callback(NULL);

        uint16_t bad_id = STATUS_ENCODE((uint16_t)NUM_STATUS_BANKS, 0u);
        status_set_fault(bad_id);

        TEST_ASSERT(g_err_count == 0u);

        TEST_PASS(__func__);
}

/*
 * last_*_id reflects the most recently set ID; re-setting an older ID
 * overwrites the tracker (most-recent-wins).
 */
static void
test_last_id_most_recent_wins(void)
{
        setUp();

        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);
        TEST_ASSERT(status_last_fault() == STATUS_ID_FAULT_OVERCURRENT);

        status_set_fault(STATUS_ID_FAULT_OVERVOLTAGE);
        TEST_ASSERT(status_last_fault() == STATUS_ID_FAULT_OVERVOLTAGE);

        status_set_fault(STATUS_ID_FAULT_OVERCURRENT);
        TEST_ASSERT(status_last_fault() == STATUS_ID_FAULT_OVERCURRENT);

        TEST_PASS(__func__);
}

/* ------------------------------------------------------------------ */
/* Main                                                                  */
/* ------------------------------------------------------------------ */

int
main(void)
{
        /* Original tests */
        test_fault_set_and_clear();
        test_warn_set_and_clear();
        test_info_set_and_clear();
        test_is_warn_set();
        test_is_fault_set();
        test_is_info_set();
        test_status_any();
        test_status_clear_all_faults();
        test_status_clear_all_warnings();
        test_status_clear_all_info();
        test_status_last_fault();
        test_status_last_warning();
        test_status_last_info();

        /* New tests */
        test_last_unset_before_first_set();
        test_err_cb_invalid_bank();
        test_err_cb_invalid_bank_all_ops();
        test_boundary_ids_valid();
        test_snapshot_captures_state();
        test_snapshot_null_dst();
        test_snapshot_zero_len();
        test_snapshot_invalid_class();
        test_snapshot_error_codes_distinct();
        test_class_isolation();
        test_any_false_after_init();
        test_invalid_class_ops();
        test_multi_bit_multi_bank();
        test_init_resets_last_ids();
        test_init_preserves_err_cb();
        test_snapshot_oversized_len();
        test_clear_all_preserves_last_id();
        test_clear_fault_preserves_last_id();
        test_null_callback_deregisters();
        test_last_id_most_recent_wins();

        fprintf(stdout, "\nAll tests passed.\n");
        return EXIT_SUCCESS;
}
