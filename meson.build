project(
  'yabridge',
  'cpp',
  version : '2.1.0',
  default_options : ['warning_level=3', 'cpp_std=c++2a', 'build.cpp_std=c++2a'],
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

# Depending on the `with-bitbridge` flag we'll enable building secondary 32-bit
# host applications that can act as a bit bridge for using 32-bit Windows
# plugins in 64-bit Linux VST hosts. The plugin will determine which host
# application to use based on the `.dll` file it's trying to load. This setup is
# necessary until Meson provides a way to have multiple cross-builds for a
# single build directory: https://github.com/mesonbuild/meson/issues/5125
individual_host_name_64bit = 'yabridge-host'
individual_host_name_32bit = 'yabridge-host-32'
group_host_name_64bit = 'yabridge-group'
group_host_name_32bit = 'yabridge-group-32'

compiler_options = [
  '-fvisibility=hidden',
  '-fvisibility-inlines-hidden',
  # Disable the use of concepts in Boost.Asio until Boost 1.73 gets released
  # https://github.com/boostorg/asio/issues/312
  '-DBOOST_ASIO_DISABLE_CONCEPTS'
]

with_bitbridge = get_option('with-bitbridge')
with_static_boost = get_option('with-static-boost')
with_winedbg = get_option('with-winedbg')
with_vst3 = get_option('with-vst3')

if with_bitbridge
  compiler_options += '-DWITH_BITBRIDGE'
endif

# This provides an easy way to start the Wine VST host using winedbg since it
# can be quite a pain to set up
if with_winedbg
  compiler_options += '-DWITH_WINEDBG'
endif

# Generate header files for configuration variables such as the current git tag
# and the name of the host binary
subdir('src/common/config')

# Statically link against Boost.Filesystem, otherwise it would become impossible
# to distribute a prebuilt version of yabridge
boost_dep = dependency('boost', version : '>=1.66', static : with_static_boost)
boost_filesystem_dep = dependency(
  'boost',
  version : '>=1.66',
  modules : ['filesystem'],
  static : with_static_boost,
)
bitsery_dep = subproject('bitsery', version : '5.2.0').get_variable('bitsery_dep')
function2_dep = subproject('function2', version : '4.1.0').get_variable('function2_dep')
threads_dep = dependency('threads')
tomlplusplus_dep = subproject('tomlplusplus', version : '2.1.0').get_variable('tomlplusplus_dep')
# The built in threads dependency does not know how to handle winegcc
wine_threads_dep = declare_dependency(link_args : '-lpthread')
xcb_dep = dependency('xcb')

include_dir = include_directories('src/include')

# The application consists of a plugin (`libyabridge-{vst2,vst3}.so`) that calls
# a Winelib application (`yabridge-{host,group}{,-32}.exe`) that can host
# Windows VST plugins. More information about the way these two components work
# together can be found in `docs/architecture.md`.

shared_library(
  'yabridge-vst2',
  [
    'src/common/communication/common.cpp',
    'src/common/communication/vst2.cpp',
    'src/common/serialization/vst2.cpp',
    'src/common/configuration.cpp',
    'src/common/logging/common.cpp',
    'src/common/logging/vst2.cpp',
    'src/common/plugins.cpp',
    'src/common/utils.cpp',
    'src/plugin/bridges/vst2.cpp',
    'src/plugin/host-process.cpp',
    'src/plugin/utils.cpp',
    'src/plugin/vst2-plugin.cpp',
    version_header,
  ],
  native : true,
  include_directories : include_dir,
  dependencies : [
    boost_dep,
    boost_filesystem_dep,
    bitsery_dep,
    threads_dep,
    tomlplusplus_dep,
  ],
  cpp_args : compiler_options,
  link_args : ['-ldl']
)

if with_vst3
  # Meson does not allow mixing native and non native dependencies from
  # subprojects. The only workaround is to only define the necessary variables
  # there, and to then assemble the dependencies here ourselves.
  vst3 = subproject('vst3', version : '3.7.1')
  vst3_compiler_options = vst3.get_variable('compiler_options')
  vst3_include_dir = vst3.get_variable('include_dir')

  # We'll create a dependency for the plugin SDK for our native VST3 plugin
  vst3_base_native = static_library(
    'base_native',
    vst3.get_variable('base_sources'),
    cpp_args : vst3_compiler_options + ['-Wno-cpp'],
    include_directories : vst3_include_dir,
    override_options : ['warning_level=0'],
    native : true,
  )
  vst3_pluginterfaces_native = static_library(
    'pluginterfaces_native',
    vst3.get_variable('pluginterfaces_sources'),
    cpp_args : vst3_compiler_options,
    include_directories : vst3_include_dir,
    override_options : ['warning_level=0'],
    native : true,
  )
  vst3_sdk_native = static_library(
    'sdk_native',
    vst3.get_variable('sdk_common_sources') + vst3.get_variable('sdk_sources'),
    link_with : [vst3_base_native, vst3_pluginterfaces_native],
    cpp_args : vst3_compiler_options + ['-Wno-multichar'],
    include_directories : vst3_include_dir,
    override_options : ['warning_level=0'],
    native : true,
  )
  vst3_sdk_native_dep = declare_dependency(
    link_with : vst3_sdk_native,
    include_directories : vst3_include_dir,
  )

  # And another dependency for the host SDK for our Wine host applications
  # We need to do some minor hacking to get this to compile with winegcc. Most
  # notably some attributes are named differently, and the SDK uses 'Windows.h'
  # instead of 'windows.h' like how the file is actually called.
  # message(vst3_include_dir)
  vst3_sdk_base_dir = vst3.get_variable('sdk_base_dir')
  patch_result = run_command('tools/patch-vst3-sdk.sh', vst3_sdk_base_dir)
  if patch_result.returncode() == 0
    message(patch_result.stdout())
  else
    error('Error while trying to patch the VST3 SDK:\n' + patch_result.stderr())
  endif

  vst3_wine_compiler_options = [
    # Removes some MSVC-isms for us
    '-D__MINGW32__',
    # We don't need all of this stuff from `Windows.h`, and it only causes more
    # issues
    '-DNOMINMAX',
    '-DNOSERVICE',
    '-DNOMCX',
    '-DWIN32_LEAN_AND_MEAN',
  ]
  vst3_base_wine = static_library(
    'base_wine',
    vst3.get_variable('base_sources'),
    cpp_args : vst3_compiler_options + vst3_wine_compiler_options + [ '-Wno-cpp'],
    include_directories : vst3_include_dir,
    override_options : ['warning_level=0'],
    native : false,
  )
  vst3_pluginterfaces_wine = static_library(
    'pluginterfaces_wine',
    vst3.get_variable('pluginterfaces_sources'),
    cpp_args : vst3_compiler_options + vst3_wine_compiler_options,
    include_directories : vst3_include_dir,
    override_options : ['warning_level=0'],
    native : false,
  )
  vst3_sdk_hosting_wine = static_library(
    'sdk_hosting_wine',
    vst3.get_variable('sdk_common_sources') + vst3.get_variable('sdk_hosting_sources'),
    link_with : [vst3_base_wine, vst3_pluginterfaces_wine],
    cpp_args : vst3_compiler_options + vst3_wine_compiler_options + ['-Wno-multichar'],
    include_directories : vst3_include_dir,
    override_options : ['warning_level=0'],
    native : false,
  )
  vst3_sdk_hosting_wine_dep = declare_dependency(
    link_with : vst3_sdk_hosting_wine,
    include_directories : vst3_include_dir,
  )

  # This is the VST3 equivalent of `libyabridge-vst2.so`. The Wine host
  # applications can handle both VST2 and VST3 plugins.
  shared_library(
    'yabridge-vst3',
    [
      'src/common/logging/common.cpp',
      'src/common/logging/vst2.cpp',
      'src/plugin/vst3-plugin.cpp',
      version_header,
    ],
    native : true,
    include_directories : include_dir,
    dependencies : [
      boost_dep,
      boost_filesystem_dep,
      bitsery_dep,
      threads_dep,
      tomlplusplus_dep,
      vst3_sdk_native_dep,
    ],
    cpp_args : compiler_options,
    link_args : ['-ldl'],
  )
else
  message('VST3 support has been disabled')
endif

host_sources = [
  'src/common/communication/vst2.cpp',
  'src/common/serialization/vst2.cpp',
  'src/common/configuration.cpp',
  'src/common/logging/common.cpp',
  'src/common/logging/vst2.cpp',
  'src/common/plugins.cpp',
  'src/common/utils.cpp',
  'src/wine-host/bridges/vst2.cpp',
  'src/wine-host/editor.cpp',
  'src/wine-host/editor.cpp',
  'src/wine-host/utils.cpp',
  version_header,
]

individual_host_sources = host_sources + ['src/wine-host/individual-host.cpp']
group_host_sources = host_sources + [
  'src/wine-host/bridges/group.cpp',
  'src/wine-host/group-host.cpp',
]

executable(
  individual_host_name_64bit,
  individual_host_sources,
  native : false,
  include_directories : include_dir,
  dependencies : [
    boost_dep,
    boost_filesystem_dep,
    bitsery_dep,
    function2_dep,
    tomlplusplus_dep,
    wine_threads_dep,
    xcb_dep,
  ],
  cpp_args : compiler_options + ['-m64'],
  link_args : ['-m64'],
)

executable(
  group_host_name_64bit,
  group_host_sources,
  native : false,
  include_directories : include_dir,
  dependencies : [
    boost_dep,
    boost_filesystem_dep,
    bitsery_dep,
    function2_dep,
    tomlplusplus_dep,
    wine_threads_dep,
    xcb_dep,
  ],
  cpp_args : compiler_options + ['-m64'],
  link_args : ['-m64'],
)

if with_bitbridge
  message('Bitbridge enabled, configuring a 32-bit host application')

  # I honestly have no idea what the correct way is to have `dependency()` or
  # `compiler.find_dependency()` search for 32-bit versions of libraries when
  # cross-compiling. Meson also doesn't seem to respect the default linker
  # search path set by the system in `find_library()`. If anyone does know how
  # to properly do this, please let me know!
  winegcc = meson.get_compiler('cpp', native : false)
  boost_filesystem_dep = winegcc.find_library(
    'boost_filesystem',
    static : with_static_boost,
    dirs : [
      # Used by Arch based distros
      '/usr/local/lib32',
      '/usr/lib32',
      # Used by Debian based distros
      '/usr/local/lib/i386-linux-gnu',
      '/usr/lib/i386-linux-gnu',
      # Used by Red Hat based distros, could cause issues though since Meson
      # cannot differentiate between the 32-bit version and the regular 64-bit
      # version that would normally be in /lib
      '/usr/local/lib',
      '/usr/lib',
    ]
  )
  xcb_dep = winegcc.find_library('xcb')

  executable(
    individual_host_name_32bit,
    individual_host_sources,
    native : false,
    include_directories : include_dir,
    dependencies : [
      boost_dep,
      boost_filesystem_dep,
      bitsery_dep,
      function2_dep,
      tomlplusplus_dep,
      wine_threads_dep,
      xcb_dep,
    ],
    cpp_args : compiler_options + ['-m32'],
    link_args : ['-m32'],
  )

  executable(
    group_host_name_32bit,
    group_host_sources,
    native : false,
    include_directories : include_dir,
    dependencies : [
      boost_dep,
      boost_filesystem_dep,
      bitsery_dep,
      function2_dep,
      tomlplusplus_dep,
      wine_threads_dep,
      xcb_dep,
    ],
    cpp_args : compiler_options + ['-m32'],
    link_args : ['-m32'],
  )
endif
