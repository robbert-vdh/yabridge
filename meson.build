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
  error('You need to set up a cross compiler, check the README for more information.')
endif

# The application consists of a VST plugin (yabridge) that calls a winelib
# program (yabridge-host) that can host Windows VST plugins. More information
# about the way these two components work together can be found in the readme
# file.

boost_dep = dependency('boost', modules : ['filesystem'])
msgpack_dep = dependency('msgpack')

include_dir = include_directories('src/include')

shared_library(
  'yabridge',
  [
    'src/plugin/bridge.cpp',
    'src/plugin/plugin.cpp',
  ],
  native : true,
  include_directories : include_dir,
  dependencies : [boost_dep, msgpack_dep],
  link_args : ['-ldl']
)

executable(
  'yabridge-host',
  [
    'src/wine-host/vst-host.cpp',
  ],
  native : false,
  include_directories : include_dir,
  dependencies : [],
  link_args : []
)
