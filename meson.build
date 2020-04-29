project(
  'yabridge',
  'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++17', 'build.cpp_std=c++17']
)

# Meson does not let us set a default cross compiler, which makes sense, but it
# also means that it's easy to forget. This will cause the setup process to
# abort if no cross compiler has been set up.
winelib_check = '''#ifndef __WINE__
#error 1
#endif'''
if not meson.get_compiler('cpp').compiles(winelib_check)
  error('You need to set up a cross compiler, check the README for compilation instructions.')
endif

# Depending on the `use-bitbridge` flag we'll enable building a second 32-bit
# host application that can act as a bit bridge for using 32-bit Windows plugins
# in 64-bit Linux VST hsots. The plugin will determine which host application to
# use based on the `.dll` file it's trying to load.
# This setup is necessary until Meson provides a way to have multiple
# cross-builds for a single build directory:
# https://github.com/mesonbuild/meson/issues/5125
host_name_64bit = 'yabridge-host'
host_name_32bit = 'yabridge-host-32'

# This provides an easy way to start the Wine VST host using winedbg since it
# can be quite a pain to set up
compiler_options = []
if get_option('use-winedbg')
  compiler_options += '-DUSE_WINEDBG'
endif

# Generate header files for configuration variables such as the current git tag
# and the name of the host binary
subdir('src/common/config')

boost_dep = dependency('boost', modules : ['filesystem'])
bitsery_dep = subproject('bitsery').get_variable('bitsery_dep')
threads_dep = dependency('threads')
# The built in threads dependency does not know how to handle winegcc
wine_threads_dep = declare_dependency(link_args : '-lpthread')
xcb_dep = dependency('xcb')

include_dir = include_directories('src/include')

# The application consists of a VST plugin (yabridge) that calls a winelib
# program (yabridge-host) that can host Windows VST plugins. More information
# about the way these two components work together can be found in the readme
# file.

shared_library(
  'yabridge',
  [
    'src/common/logging.cpp',
    'src/common/serialization.cpp',
    'src/plugin/host-bridge.cpp',
    'src/plugin/plugin.cpp',
    version_header,
  ],
  native : true,
  include_directories : include_dir,
  dependencies : [boost_dep, bitsery_dep, threads_dep],
  cpp_args : compiler_options,
  link_args : ['-ldl']
)

host_sources = [
  'src/common/logging.cpp',
  'src/common/serialization.cpp',
  'src/wine-host/editor.cpp',
  'src/wine-host/editor.cpp',
  'src/wine-host/plugin-bridge.cpp',
  'src/wine-host/vst-host.cpp',
  'src/wine-host/utils.cpp',
  version_header,
]

executable(
  host_name_64bit,
  host_sources,
  native : false,
  include_directories : include_dir,
  dependencies : [boost_dep, bitsery_dep, wine_threads_dep, xcb_dep],
  cpp_args : compiler_options + ['-m64'],
  link_args : ['-m64']
)

if get_option('use-bitbridge')
  compiler_options += '-DUSE_BITBRIDGE'

  # TODO: Use 32 bit libraries
  executable(
    host_name_32bit,
    host_sources,
    native : false,
    include_directories : include_dir,
    dependencies : [boost_dep, bitsery_dep, wine_threads_dep, xcb_dep],
    cpp_args : compiler_options + ['-m32'],
    link_args : ['-m32']
  )
endif
