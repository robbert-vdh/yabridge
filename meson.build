project(
  'yabridge',
  'cpp',
  version : '2.2.1',
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
  '-DBOOST_ASIO_DISABLE_CONCEPTS',
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

if with_vst3
  compiler_options += '-DWITH_VST3'
endif

# Wine versions after Wine 5.6 and before 6.0 require a `__cdecl` calling
# convention to be specified on the `main()` functions or else `argc` and `argv`
# will point to the wrong memory. Similarly, with other versions of Wine this
# should _not_ be specified for the same reason. We'll try to figure out the
# current Wine version and add this calling convention based on that.
#
# https://bugs.winehq.org/show_bug.cgi?id=49138
wine_version = run_command('sh', '-c', '''wine --version | grep --only-matching -E '[0-9]+\.[0-9.]+' | head -n1''')
if wine_version.returncode() == 0 and \
   wine_version.stdout().version_compare('>=5.7') and \
   wine_version.stdout().version_compare('<6.0')
  compiler_options += '-DWINE_USE_CDECL'
endif

# Generate header files for configuration variables such as the current git tag
# and the name of the host binary
subdir('src/common/config')

vst2_plugin_sources = [
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
]

vst3_plugin_sources = [
  'src/common/communication/common.cpp',
  'src/common/logging/common.cpp',
  'src/common/logging/vst3.cpp',
  'src/common/serialization/vst3/plug-view/plug-view.cpp',
  'src/common/serialization/vst3/plug-frame/plug-frame.cpp',
  'src/common/serialization/vst3/plugin/audio-processor.cpp',
  'src/common/serialization/vst3/plugin/component.cpp',
  'src/common/serialization/vst3/plugin/connection-point.cpp',
  'src/common/serialization/vst3/plugin/edit-controller.cpp',
  'src/common/serialization/vst3/plugin/edit-controller-2.cpp',
  'src/common/serialization/vst3/plugin/plugin-base.cpp',
  'src/common/serialization/vst3/plugin/program-list-data.cpp',
  'src/common/serialization/vst3/plugin/unit-info.cpp',
  'src/common/serialization/vst3/component-handler/component-handler.cpp',
  'src/common/serialization/vst3/component-handler/unit-handler.cpp',
  'src/common/serialization/vst3/host-context/host-application.cpp',
  'src/common/serialization/vst3/attribute-list.cpp',
  'src/common/serialization/vst3/base.cpp',
  'src/common/serialization/vst3/component-handler-proxy.cpp',
  'src/common/serialization/vst3/connection-point-proxy.cpp',
  'src/common/serialization/vst3/event-list.cpp',
  'src/common/serialization/vst3/host-context-proxy.cpp',
  'src/common/serialization/vst3/message.cpp',
  'src/common/serialization/vst3/param-value-queue.cpp',
  'src/common/serialization/vst3/parameter-changes.cpp',
  'src/common/serialization/vst3/plug-frame-proxy.cpp',
  'src/common/serialization/vst3/plug-view-proxy.cpp',
  'src/common/serialization/vst3/plugin-proxy.cpp',
  'src/common/serialization/vst3/plugin-factory.cpp',
  'src/common/serialization/vst3/process-data.cpp',
  'src/common/configuration.cpp',
  'src/common/plugins.cpp',
  'src/common/utils.cpp',
  'src/plugin/bridges/vst3.cpp',
  'src/plugin/bridges/vst3-impls/plugin-factory.cpp',
  'src/plugin/bridges/vst3-impls/plug-view-proxy.cpp',
  'src/plugin/bridges/vst3-impls/plugin-proxy.cpp',
  'src/plugin/host-process.cpp',
  'src/plugin/utils.cpp',
  'src/plugin/vst3-plugin.cpp',
  version_header,
]

host_sources = [
  'src/common/communication/vst2.cpp',
  'src/common/serialization/vst2.cpp',
  'src/common/configuration.cpp',
  'src/common/logging/common.cpp',
  'src/common/logging/vst2.cpp',
  'src/common/plugins.cpp',
  'src/common/utils.cpp',
  'src/wine-host/bridges/common.cpp',
  'src/wine-host/bridges/vst2.cpp',
  'src/wine-host/editor.cpp',
  'src/wine-host/editor.cpp',
  'src/wine-host/utils.cpp',
  version_header,
]

if with_vst3
  host_sources += [
    'src/common/logging/vst3.cpp',
    'src/common/serialization/vst3/plug-view/plug-view.cpp',
    'src/common/serialization/vst3/plug-frame/plug-frame.cpp',
    'src/common/serialization/vst3/plugin/audio-processor.cpp',
    'src/common/serialization/vst3/plugin/component.cpp',
    'src/common/serialization/vst3/plugin/connection-point.cpp',
    'src/common/serialization/vst3/plugin/edit-controller.cpp',
    'src/common/serialization/vst3/plugin/edit-controller-2.cpp',
    'src/common/serialization/vst3/plugin/plugin-base.cpp',
    'src/common/serialization/vst3/plugin/program-list-data.cpp',
    'src/common/serialization/vst3/plugin/unit-info.cpp',
    'src/common/serialization/vst3/component-handler/component-handler.cpp',
    'src/common/serialization/vst3/component-handler/unit-handler.cpp',
    'src/common/serialization/vst3/host-context/host-application.cpp',
    'src/common/serialization/vst3/attribute-list.cpp',
    'src/common/serialization/vst3/base.cpp',
    'src/common/serialization/vst3/component-handler-proxy.cpp',
    'src/common/serialization/vst3/connection-point-proxy.cpp',
    'src/common/serialization/vst3/event-list.cpp',
    'src/common/serialization/vst3/host-context-proxy.cpp',
    'src/common/serialization/vst3/message.cpp',
    'src/common/serialization/vst3/param-value-queue.cpp',
    'src/common/serialization/vst3/parameter-changes.cpp',
    'src/common/serialization/vst3/plug-frame-proxy.cpp',
    'src/common/serialization/vst3/plug-view-proxy.cpp',
    'src/common/serialization/vst3/plugin-proxy.cpp',
    'src/common/serialization/vst3/plugin-factory.cpp',
    'src/common/serialization/vst3/process-data.cpp',
    'src/wine-host/bridges/vst3-impls/component-handler-proxy.cpp',
    'src/wine-host/bridges/vst3-impls/connection-point-proxy.cpp',
    'src/wine-host/bridges/vst3-impls/host-context-proxy.cpp',
    'src/wine-host/bridges/vst3-impls/plug-frame-proxy.cpp',
    'src/wine-host/bridges/vst3.cpp',
  ]
endif

individual_host_sources = host_sources + ['src/wine-host/individual-host.cpp']
group_host_sources = host_sources + [
  'src/wine-host/bridges/group.cpp',
  'src/wine-host/group-host.cpp',
]

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
wine_gdi32_dep = declare_dependency(link_args : '-lgdi32')
# The built in threads dependency does not know how to handle winegcc
wine_threads_dep = declare_dependency(link_args : '-lpthread')
xcb_dep = dependency('xcb')

wine_ole32_dep = declare_dependency(link_args : '-lole32')
# The SDK includes a comment pragma that would link to this on MSVC
wine_shell32_dep = declare_dependency(link_args : '-lshell32')
wine_uuid_dep = declare_dependency(link_args : '-luuid')

include_dir = include_directories('src/include')

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
    compile_args : vst3_compiler_options,
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
    # Some stuff from `windows.h` results in conflicting definitions
    '-DNOMINMAX',
    '-DWINE_NOWINSOCK',
  ]
  vst3_base_wine_64bit = static_library(
    'vst3_base_wine_64bit',
    vst3.get_variable('base_sources'),
    cpp_args : vst3_compiler_options + vst3_wine_compiler_options + ['-m64', '-Wno-cpp'],
    include_directories : vst3_include_dir,
    override_options : ['warning_level=0'],
    native : false,
  )
  vst3_pluginterfaces_wine_64bit = static_library(
    'vst3_pluginterfaces_wine_64bit',
    vst3.get_variable('pluginterfaces_sources'),
    cpp_args : vst3_compiler_options + vst3_wine_compiler_options + ['-m64'],
    include_directories : vst3_include_dir,
    override_options : ['warning_level=0'],
    native : false,
  )
  vst3_sdk_hosting_wine_64bit = static_library(
    'vst3_sdk_hosting_wine_64bit',
    vst3.get_variable('sdk_common_sources') + vst3.get_variable('sdk_hosting_sources'),
    link_with : [vst3_base_wine_64bit, vst3_pluginterfaces_wine_64bit],
    cpp_args : vst3_compiler_options + vst3_wine_compiler_options + ['-m64', '-Wno-multichar'],
    include_directories : vst3_include_dir,
    override_options : ['warning_level=0'],
    native : false,
  )
  vst3_sdk_hosting_wine_64bit_dep = declare_dependency(
    link_with : vst3_sdk_hosting_wine_64bit,
    include_directories : vst3_include_dir,
    # This does mean that we now have a lot of defines in our code, but the
    # alternative would be patching every location in the SDK where they include
    # `windows.h`
    compile_args : vst3_compiler_options + vst3_wine_compiler_options,
  )

  # And another time for the 32-bit version
  if with_bitbridge
    vst3_base_wine_32bit = static_library(
      'vst3_base_wine_32bit',
      vst3.get_variable('base_sources'),
      cpp_args : vst3_compiler_options + vst3_wine_compiler_options + ['-m32', '-Wno-cpp'],
      include_directories : vst3_include_dir,
      override_options : ['warning_level=0'],
      native : false,
    )
    vst3_pluginterfaces_wine_32bit = static_library(
      'vst3_pluginterfaces_wine_32bit',
      vst3.get_variable('pluginterfaces_sources'),
      cpp_args : vst3_compiler_options + vst3_wine_compiler_options + ['-m32'],
      include_directories : vst3_include_dir,
      override_options : ['warning_level=0'],
      native : false,
    )
    vst3_sdk_hosting_wine_32bit = static_library(
      'vst3_sdk_hosting_wine_32bit',
      vst3.get_variable('sdk_common_sources') + vst3.get_variable('sdk_hosting_sources'),
      link_with : [vst3_base_wine_32bit, vst3_pluginterfaces_wine_32bit],
      cpp_args : vst3_compiler_options + vst3_wine_compiler_options + ['-m32', '-Wno-multichar'],
      include_directories : vst3_include_dir,
      override_options : ['warning_level=0'],
      native : false,
    )
    vst3_sdk_hosting_wine_32bit_dep = declare_dependency(
      link_with : vst3_sdk_hosting_wine_32bit,
      include_directories : vst3_include_dir,
      # This does mean that we now have a lot of defines in our code, but the
      # alternative would be patching every location in the SDK where they include
      # `windows.h`
      compile_args : vst3_compiler_options + vst3_wine_compiler_options,
    )
  endif
