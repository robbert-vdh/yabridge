# The plugin host applications come on four flavours: hosting a single plugin
# and hosting multiple plugins within a single process, and 32-bit and 64-bit.
# We will compile the shared parts for the individual an plugin group hosts as a
# static library first to cut down on compile times.
#
# As with the libraries, we cannot call `executable()` here since we would like
# to keep all relevant files in the root of the build directory, and Meson
# doesn't have a way to customize that yet.
if is_64bit_system
  host_64bit_deps = [
    configuration_dep,

    asio_dep,
    bitsery_dep,
    function2_dep,
    ghc_filesystem_dep,
    rt_dep,
    tomlplusplus_dep,
    wine_ole32_dep,
    wine_threads_dep,
    xcb_64bit_dep,
  ]
  if with_vst3
    host_64bit_deps += [
      vst3_sdk_hosting_wine_64bit_dep,
      wine_shell32_dep,
      wine_uuid_dep,
    ]
  endif
endif

if with_bitbridge
  message('Bitbridge enabled, configuring a 32-bit host application')

  host_32bit_deps = [
    configuration_dep,

    asio_dep,
    ghc_filesystem_dep,
    bitsery_dep,
    function2_dep,
    rt_dep,
    tomlplusplus_dep,
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

host_common_sources = files(
  '../common/communication/vst2.cpp',
  '../common/serialization/vst2.cpp',
  '../common/configuration.cpp',
  '../common/logging/common.cpp',
  '../common/logging/vst2.cpp',
  '../common/audio-shm.cpp',
  '../common/plugins.cpp',
  '../common/process.cpp',
  '../common/utils.cpp',
  '../include/llvm/small-vector.cpp',
  'bridges/common.cpp',
  'bridges/vst2.cpp',
  'editor.cpp',
  'utils.cpp',
  'xdnd-proxy.cpp',
)

if with_vst3
  host_common_sources += files(
    '../common/logging/vst3.cpp',
    '../common/serialization/vst3/component-handler/component-handler.cpp',
    '../common/serialization/vst3/component-handler/component-handler-2.cpp',
    '../common/serialization/vst3/component-handler/component-handler-3.cpp',
    '../common/serialization/vst3/component-handler/component-handler-bus-activation.cpp',
    '../common/serialization/vst3/component-handler/progress.cpp',
    '../common/serialization/vst3/component-handler/unit-handler.cpp',
    '../common/serialization/vst3/component-handler/unit-handler-2.cpp',
    '../common/serialization/vst3/context-menu/context-menu.cpp',
    '../common/serialization/vst3/host-context/host-application.cpp',
    '../common/serialization/vst3/host-context/plug-interface-support.cpp',
    '../common/serialization/vst3/plug-view/parameter-finder.cpp',
    '../common/serialization/vst3/plug-view/plug-view.cpp',
    '../common/serialization/vst3/plug-view/plug-view-content-scale-support.cpp',
    '../common/serialization/vst3/plug-frame/plug-frame.cpp',
    '../common/serialization/vst3/plugin/audio-presentation-latency.cpp',
    '../common/serialization/vst3/plugin/audio-processor.cpp',
    '../common/serialization/vst3/plugin/automation-state.cpp',
    '../common/serialization/vst3/plugin/component.cpp',
    '../common/serialization/vst3/plugin/connection-point.cpp',
    '../common/serialization/vst3/plugin/edit-controller.cpp',
    '../common/serialization/vst3/plugin/edit-controller-2.cpp',
    '../common/serialization/vst3/plugin/edit-controller-host-editing.cpp',
    '../common/serialization/vst3/plugin/info-listener.cpp',
    '../common/serialization/vst3/plugin/keyswitch-controller.cpp',
    '../common/serialization/vst3/plugin/midi-learn.cpp',
    '../common/serialization/vst3/plugin/midi-mapping.cpp',
    '../common/serialization/vst3/plugin/note-expression-controller.cpp',
    '../common/serialization/vst3/plugin/note-expression-physical-ui-mapping.cpp',
    '../common/serialization/vst3/plugin/parameter-function-name.cpp',
    '../common/serialization/vst3/plugin/plugin-base.cpp',
    '../common/serialization/vst3/plugin/prefetchable-support.cpp',
    '../common/serialization/vst3/plugin/process-context-requirements.cpp',
    '../common/serialization/vst3/plugin/program-list-data.cpp',
    '../common/serialization/vst3/plugin/unit-data.cpp',
    '../common/serialization/vst3/plugin/unit-info.cpp',
    '../common/serialization/vst3/plugin/xml-representation-controller.cpp',
    '../common/serialization/vst3/plugin-factory/plugin-factory.cpp',
    '../common/serialization/vst3/attribute-list.cpp',
    '../common/serialization/vst3/base.cpp',
    '../common/serialization/vst3/bstream.cpp',
    '../common/serialization/vst3/component-handler-proxy.cpp',
    '../common/serialization/vst3/connection-point-proxy.cpp',
    '../common/serialization/vst3/context-menu-proxy.cpp',
    '../common/serialization/vst3/context-menu-target.cpp',
    '../common/serialization/vst3/event-list.cpp',
    '../common/serialization/vst3/host-context-proxy.cpp',
    '../common/serialization/vst3/message.cpp',
    '../common/serialization/vst3/param-value-queue.cpp',
    '../common/serialization/vst3/parameter-changes.cpp',
    '../common/serialization/vst3/physical-ui-map-list.cpp',
    '../common/serialization/vst3/plug-frame-proxy.cpp',
    '../common/serialization/vst3/plug-view-proxy.cpp',
    '../common/serialization/vst3/plugin-proxy.cpp',
    '../common/serialization/vst3/plugin-factory-proxy.cpp',
    '../common/serialization/vst3/process-data.cpp',
    'bridges/vst3-impls/component-handler-proxy.cpp',
    'bridges/vst3-impls/connection-point-proxy.cpp',
    'bridges/vst3-impls/context-menu-proxy.cpp',
    'bridges/vst3-impls/host-context-proxy.cpp',
    'bridges/vst3-impls/plug-frame-proxy.cpp',
    'bridges/vst3.cpp',
)
endif

# These will be linked against a static library made from `host_common_sources`
individual_host_sources = files(
  'individual-host.cpp',
)
group_host_sources = files(
  'bridges/group.cpp',
  'group-host.cpp',
)

if is_64bit_system
  host_common_64bit = static_library(
    'host_common_64bit',
    host_common_sources,
    native : false,
    include_directories : include_dir,
    dependencies : host_64bit_deps,
    cpp_args : compiler_options + wine_64bit_compiler_options,
    link_args : ['-m64'],
  )
  host_common_64bit_dep = declare_dependency(
    link_with : host_common_64bit,
    include_directories : include_dir,
    dependencies : host_64bit_deps,
    compile_args : compiler_options + wine_64bit_compiler_options,
  )
endif

if with_bitbridge
  host_common_32bit = static_library(
    'host_common_32bit',
    host_common_sources,
    native : false,
    include_directories : include_dir,
    dependencies : host_32bit_deps,
    cpp_args : compiler_options + wine_32bit_compiler_options,
    link_args : ['-m32'],
  )
  host_common_32bit_dep = declare_dependency(
    link_with : host_common_32bit,
    include_directories : include_dir,
    dependencies : host_32bit_deps,
    compile_args : compiler_options + wine_32bit_compiler_options,
  )
endif
