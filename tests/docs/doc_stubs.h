/*
 * @file doc_stubs.h
 * @brief Declarations-only stubs for the documentation examples.
 *
 * The examples in README.md and the guides under docs/ call application code
 * that the library does not provide. This header declares just enough of it
 * for those examples to type-check in the "docs examples" test. Nothing here
 * is defined or linked; it exists so a signature drift in the documentation
 * fails the build. See CONTRIBUTING.md.
 */

#ifndef STATUS_DOC_STUBS_H
#define STATUS_DOC_STUBS_H

#include <stdint.h>

#include "status.h"

/* Error handler registered by the caller-owned-register recipe. */
void my_error_handler(status_err_t err, uint16_t id);

#endif /* STATUS_DOC_STUBS_H */
