# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic
Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- When building from source, only statically link Boost when the
  `with-static-boost` option is enabled.
- The `use-bitbridge` and `use-winedbg` options have been replaced by
  `with-bitbridge` and `with-winedbg` for consistency's sake. The old options
  will be marked as deprecated in the next minor release.

### Fixed

- Fixed memory error that would cause crashing on playback with some buffer
  sizes in Mixbus6.
- Opening a plugin would override the Wine prefix for all subsequent plugins
  opened from within the same process. This prevented the use of multiple Wine
  prefixes in hosts that do not sandbox their plugins, such as Ardour.
- Manual Wine prefix overides through the `WINEPREFIX` environment were not
  reflected in the output shown on startup.
- Fixed plugin group socket name generation. This would have prevented plugin
  groups with the same name from being used simultaneously in different Wine
  prefixes.
- Distinguish between active processes and zombies when checking whether a group
  host process is still running during initialization.

## [1.2.0] - 2020-05-29

### Added

- Added the ability to host multiple plugins within a single Wine process
  through _plugin groups_. A plugin group is a user-defined set of plugins that
  will be hosted together in the same Wine process. This allows multiple
  instances of plugins to share data and communicate with each other. Examples
  of plugins that can benefit from this are FabFilter Pro-Q 3, MMultiAnalyzer,
  and the iZotope mixing plugins. See the readme for instructions on how to set
  this up.

### Changed

- Changed architecture to use one fewer socket.
- GUI events are now always handled on a steady timer rather than being
  interleaved as part of the event loop. This change was made to unify the event
  handling logic for individually hosted plugins and plugin groups. It should
  not have any noticeable effects, but please let me know if this does cause
  unwanted behavior.

### Fixed

- Steal keyboard focus when clicking on the plugin editor window to account for
  the new keyboard focus behavior in _Bitwig Studio 3.2_.
- Fixed large amount of empty lines in the log file when the Wine process closes
  unexpectedly.
- Made the plugin and host detection slightly more robust.

## [1.1.4] - 2020-05-12

### Fixed

- Fixed a static linking issue with the 32-bit build for Ubuntu 18.04.

## [1.1.3] - 2020-05-12

### Fixed

- Added a workaround for the compilation issues under Wine 5.7 and above as
  caused by [Wine bug #49138](https://bugs.winehq.org/show_bug.cgi?id=49138).
- Added a workaround for plugins that improperly defer part of their
  initialization process without telling the host. This fixes startup behavior
  for the Roland Cloud plugins.
- Added a workaround for a rare race condition in certain plugins caused by
  incorrect assumptions in plugin's editor handling. Fixes the editor for
  Superior Drummer 3 and the Roland Cloud synths in Bitwig Studio.
- Fixed potential issue with plugins not returning their editor size.

## [1.1.2] - 2020-05-09

### Fixed

- Fixed an issue where plugin removal could cause Ardour and Mixbus to crash.

## [1.1.1] - 2020-05-09

### Changed

- Changed installation recommendations to only install using symlinks with hosts
  that support individually sandboxed plugins.
- Respect `YABRIDGE_DEBUG_FILE` when printing initialization errors.

### Fixed

- Stop waiting for the Wine VST host process on startup if the process has
  crashed or if Wine was not able to start.

## [1.1.0] - 2020-05-07

### Added

- Added support for plugins that send MIDI events back to the host. This allows
  plugins such as Cthulhu and Scaler to output notes and CC for another plugin
  to work with.
- Added support for querying and setting detailed information about speaker
  configurations for use in advanced surround setups. This indirectly allows
  yabridge to work under _Renoise_.
- Added automated development builds for yabridge, available by clicking on the
  'Automated builds' badge in the project readme.

### Changed

- Changed the plugin detection mechanism to support yet another way of
  symlinking plugins. Now you can use a symlink to a copy of `libyabridge.so`
  that's installed for a plugin in another directory. This is not recommended
  though.
- Changed Wine prefix detection to be relative to the plugin's `.dll` file,
  rather than the loaded `.so` file.
- Increased the maximum number of audio channels from 32 to 256.
- Clarified the error that appears when we're unable to load the `.dll`.
- Yabridge will now print the used version of Wine during startup. This can be
  useful for diagnosing startup problems.

### Fixed

- Fixed plugins failing to load on certain versions of _Ubuntu_ because of
  paths starting with two forward slashes.
- Redirect the output from the Wine host process earlier in the startup process.
  Otherwise errors printed during startup won't be visible, making it very hard
  to diagnose problems.

## [1.0.0] - 2020-05-03

### Added

- This changelog file to track keep track of changes since yabridge's 1.0
  release.
