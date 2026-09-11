# Contributing to status

status is a small C11 status register library for embedded systems. It tracks fault, warning, and info bits using compact banked status IDs and is designed for deterministic firmware and diagnostic code.

## Getting started

The same commands CI runs, locally:

```sh
# Configure with tests + sanitisers (CI default)
meson setup build --buildtype=debug -Dbuild_tests=true \
                  -Db_sanitize=address,undefined
meson compile -C build
meson test -C build --verbose

# Coverage (CI gate is 80% line + 70% branch). -fprofile-update=atomic keeps
# gcov's counter increments atomic; without it the multi-threaded tests race on
# them and gcov aborts the report with negative branch counts (gcc bug 68080).
meson setup build_cov --buildtype=debug -Dbuild_tests=true \
                      -Db_coverage=true \
                      -Dc_args=-fprofile-update=atomic
meson compile -C build_cov && meson test -C build_cov
gcovr --root . --filter 'src/' --filter 'include/' --print-summary
```

## Source style

- `.clang-format` is mandatory. Run `clang-format -i` on every modified `.c` / `.h` file before submitting.
- 8-space indent, Linux brace style, 80-column limit. Match the existing conventions; do not reformat unrelated code.
- The Meson build system is the single source of truth. Update `meson.build` / `tests/meson.build` when adding or removing source files.
- No CMake, no Make, no other build systems.

## C language rules

- C11 only.
- Use fixed-width types from `<stdint.h>` and `<stdbool.h>`.
- No heap allocation (`malloc`, `free`, VLAs).
- Validate pointer arguments and encoded status IDs at every public-API boundary.
- Preserve the critical-section hook contract for interrupt-safe embedded use.

## MISRA C:2023

The library is analysed with `misch` (cppcheck-backed MISRA C:2023 analysis),
configured by `misra.toml`. The audit is clean: `misch run` must report zero
findings for the default configuration and for both backend profiles:

```sh
misch run
misch run --profile c11-atomics
misch run --profile no-atomics
```

The default configuration models the GNU `__atomic` backend that GCC and Clang
builds compile. The profiles force the C11 `<stdatomic.h>` and
`STATUS_USE_NO_ATOMICS` branches, which the Meson build never selects on its
own. The analysis target is the 8-bit MCU data model in
`analysis/mcu8_platform.xml`; `analysis/README.md` explains both.

If your change introduces a new finding:

1. Prefer restructuring the code so the rule is satisfied. Deviate only when
   compliance would make the code genuinely worse, and never restructure purely
   to hide a violation from the checker; an honest deviation beats evasion.
2. Suppress at point of use with
   `/* cppcheck-suppress misra-c2012-<rule> ; @deviation <rationale> */`, or add
   a justified project-wide entry to `analysis/deviations/misra-deviations.txt`
   for house-style rules. Use `:file` on a project-wide entry to keep its scope
   as narrow as practical.
3. Verify with the three `misch run` commands and `misch deviations` (every
   suppression must carry a rationale), and justify the deviation in the PR
   description.

`required` and `mandatory` rule deviations face a higher bar than `advisory`.
The project carries four project-wide rules across five entries, each with its
rationale in `analysis/deviations/misra-deviations.txt`: 15.5 and 8.7 (advisory
house style), 2.5 (macros of the unselected configuration branches), and 17.3
(mandatory, but a cppcheck false positive on compiler atomic builtins, not a
code defect). Do not add a deviation without explaining why the code cannot
comply.

The baseline files under `analysis/baseline/` are empty: the audit reports zero
findings, so the gate is zero findings rather than a ratchet. Regenerate a
baseline only after a deliberate review. Never commit licensed MISRA rule text;
`analysis/rules/README.md` explains how to supply it locally or in CI.

## Tests and coverage

- Add a test for every bug fix.
- Add a test for every new feature.
- CI enforces an 80% line / 70% branch coverage gate.
- Tests live in `tests/test_*.c`.

## Commits

Use Conventional Commits:

- `feat: ...` new feature
- `fix: ...` bug fix
- `doc: ...` documentation only
- `test: ...` test-only changes
- `chore: ...` build, CI, release work
- `refactor: ...` code change that neither fixes a bug nor adds a feature

Keep the subject under ~70 characters. Use the body to explain _why_ the change is needed, not _what_ the diff already shows.

## Pull requests

- Open an issue first for non-trivial changes so the design can be agreed before implementation.
- Keep PRs focused. One feature or one fix per PR.
- All CI checks must pass: tests on Linux + macOS, ASan + UBSan, ThreadSanitizer, release build, coverage gate, and the MISRA audit.

## When in doubt

Open an issue and discuss before writing code. The library is small enough that even modest design changes have outsized implications.
