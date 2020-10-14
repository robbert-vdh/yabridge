# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic
Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Fixed a regression where the `editor_double_embed` option would cause X11
  errors and crash yabridge.

## [1.7.0] - 2020-10-13

### Changed

- The way keyboard input works has been completely rewritten to be more reliable
  in certain hosts and to provide a more integrated experience. Hovering over
  the plugin's editor while the window provided by the host is active will now
  immediately grab keyboard focus, and yabridge will return input focus to the
  host's window when moving the mouse outside of the plugin's editor when the
  window is still active. This should fix some instances where keyboard input
  was not working in hosts with more complex editor windows like _REAPER_ and
  _Ardour_, and it also allows things like the comment field in REAPER's FX
  window to still function.

  A consequence of this change is that pressing Space in Bitwig Studio 3.2 will
  now play or pause playback as intended, but this does mean that it can be
  impossible to type the space character in text boxes inside of a plugin editor
  window. Please let me know if this causes any issues for you.

- Both unrecognized and invalid options are now printed on started to make
  debugging `yabridge.toml` files easier.

- Added a note to the message stating that libSwell GUI support has been
  disabled to clarify that this is expected behaviour when using REAPER. The
  message now also contains a suggestion to enable the
  `hack_reaper_update_display` option when it is not already enabled.

### Fixed

- Added a workaround for reparenting issues with the plugin editor GUI on a
  [specific i3 setup](https://github.com/robbert-vdh/yabridge/issues/40).

### Documentation

- The documentation on `yabridge.toml` files and the available options has been
  rewritten in an effort to make it easier to comprehend.

## [1.6.1] - 2020-09-28

### Fixed

- Fixed a potential crash that could happen if the host would unload a plugin
  immediately after its initialization. This issue affected the plugin scanning
  in _REAPER_.
- Fixed parsing order of `yabridge.toml`. Sections were not always read from top
  to bottom like they should be, which could cause incorrect and unexpected
  setting overrides.
- Fixed an initialization error when using plugin groups for plugins that are
  installed outside of a Wine prefix.

### yabridgectl

- Relative paths now work when adding plugin directories or when setting the
  path to yabridge's files.
- Also search `/usr/local/lib` for `libyabridge.so` when no manual path has been
  specified. Note that manually copying yabridge's files to `/usr` is still not
  recommended.

## [1.6.0] - 2020-09-17

### Added

- Added support for double precision audio processing. This is not very widely
  used, but some plugins running under REAPER make use of this. Without this
  those plugins would cause REAPER's audio engine to crash.

### Fixed

- Increased the limit for the maximum number of audio channels. This could cause
  issues in Renoise when using a lot of output channels.

## [1.5.0] - 2020-08-21

### Added

- Added an option to work around timing issues in _REAPER_ and _Renoise_ where
  the hosts can freeze when plugins call a certain function while the host
  doesn't expect it, see
  [#29](https://github.com/robbert-vdh/yabridge/issues/29) and
  [#32](https://github.com/robbert-vdh/yabridge/issues/32). The
  [readme](https://github.com/robbert-vdh/yabridge#runtime-dependencies-and-known-issues)
  contains instructions on how to enable this.

### Changed

- Don't print calls to `effIdle()` when `YABRIDGE_DEBUG_LEVEL` is set to 1.

### Fixed

- Fix Waves plugins from freezing the plugin process by preventing them from
  causing an infinite message loop.

## [1.4.1] - 2020-07-27

### yabridgectl

- Fixed regression caused by
  [alexcrichton/toml-rs#256](https://github.com/alexcrichton/toml-rs/issues/256)
  where the configuration file failed to parse after running `yabridgectl sync`.
  If you have already run `yabridgectl sync` using yabridgectl 1.4.0, then
  you'll have to manually remove the `[last_known_config]` section from
  `~/.config/yabridgectl/config.toml`.
- Fixed issue with overwriting broken symlinks during `yabridgectl sync`.

## [1.4.0] - 2020-07-26

### Added

- Added an alternative editor hosting mode that adds yet another layer of
  embedding. Right now the only known plugins that may need this are
  _PSPaudioware_ plugins with expandable GUIs such as E27. The behaviour can be
  enabled on a per-plugin basis in the plugin configuration. See the
  [readme](https://github.com/robbert-vdh/yabridge#compatibility-options)
  for more details.

### Changed

- Both parts of yabridge will now run with realtime priority if available. This
  can significantly reduce overall latency and spikes. Wine itself will still
  run with a normal scheduling policy by default, since running wineserver with
  realtime priority can actually increase the audio processing latency although
  it does reduce the amount of latency spikes even further. You can verify that
  yabridge is running with realtime priority by looking for the `realtime:` line
  in the initialization message. I have not found any downsides to this approach
  in my testing, but please let me know if this does end up causing any issues.

### Fixed

- Fixed rare plugin location detection issue on Debian based distros related to
  the plugin and host detection fix in yabridge 1.2.0.

### yabridgectl

- Added a check to `yabridgectl sync` that verifies that the currently installed
  versions of Wine and yabridge are compatible. This check will only be repeated
  after updating either Wine or yabridge.

- Added a `--no-verify` option to `yabridgectl sync` to skip the
  post-installation setup checks. This option will skip both the login shell
  search path check for the copy-based installation method as well as the new
  Wine compatibility check.

## [1.3.0] - 2020-07-17

### Added

- By somewhat popular demand yabridge now comes with yabridgectl, a utility that
  can automatically set up and manage yabridge for you. It also performs some
  basic checks to ensure that everything has been set up correctly so you can
  get up and running faster. Yabridgectl can be downloaded separately from the
  GitHub releases page and its use is completely optional, so you don't have to
  use it if you don't want to. Check out the
  [readme](https://github.com/robbert-vdh/yabridge/tree/master/tools/yabridgectl)
  for more information on how it works.

### Deprecated

- The `use-bitbridge` and `use-winedbg` options have been deprecated in favour
  of the new `with-bitbridge` and `with-winedbg` options. The old options will
  continue to work until they are removed in yabridge 2.0.0.

## [1.2.1] - 2020-06-20

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
