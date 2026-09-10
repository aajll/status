/*
 * SPDX-License-Identifier: MIT
 *
 * @file: test_header_cpp.cpp
 * @brief C++17 header smoke test: status.h must compile and link from C++.
 *
 * @details
 *    Built twice by tests/meson.build: with the default GNU __atomic backend
 *    linked against the C-compiled library, and with the no-atomics backend
 *    compiled alongside the library sources. Calling exported functions (not
 *    only static inline helpers) also proves the extern "C" guard matches the
 *    C symbols. The forced C11-atomics backend is C-only and is not exercised.
 */

#include <stdint.h>

#include "status.h"

int
main()
{
        status_reg_t reg;
        const uint16_t id = STATUS_ENCODE(0u, 0u);

        status_init();
        status_reg_init(&reg);

        status_reg_set_fault(&reg, id);
        if (!status_reg_is_fault_set(&reg, id)) {
                return 1;
        }
        if (status_reg_last_fault(&reg) != id) {
                return 1;
        }
        if (!status_reg_test_and_clear_fault(&reg, id)) {
                return 1;
        }
        status_reg_set_fault(&reg, id);
        status_reg_clear_all(&reg, STATUS_CLASS_FAULT);
        if (status_reg_any(&reg, STATUS_CLASS_FAULT)) {
                return 1;
        }

        return 0;
}
