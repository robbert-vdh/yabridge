# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic
Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Changed the plugin detection mechanism to support yet another way of
  symlinking plugins. Now you can use a symlink to a copy of `libyabridge.so`
  that's installed for a plugin in another directory. This is not recommended
  though. Fixes #3.

### Fixed

- Redirect the output from the Wine host process earlier in the startup.
  Otherwise errors printed startup won't be visible, making it very hard to
  diagnose problems.

## [1.0.0] - 2020-05-03

### Added

- This changelog file to track keep track of changes since yabridge's 1.0
  release.