endif

# The application consists of a plugin (`libyabridge-{vst2,vst3}.so`) that calls
# a Winelib application (`yabridge-{host,group}{,-32}.exe`) that can host
# Windows VST plugins. More information about the way these two components work
# together can be found in `docs/architecture.md`.

shared_library(
  'yabridge-vst2',
  vst2_plugin_sources,
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
  # This is the VST3 equivalent of `libyabridge-vst2.so`. The Wine host
  # applications can handle both VST2 and VST3 plugins.
  shared_library(
    'yabridge-vst3',
    vst3_plugin_sources,
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
endif

host_64bit_deps = [
  boost_dep,
  boost_filesystem_dep,
  bitsery_dep,
  function2_dep,
  tomlplusplus_dep,
  wine_gdi32_dep,
  wine_threads_dep,
  xcb_dep,
]
if with_vst3
  host_64bit_deps += [
    vst3_sdk_hosting_wine_64bit_dep,
    wine_ole32_dep,
    wine_shell32_dep,
    wine_uuid_dep,
  ]
endif

if with_bitbridge
  message('Bitbridge enabled, configuring a 32-bit host application')

  # I honestly have no idea what the correct way is to have `dependency()` or
  # `compiler.find_dependency()` search for 32-bit versions of libraries when
  # cross-compiling. Meson also doesn't seem to respect the default linker
  # search path set by the system in `find_library()`. If anyone does know how
  # to properly do this, please let me know!
  winegcc = meson.get_compiler('cpp', native : false)
  boost_filesystem_32bit_dep = winegcc.find_library(
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
  xcb_32bit_dep = winegcc.find_library('xcb')

  host_32bit_deps = [
    boost_dep,
    boost_filesystem_32bit_dep,
    bitsery_dep,
    function2_dep,
    tomlplusplus_dep,
    wine_gdi32_dep,
    wine_threads_dep,
    xcb_32bit_dep,
  ]
  if with_vst3
    host_32bit_deps += [
      vst3_sdk_hosting_wine_32bit_dep,
      wine_ole32_dep,
      wine_shell32_dep,
      wine_uuid_dep,
    ]
  endif
endif

executable(
  individual_host_name_64bit,
  individual_host_sources,
  native : false,
  include_directories : include_dir,
  dependencies : host_64bit_deps,
  cpp_args : compiler_options + ['-m64'],
  link_args : ['-m64'],
)

executable(
  group_host_name_64bit,
  group_host_sources,
  native : false,
  include_directories : include_dir,
  dependencies : host_64bit_deps,
  cpp_args : compiler_options + ['-m64'],
  link_args : ['-m64'],
)

if with_bitbridge
  executable(
    individual_host_name_32bit,
    individual_host_sources,
    native : false,
    include_directories : include_dir,
    dependencies : host_32bit_deps,
    cpp_args : compiler_options + ['-m32'],
    link_args : ['-m32'],
  )

  executable(
    group_host_name_32bit,
    group_host_sources,
    native : false,
    include_directories : include_dir,
    dependencies : host_32bit_deps,
    cpp_args : compiler_options + ['-m32'],
    link_args : ['-m32'],
  )
endif
