/*
 * SPDX-License-Identifier: MIT
 *
 * @file test_copy_in.c
 * @brief Consumer smoke test for copied-source installation.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "status.h"
#include "status_version.h"

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
        TEST_ASSERT(STATUS_VERSION_NUMBER
                    == ((STATUS_VERSION_MAJOR * 10000L)
                        + (STATUS_VERSION_MINOR * 100L)
                        + STATUS_VERSION_PATCH));
        TEST_ASSERT(STATUS_VERSION_STRING[0] != '\0');

        return EXIT_SUCCESS;
}
