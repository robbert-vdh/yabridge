# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic
Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Added support for plugins that send MIDI events back to the host. This plugins
  such as Cthulhu and Scaler to output notes and CC for another plugin to work
  with.
- Added automated development builds for yabridge, available by clicking on the
  'Automated builds' badge in the project readme.

### Changed

- Changed the plugin detection mechanism to support yet another way of
  symlinking plugins. Now you can use a symlink to a copy of `libyabridge.so`
  that's installed for a plugin in another directory. This is not recommended
  though. Fixes #3.
- Clarified the error that appears when we're unable to load the `.dll`.

### Fixed

- Fixed plugins failing to load on certain Debian based distros because of paths
  starting with two forward slashes.
- Redirect the output from the Wine host process earlier in the startup.
  Otherwise errors printed startup won't be visible, making it very hard to
  diagnose problems.

## [1.0.0] - 2020-05-03

### Added

- This changelog file to track keep track of changes since yabridge's 1.0
  release.
