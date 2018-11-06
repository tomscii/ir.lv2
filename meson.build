project(
  'IR', 'c', 'cpp',
  meson_version: '>= 0.47.0',
  license: 'GPL-2.0-or-later',
  version: '1.3.5',
)

# Versioning
package_name = meson.project_name()
package_version = meson.project_version()

# Paths
prefix = get_option('prefix')
libdir = join_paths(prefix, get_option('libdir'))
lv2dir = join_paths(libdir, 'lv2')

# Dependencies
cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

glib_req = '>= 2.28.0'
gtk_req = '>= 2.24.32'
lv2_req = '>= 1.12.0'
samplerate_req = '>= 0.1.9'
sndfile_req = '>= 1.0.28'

glib_dep = dependency('glib-2.0', version: glib_req)
gtk_dep = dependency('gtk+-2.0', version: gtk_req)
lv2_dep = dependency('lv2', version: lv2_req)
samplerate_dep = dependency('samplerate', version: samplerate_req)
sndfile_dep = dependency('sndfile', version: sndfile_req)

zita_dep = cxx.find_library('zita-convolver')

# Configuration
bundle_name = get_option('bundle')
build_utils = get_option('utils')

add_project_arguments([
  '-Wno-deprecated-declarations',
  '-Wno-parentheses',
], language: 'cpp')

if get_option('buildtype') == 'release'
  add_project_arguments([
    '-ffast-math',
  ], language: 'cpp')
endif

# Build
ir_sources = files([
  'ir.cc',
  'ir_utils.cc',
])

ir_deps = [
  glib_dep,
  gtk_dep,
  lv2_dep,
  samplerate_dep,
  sndfile_dep,
  zita_dep,
]

ir_sha = shared_library(
  'ir', ir_sources,
  dependencies: ir_deps,
  name_prefix: '',
  install: true,
  install_dir: join_paths(
    lv2dir,
    bundle_name,
  )
)

ir_gui_sources = files([
  'ir_gui.cc',
  'ir_meter.cc',
  'ir_modeind.cc',
  'ir_utils.cc',
  'ir_wavedisplay.cc',
])

ir_gui_deps = [
  glib_dep,
  gtk_dep,
]

ir_gui_sha = shared_library(
  'ir_gui', ir_gui_sources,
  dependencies: ir_gui_deps,
  name_prefix: '',
  install: true,
  install_dir: join_paths(
    lv2dir,
    bundle_name,
  )
)

if build_utils == true
  convert4chan_exe = executable(
    'convert4chan', 'convert4chan.c',
    dependencies: [glib_dep, sndfile_dep],
    install: false
  )
endif

# Data
ir_data = [
  'ir.ttl',
  'manifest.ttl',
]

install_data(
  ir_data,
  install_dir: join_paths(
    lv2dir,
    bundle_name,
  )
)

# Message
summary = [
  '',
  '------',
  '@0@.lv2 @1@'.format(package_name, package_version),
  '',
  '  Bundle: @0@'.format(bundle_name),
  '   Utils: @0@'.format(build_utils),
  '',
  'Directories:',
  '  prefix: @0@'.format(prefix),
  '  libdir: @0@'.format(libdir),
  '  lv2dir: @0@'.format(lv2dir),
  '------',
]

message('\n'.join(summary))
