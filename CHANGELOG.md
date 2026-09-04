# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [1.4.0] - 2026-09-04

### Added

- Caller-owned `status_reg_t` registers with independent banks, trackers, and error callbacks. The existing singleton interface remains available as compatibility wrappers.
- Atomic per-bit test-and-clear operations for singleton and caller-owned registers.
- Deterministic enumeration of active IDs from caller-owned snapshots.

### Fixed

- Protected error-callback pointer access with critical-section hooks on the no-atomics backend.
- Corrected copy-in installation and compile-time configuration guidance, with a consumer smoke test.

### Changed

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
