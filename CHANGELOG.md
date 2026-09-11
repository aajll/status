# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- MISRA C:2023 analysis with `misch` (cppcheck-backed), configured by `misra.toml`: a committed `analysis/` scaffold, an 8-bit MCU target data model, and per-backend profiles for the GNU `__atomic`, C11 and `STATUS_USE_NO_ATOMICS` branches. CI runs the audit for all three.

### Changed

- Restructured the documentation into a short README landing page plus `docs/integration.md`, `docs/api.md`, and `docs/design.md`, added a `docs examples` test that compiles every `c` block in the README and `docs/` against the real header, and stopped hard-wrapping Markdown prose at 80 columns (one line per paragraph).
- Made the internal `STATUS_ATOMIC_INIT` and `STATUS_ATOMIC_FETCH_AND` macros statement-shaped on the GNU and C11 backends, so the assignment result is no longer used as an expression (MISRA 13.4). Behaviour is unchanged; the no-atomics backend already used this form.
- Added explicit parentheses to mixed-precedence conditions and ternaries (MISRA 12.1), narrowed the intermediate snapshot indices before shifting and encoding (MISRA 10.7 / 10.8), and made the internal bit helpers take a `const` register parameter.

### Fixed

- Fixed the MISRA CI job, which used the cppcheck 2.13 shipped by Ubuntu 24.04. That release rejects the `--platform=<file>` form that `misra.toml` uses and aborts before analysing anything, so the job now installs a current cppcheck and matches a local run.

## [1.4.0] - 2026-09-04

### Added

- Documented the caller-owned `status_reg_*` interface with per-function contracts and added it, with `status_snapshot_next()`, to the README API reference.
- Caller-owned `status_reg_t` registers with independent banks, trackers, and error callbacks. The existing singleton interface remains available as compatibility wrappers.
- Atomic per-bit test-and-clear operations for singleton and caller-owned registers.
- Deterministic enumeration of active IDs from caller-owned snapshots.

### Fixed

- Corrected stale README claims: the lock-free and interrupt- and core-safe guarantees hold on the GNU and C11 backends only, ISR signalling uses the critical-section hooks on the no-atomics backend only, and a `status_reg_t` is caller-allocated rather than static.
- Corrected the `STATUS_ENCODE` diagram: the 12-bit bank field can represent 0–4095, but a usable bank must be below `NUM_STATUS_BANKS`, so 4095 is never usable.
- Rejected `NUM_STATUS_BANKS` outside the range 1–4095 in `status_conf.h`, so an out-of-range configuration fails at compile time in every translation unit instead of only in the library build.
- Matched the `status_reg_*` definition parameter names to the public declarations.
- Corrected the atomicity contracts: setters update banks and trackers separately, and tracker reads during overlapping setters can still return an earlier ID or `STATUS_UNSET_ID`. Documented backend protection requirements and exclusive access during caller-owned initialisation.
- Protected error-callback pointer access with critical-section hooks on the no-atomics backend.
- Wrapped every no-atomics bank and tracker load and store in the critical-section hooks, so a 16-bit access cannot tear or lose an update on 8-bit targets.
- Corrected the active and latched fault composition example: acknowledgement now serialises with producers instead of racing them, and the producer visibility window is documented.
- Corrected copy-in installation and compile-time configuration guidance, with a consumer smoke test.
- Made the public header compile as C++17 by routing the lock-free checks through a shared assertion macro; documented that the forced C11-atomics backend is C-only.

### Changed

- Made `.github/workflows/ci.yml` the single CI workflow by removing the duplicate `test.yml`.
- Aligned `CONTRIBUTING.md` and `AGENTS.md` with the commands and files they describe: coverage passes `-fprofile-update=atomic`, ThreadSanitizer is listed as a required check, the source-list pointer is the root `meson.build`, and the concurrency test comment matches its eight workers.
- Documented active and latched fault composition using independent registers.
- Added development guidance for visible invalid-input handling without changing nonfatal production defaults.

## [1.3.0] - 2026-06-26

### Changed

- Reworked the concurrency model: single-bit set/clear is now a lock-free atomic read-modify-write via an auto-discovered backend (`__atomic` → C11 `<stdatomic.h>` → uniprocessor fallback), mirroring `seqlock`. The mandatory `STATUS_ENTER_CRITICAL` / `STATUS_EXIT_CRITICAL` hooks and their unconditional `#warning` are gone; the hooks now apply only to the `STATUS_USE_NO_ATOMICS` fallback and default to silent no-ops. The atomic backends statically assert lock-free bank and callback-pointer storage so unsupported targets fail loudly. Backend selection and `NUM_STATUS_BANKS` moved to a new `status_conf.h`.
- Documented MISRA C:2023 / IEC 61508 awareness in the public header, matching the primitive family baseline.
- Hardened the public API contracts: every function now documents its parameters, return values, invalid-input behaviour, the error callback's execution context (may be an ISR) and reentrancy, and the relaxed memory-ordering limitation.
- Standardised CI, security policy, contributor guidance, SPDX headers, coverage thresholds, ignore rules, README badge, and copyright metadata with the primitive family baseline.

### Added

- Concurrent lost-update test (`test_status_mt.c`) that races 16 distinct bits of a shared bank and fails if any atomic read-modify-write is lost; verified to fail against a non-atomic build and to be ThreadSanitizer-clean against the atomic backends.
- Test suite now exercises all three backends (default, forced C11, forced no-atomics) and CI gains a dedicated ThreadSanitizer job.

## [1.2.4] - 2026-03-12

### Added

- Added pkg-config metadata generation for Meson consumers.
- Banked C11 status-register API for faults, warnings, and info bits, using compact encoded status IDs, critical-section hooks, snapshots, callbacks, and fixed-size storage.
