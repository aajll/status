project(
  'status',
  'c',
  version: '1.2.4',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
  license: 'MIT',
)

# ── Version header ─────────────────────────────────────────────────────────────

ver = meson.project_version().split('.')

major = ver[0].to_int()
minor = ver[1].to_int()
patch = ver[2].to_int()

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set('MAJOR', major)
conf.set('MINOR', minor)
conf.set('PATCH', patch)

configure_file(
  input: 'config' / 'status_version.h.in',
  output: 'status_version.h',
  configuration: conf,
  install: true,
  install_dir: get_option('includedir') / 'status',
)

# ── Include directories ────────────────────────────────────────────────────────

public_headers = include_directories('include')

install_headers('include/status.h', 'include/status_conf.h', subdir: 'status')

# ── Application core library ───────────────────────────────────────────────────
# Hardware-agnostic application logic.  Linked by both the application
# executable and the unit-test suite (which supplies its own platform mocks).

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
