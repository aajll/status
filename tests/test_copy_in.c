/*
 * @file test_copy_in.c
 * @brief Consumer smoke test for copied-source installation.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "status.h"

#define TEST_ASSERT(expr)                                                      \
        do {                                                                   \
                if (!(expr)) {                                                 \
                        (void)fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__,   \
                                      __LINE__, #expr);                        \
                        return EXIT_FAILURE;                                   \
                }                                                              \
        } while (0)

int
main(void)
{
        const uint16_t id = STATUS_ENCODE(1u, 15u);

        status_init();
        status_set_fault(id);
        TEST_ASSERT(status_is_fault_set(id));

        return EXIT_SUCCESS;
}
