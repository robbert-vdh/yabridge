# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic
Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Added a workaround for plugins that improperly defer part of their
  initialization process without telling the host. This fixes the Roland Cloud
  plugins.

### Fixed

- Added a workaround for the compilation issues under Wine 5.7 and above as
  caused by [Wine bug 49138](https://bugs.winehq.org/show_bug.cgi?id=49138).

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
