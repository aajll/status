# Security Policy

## Reporting a Vulnerability

Use GitHub's Security Advisories feature to report security concerns
privately.

Expect a response within 7 days. If the issue is confirmed, a fix
will be released as a patch version.

## Scope

status is a small status register library with no network stack, no
external dependencies, and no dynamic memory allocation. The primary
attack surface is integer overflow in bank/bit index calculations and
out-of-bounds register access.
