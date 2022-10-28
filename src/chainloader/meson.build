# Like for the other libraries, the actual `shared_library()` call is in the
# main `meson.build` file so everything gets bundled to a single directory.

chainloader_deps = [
  configuration_dep,

  dbus_dep,
  dl_dep,
  ghc_filesystem_dep,
  rt_dep,
]

if with_clap
  clap_chainloader_deps = chainloader_deps + [clap_dep]
endif

vst2_chainloader_sources = files(
  '../common/logging/common.cpp',
  '../common/linking.cpp',
  '../common/notifications.cpp',
  '../common/process.cpp',
  'utils.cpp',
  'vst2-chainloader.cpp',
)

if with_clap
  clap_chainloader_sources = files(
    '../common/logging/common.cpp',
    '../common/linking.cpp',
    '../common/notifications.cpp',
    '../common/process.cpp',
    'utils.cpp',
    'clap-chainloader.cpp',
  )
endif

if with_vst3
  vst3_chainloader_sources = files(
    '../common/logging/common.cpp',
    '../common/linking.cpp',
    '../common/notifications.cpp',
    '../common/process.cpp',
    'utils.cpp',
    'vst3-chainloader.cpp',
  )
endif
