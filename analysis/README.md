# MISRA analysis assets

This directory was created by `misch init --scaffold`. Its configured paths are resolved relative to the project-root `misra.toml`.

- `rules/` explains how to supply licensed MISRA rule headlines.
- `deviations/` contains reviewed project-level cppcheck suppressions.
- `baseline/` is the configured destination for accepted finding counts.
- `mcu8_platform.xml` is the cppcheck target data model for the 8-bit MCUs the library is written for.

## Recommended workflow

1. Review `misra.toml`, especially the scope, exclusions, and compilation-database source.
2. Supply rule texts as described in `rules/README.md` when headlines and categories are needed.
3. Run `misch run`, then fix findings or document justified deviations.
4. Run `misch baseline` only after explicitly accepting the current finding counts.
5. Commit reviewed project deviations and the generated baseline so CI can enforce them.

Do not commit MISRA guideline text unless its licence and the repository's access controls permit that distribution.

## Target model

cppcheck has no generic 8-bit preset, so `mcu8_platform.xml` records the data model instead of assuming the analysis host: 8-bit `char`, 16-bit `int` and `short`, 16-bit pointers and `size_t`, 32-bit `long`, and 32-bit `double` (AVR-class). `status_conf.h`'s degenerate backend exists precisely for parts in this class, where a 16-bit bank access is not a single indivisible operation.

The XML is a single point in the 8-bit and 16-bit spaces, not every MCU the library can run on. A target with a different data model (for example, 32-bit `long` with 16-bit `int`, or signed plain `char`) may produce different findings. Model that target by adding a profile that points `platform.xml` at its own XML, the same way the backend profiles below override other defaults.

## Backend profiles

The atomic backend is chosen by a preprocessor ladder in `status_conf.h`, so the Meson build only ever compiles one branch. The default configuration and two profiles cover all three:

| Configuration | Backend | Baseline |
| --- | --- | --- |
| `misch run` | GNU `__atomic` (the GCC/Clang default) | `baseline/misra-baseline.json` |
| `misch run --profile c11-atomics` | C11 `<stdatomic.h>` | `baseline/misra-baseline.c11-atomics.json` |
| `misch run --profile no-atomics` | `volatile` + critical-section hooks | `baseline/misra-baseline.no-atomics.json` |

cppcheck does not predefine `__GNUC__`, so without `[toolchain].defines` the ladder would silently fall through to the C11 branch. The base configuration pins `STATUS_USE_GNU_ATOMICS=1`; each profile replaces that define, because exactly one backend may be selected. Each configuration keeps its own baseline since the findings differ.

## Deviations

`deviations/misra-deviations.txt` holds the reviewed project-level suppressions: 15.5 (single point of exit, house style), 8.7 (external linkage of a library API), 2.5 (macros of the unselected configuration branches), and 17.3 (cppcheck does not model `__atomic_*` or the full C11 atomic API, so it reports those calls as implicit declarations). Each entry carries its rationale and is validated by `misch deviations`. Prefer an inline `/* cppcheck-suppress misra-c2012-<rule> ; @deviation <rationale> */` for a single location; see `CONTRIBUTING.md`.

## Local commands

```sh
misch run -v                        # default (GNU __atomic) backend
misch run --profile c11-atomics -v
misch run --profile no-atomics -v
misch deviations                    # validate every suppression's rationale
misch baseline                      # re-accept findings (review the diff first)
```
