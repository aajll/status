# AGENTS.md

---

## 1) Project-specific instructions

**Project:** `status`
**Primary goal:** A lightweight C11 status register library for embedded systems, with atomic single-bit updates and no allocation.

### 1.1 Essential commands

#### Configure and build (library only)

```sh
meson setup build --wipe --buildtype=release -Dbuild_tests=false
meson compile -C build
```

#### Configure, build, and run unit tests

```sh
meson setup build --wipe --buildtype=debug -Dbuild_tests=true
meson compile -C build
meson test -C build --verbose
```

#### MISRA C:2023 (needs a configured build for `compile_commands.json`)

```sh
misch run
misch run --profile c11-atomics
misch run --profile no-atomics
misch deviations
```

#### Formatting (pinned clang-format through `uvx`)

```sh
scripts/format.sh          # check, as CI does
scripts/format.sh --fix    # rewrite in place
```

### 1.2 Notes

- `meson setup` generates the optional `status_version.h` into the build directory.
- The MISRA analysis needs cppcheck 2.17 or later, because older releases reject the platform file in `misra.toml`.

---

## 2) Non-negotiable design invariants

- A status ID is a `uint16_t` with the bank in the high 12 bits and the bit in the low 4. `NUM_STATUS_BANKS` stays in 1 to 4095, so `STATUS_UNSET_ID` (`0xFFFF`) can never be a valid ID.
- A single-bit set or clear is one atomic read-modify-write on the bank word. A set writes the bank and then the tracker; a clear never touches the tracker.
- The atomic backends keep their static assertions that the bank word and the callback pointer are always lock-free. Never let an access fall through to libatomic.
- On `STATUS_USE_NO_ATOMICS`, every bank and tracker access runs inside the critical-section hooks, and the hooks are defined together or not at all.
- Invalid input is non-fatal: report through the error callback and return. A `NULL` register is silent. No operation allocates, blocks, or loops over more than `NUM_STATUS_BANKS` words.

---

## 3) CI / source of truth

- CI definitions live in `.github/workflows/ci.yml`.
- Prefer running the same commands locally as CI runs (see §1.1 above).
- Install Python tools (Meson, Ninja, gcovr, misch) with `uv tool install`, never `pip`; CI does the same through `astral-sh/setup-uv`.
- The CI jobs are: tests under ASan/UBSan, ThreadSanitizer, a release build, a TI C2000 cross-build (CGT 22.6.2 and 25.11.1, COFF and EABI, warnings as errors), `clang-format`, a 100 percent line and 98 percent branch coverage gate, and `misch` with zero findings on all three backends.

---

## 4) Docs / commit conventions

### Documentation layout

- `README.md` is a short landing page (about 100 lines). Put detail in `docs/`, never in the README; link to the owning file instead.
- `docs/integration.md` owns requirements, install methods, building, and toolchain selection.
- `docs/api.md` owns the API reference. Change it together with `include/status.h`; keep signatures in fenced `c` blocks.
- `docs/design.md` is the single authoritative design record.
- `CONTRIBUTING.md` documents the contributor workflow; `CHANGELOG.md` holds release history; `.github/SECURITY.md` covers vulnerability reporting.
- Code blocks in `README.md` and `docs/` are compiled by the `docs examples` test (`tests/docs/check_snippets.py`). Each document is compiled as one source file: a `c` fence is file-level code, `<!-- snippet: body -->` wraps statements, and `<!-- snippet: skip -->` excludes pseudo-code. Do not use `skip` to silence a broken example. Declare illustrative application symbols in `tests/docs/doc_stubs.h`.
- Prose uses British spelling. Do not hard-wrap Markdown: write one line per paragraph. The 80-column limit applies to C source, not Markdown.

### Commits

- Use **Conventional Commits** format when asked to commit.
- Keep commits focused; explain *why* in the message body.

---

## 5) C style expectations

### Build & configuration

- Use the Meson build system. Do not introduce CMake, Make, or other systems.
- Update the root `meson.build` when adding or removing library source files, and `tests/meson.build` when adding or removing test files.

### Formatting

- `.clang-format` is present and **mandatory**. Run `scripts/format.sh --fix` before committing. It runs the clang-format release pinned in the script through `uvx`. Do not use a system `clang-format`: another release can format the same code differently.
- Do not reformat unrelated code.
- Key settings: 8-space indent, `BreakBeforeBraces: Linux`, column limit 80.

### Style & correctness

- Match conventions in the existing files (indentation, braces, naming).
- Validate pointer arguments at every public API boundary.
- No heap allocation (`malloc` / `free` / VLAs).
- Use `uint32_t`, `uint16_t`, `int16_t`, `bool` from `<stdint.h>` / `<stdbool.h>`; never plain `int` for fixed-width fields.

### Error handling

- Public functions return `bool` or validate via early `return`.
- No `errno`; no exceptions.

### Comment placement (Doxygen)

- Inline trailing annotations (`/**< ... */`) on `enum`/`struct` members are allowed only when the resulting line fits the 80-column limit.
- If any member's annotation would overrun, move **all** of that aggregate's member docs into a single structured Doxygen block above the type, as an `@details` list of `- ::SYMBOL  description` entries.
- Never mix inline and block forms within one aggregate, and never leave a trailing comment that clang-format would wrap onto a second line.
- After editing, run `scripts/format.sh` and an 80-column scan.

### Testing

- Run `meson test -C build` after every change.
- Add a test case for each bug fix.
- Tests live in `tests/test_*.c`; all tests must pass.
- Keep `src/` and `include/` at the coverage gate (100 percent line, 98 percent branch).

---
