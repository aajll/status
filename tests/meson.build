thread_dep = dependency('threads')

# Single-threaded behavioural suite on the auto-selected backend (the
# GCC/Clang __atomic path by default).
test_exe = executable(
  'test_status',
  ['test_status.c'],
  dependencies: [status_dep],
  c_args: ['-Werror'],
)
test('status module', test_exe)

# Concurrent lost-update detection across 16 threads sharing one bank word.
# This same executable, rebuilt with -Db_sanitize=thread, is the TSAN-clean
# concurrency gate (separate CI job; see ci.yml).
test_mt_exe = executable(
  'test_status_mt',
  ['test_status_mt.c'],
  dependencies: [status_dep, thread_dep],
  c_args: ['-Werror'],
)
test('status concurrency', test_mt_exe, timeout: 120)

# Forced C11 <stdatomic.h> backend. The default on GCC/Clang is the __atomic
# path, so this rebuilds the library sources with the -D for both the suite and
# the core to exercise that backend and its lock-free static assertion.
test_c11_exe = executable(
  'test_status_c11',
  ['test_status.c'] + library_sources,
  c_args: ['-DSTATUS_USE_C11_ATOMICS=1', '-Werror'],
  include_directories: public_headers,
)
test('status c11 backend', test_c11_exe)

# Forced degenerate uniprocessor backend: volatile storage with no-op critical
# hooks. Exercises the fallback read-modify-write path and the both-or-neither
# hook guard. Single-threaded only - the fallback is not safe under genuine
# preemption, so the concurrency test deliberately does not run against it.
test_no_atomics_exe = executable(
  'test_status_no_atomics',
  ['test_status.c'] + library_sources,
  c_args: ['-DSTATUS_USE_NO_ATOMICS=1', '-Werror'],
  include_directories: public_headers,
)
test('status no-atomics backend', test_no_atomics_exe)

# Forced no-atomics backend with instrumented hooks. Callback-pointer loads and
# stores must be protected, while callback invocation occurs after the exit.
test_no_atomics_hooks_exe = executable(
  'test_status_no_atomics_hooks',
  ['test_status_hooks.c'] + library_sources,
  c_args: [
    '-DSTATUS_USE_NO_ATOMICS=1',
    '-include',
    join_paths(meson.current_source_dir(), 'status_critical_hooks.h'),
    '-Werror',
  ],
  include_directories: public_headers,
)
test('status no-atomics callback hooks', test_no_atomics_hooks_exe)

# Copied-source consumer smoke test. The documented -D override must apply to
# the public header and the copied status.c implementation alike.
test_copy_in_exe = executable(
  'test_copy_in',
  ['test_copy_in.c'] + library_sources,
  c_args: ['-DNUM_STATUS_BANKS=2', '-Werror'],
  include_directories: public_headers,
)
test('status copy-in configuration', test_copy_in_exe)
