# AGENTS.md

---

## 1) Project-specific instructions

**Project:** `status` **Primary goal:** A lightweight C11 status register library for embedded systems.

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

#### Notes

- `meson setup` generates the optional `status_version.h` into the **build directory**

---

## 2) CI / source of truth

- CI definitions live in `.github/workflows/ci.yml`.
- Prefer running the same commands locally as CI runs (see §1.1 above).
- If `pre-commit` is configured, run `pre-commit run --all-files` before committing.

---

## 3) Docs / commit conventions

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

## 4) C style expectations

### Build & configuration

- Use the Meson build system. Do not introduce CMake, Make, or other systems.
- Update the root `meson.build` when adding or removing library source files, and `tests/meson.build` when adding or removing test files.

### Formatting

- `.clang-format` is present and **mandatory**. Run `clang-format -i` on all modified `.c` / `.h` files before committing.
- Do not reformat unrelated code.
- Key settings: 8-space indent, `BreakBeforeBraces: Linux`, column limit 80.

### Style & correctness

- Match conventions in the existing files (indentation, braces, naming).
- Validate pointer arguments at every public API boundary.
- No heap allocation (`malloc` / `free` / VLAs).
- Use `uint32_t`, `uint16_t`, `int16_t`, `bool` from `<stdint.h>` / `<stdbool.h>` — never plain `int` for fixed-width fields.

### Error handling

- Public functions return `bool` or validate via early `return`.
- No `errno`; no exceptions.

### Comment placement (Doxygen)

- Inline trailing annotations (`/**< ... */`) on `enum`/`struct` members are allowed only when the resulting line fits the 80-column limit.
- If any member's annotation would overrun, move **all** of that aggregate's member docs into a single structured Doxygen block above the type, as an `@details` list of `- ::SYMBOL  description` entries.
- Never mix inline and block forms within one aggregate, and never leave a trailing comment that clang-format would wrap onto a second line.
- After editing, verify with a `clang-format --style=file` no-reformat diff and an 80-column scan.

### Testing

- Run `meson test -C build` after every change.
- Add a test case for each bug fix.
- Tests live in `tests/test_*.c`; all tests must pass.

---
