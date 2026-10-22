project(
  'status',
  'c',
  version: '1.4.0',
  meson_version: '>=0.57.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
  license: 'MIT',
)

# ── Version ────────────────────────────────────────────────────────────────────
# include/status.h defines the version macros, so a copy-in build has them.
# Configuration fails when they differ from the project version.

fs = import('fs')
version_parts = meson.project_version().split('.')
version_macros = {
  'STATUS_VERSION_MAJOR': version_parts[0],
  'STATUS_VERSION_MINOR': version_parts[1],
  'STATUS_VERSION_PATCH': version_parts[2],
  'STATUS_VERSION': '"' + meson.project_version() + '"',
}
header_lines = []
foreach line : fs.read('include' / 'status.h').split('\n')
  header_lines += ' '.join(line.split())
endforeach
foreach name, value : version_macros
  if '#define @0@ @1@'.format(name, value) not in header_lines
    error('include/status.h must define @0@ as @1@'.format(name, value))
  endif
endforeach

# ── Include directories ────────────────────────────────────────────────────────

public_headers = include_directories('include')

install_headers(
  'include/status.h',
  'include/status_conf.h',
  subdir: 'status',
)

# ── Core library ───────────────────────────────────────────────────────────────
# Hardware-agnostic status logic, shared by the unit-test suite and by Meson
# consumers that link the declared dependency.

library_sources = files('src/status.c')

status_lib = static_library(
  'status',
  library_sources,
  include_directories: public_headers,
  install: true,
)

status_dep = declare_dependency(
  include_directories: public_headers,
  link_with: status_lib,
)
meson.override_dependency('status', status_dep)

# ── pkg-config ─────────────────────────────────────────────────────────────────

pkgconfig = import('pkgconfig')
pkgconfig.generate(
  status_lib,
  name: 'status',
  description: 'Lightweight C11 banked-bitfield status register library for embedded systems',
  subdirs: 'status',
)

# ── Host unit tests ────────────────────────────────────────────────────────────

if get_option('build_tests')
  subdir('tests')
endif
