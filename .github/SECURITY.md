# Security Policy

## Reporting a Vulnerability

Use GitHub's Security Advisories feature to report security concerns privately.

Expect a response within 7 days. If the issue is confirmed, a fix will be released as a patch version.

## Scope

status is a small status register library with no network stack, no external dependencies, and no dynamic memory allocation. The primary attack surface is integer overflow in bank/bit index calculations and out-of-bounds register access.

The library validates IDs at runtime and reports invalid input through the error callback, but it cannot enforce every caller contract. Passing a bank index outside `NUM_STATUS_BANKS`, sharing a `status_reg_t` without the [concurrency contract](../docs/design.md#concurrency) the caller's target requires, or copying a register while it is active is caller misuse, not a library vulnerability.

Supported versions: fixes are released as patch versions, and the latest release line is the supported one.
