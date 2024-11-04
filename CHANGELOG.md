# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic
Versioning](https://semver.org/spec/v2.0.0.html).

## [5.1.1] - 2024-11-04

### Fixed

- Fixed DPI scaling causing windows contents to become larger than they should
  be when using **Wine 9.17+** and Wine's font DPI scaling feature.
- Fixed a potential segfault when unloading yabridge.

## [5.1.0] - 2023-12-23

### Added

- The yabridge libraries now export a `yabridge_version` function that hosts can
  query to know that a plugin is a yabridge plugin, and which version of
  yabridge is in use. **Ardour** 8.2 will use this to fix a regression in Ardour
  7.3 that could cause some VST3 plugins to freeze.

### Changed

- Parsing failures for `yabridge.toml` files will no longer cause plugins to
  fail to load. If this does happen, you'll now get a desktop notification and
  the plugin will simply use the default settings instead.

### Fixed

- Fixed freezes in new versions of **Renoise** when it tries to set DPI scaling
  for VST2 plugins.

### yabridgectl

- Yabridgectl's command line interface looks slightly differently again after
  some dependency updates. The behavior remains the same.
- Some outdated warning messages have been updated to match yabridge's current
  state. There are also some additional warnings for common installation issues.

### Packaging notes

- This release includes a workaround to make bitsery compile with GCC 13 due to
  changes in transitive header includes.
- The CLAP dependency has been updated to target version 1.1.9 (revision version
  update).
- The asio dependency has been updated to target version 1.28.2.
- The bitsery dependency has been updated to version 5.2.3 (revision version
  bump).
- The function2 dependency has been updated to version 4.2.3 (revision version
  bump).
- The `ghc::filesystem` dependency has been updated to version 1.5.14 (revision
  version bump).
- The tomlplusplus dependency has been updated to target version 3.4.0.

## [5.0.5] - 2023-05-07

### Changed

- Parameter information for VST3 and CLAP plugins is now queried all at once.
  This should work around a bug in VST3 _Kontakt_ that would cause loading
  patches with lots of exposed parameters to become very slow in **REAPER**
  ([#236](https://github.com/robbert-vdh/yabridge/issues/236)).
- When dragging plugin windows around, yabridge now waits for the mouse buttons
  to be released before informing Wine about the window's new screen
  coordinates. This prevents constant flickering when dragging plugin windows
  around with some plugin and window manager combinations.
- Since the above change limits the number of times a plugin GUI potentially has
  to redraw when dragging the window around to once, the workaround added to
  yabridge 5.0.2 for _Audio Nebula Aurora FM_ implementing drawing in a very
  suboptimal way has be reverted. This removes flickering when resizing for a
  lot of plugin GUIs again.
- Yabridge now preemptively unsets the `WAYLAND_DISPLAY` environment variable
  when launching Wine. Upstream Wine currently does not yet have a Wayland
  driver, but future versions may. When that happens yabridge's X11 window
  embedding may suddenly start breaking spectacularly. This change makes sure
  that Wine will keep using X11 even if Wayland support becomes available at
  some point.

### Fixed

- Fixed a race condition that could occur when a CLAP plugin instance would
  request a host callback while the host simultaneously tried to create another
  instance of the same plugin. This would result in a deadlock. An example of a
  plugin that triggered this is _PolyChrome DSP's McRocklin Suite_.
- Mutually recursive callbacks are now enabled for more CLAP lifetime function
  calls. This was another change needed to avoid a deadlock in _PolyChrome DSP's
  McRocklin Suite_, as it changes its latency while being initialized.
- Negative indices were not treated as invalid arguments in some of the VST3
  interface implementations and could cause crashes if a plugin tried to query a
  parameter value with signed index -1. This has now been fixed. The issue only
  appeared with the VST3 validator, and not with any regular hosts or DAWs.

### yabridgectl

- VST 3.7.5 `moduleinfo.json` files without a `Compatibility` field are now
  supported. Previously this would result in a parsing error because the whole
  point of the `moduleinfo.json` files is to provide `Compatibility` mappings
  for older VST2 plugins.

## [5.0.4] - 2023-02-23

### Fixed

- Fixed a regression from yabridge 4.0.0 where plugin groups would not exit
  correctly. When removing a plugin instance that was part of a plugin group, it
  would block until the group host process had exited. This in turn resulted in
  hangs if the group host process hosted more than one plugin instance.
- Configuring the Meson build now works correctly on Wine 8.0 final. Meson's
  version comparison function considers `8.0` to be a lower version than
  `8.0rc2`.
- The tomlplusplus dependency in the Meson build new avoids linking against
  tomlplusplus' shared libraries. These were recently introduced, and depending
  on the build environment Meson may still try to link them despite tomlplusplus
  being used in headers only mode. This is to keep yabridge's plugin libraries
  free of dependencies outside of the C and C++ standard libraries, avoiding
  potential symbol clashes.

### Packaging notes

- The CLAP dependency has been updated to target version 1.1.7.
- The tomlplusplus dependency has been updated to target version 3.3.0.

## [5.0.3] - 2022-12-23

### Changed

- The yabridge 5.0.3 binaries up on the GitHub releases page (and in the
  [`yabridge-bin`](https://aur.archlinux.org/packages/yabridge-bin) AUR package)
  now work again with every Wine version after Wine 5.7, including 7.21, 7.22,
  and the 8.0 release candidates. All workarounds for Wine 7.21 and 7.22 have
  been reverted. See Wine bug
  [#53912](https://bugs.winehq.org/show_bug.cgi?id=53912) for more information.

### Packaging notes

- The VST3 dependency has been updated to target version 3.7.7 with tag
  `v3.7.7_build_19-patched`.
- The CLAP dependency has been updated to target version 1.1.4.
- The `patch-vst3-sdk.sh` script now applies a handwritten diff to the SDK
  instead of patching the SDK using sed. This makes it easier to use older (but
  still API-compatible) VST3 SDK versions with yabridge and it makes the
  patching less brittle. The patches can be found in `tools/vst3-sdk-patches`.
- Since the workarounds from yabridge 5.0.1 and 5.0.2 have been reverted, the
  Meson build will now throw an error when trying to build against Wine 7.21,
  7.22, or 8.0-rc1. Yabridge binaries built against these Wine versions will not
  work correctly.
- Yabridge built against Wine 8.0-rc2 will also work with older Wine versions,
  including the aforementioned ones that previously required workarounds.
  Yabridge built against older Wine versions will not work with Wine 8.0-rc2 or
  later.
- Unity builds can safely be re-enabled again.

## [5.0.2] - 2022-11-28

# Changed

- The yabridge builds on the GitHub releases page now have the unity build
  option disabled. This _may_ work around the same Wine bug
  [#53912](https://bugs.winehq.org/show_bug.cgi?id=53912) mentioned in the last
  release, as the bug has not yet been fixed for Wine 7.22. Since this is a low
  level bug within Wine, there's no guarantee that everything will work
  correctly until the bug gets fixed. If you still experience crashes or freezes
  with yabridge, then do consider
  [downgrading](https://github.com/robbert-vdh/yabridge#downgrading-wine) back
  to Wine Staging 7.20.
- Yabridge's build system now errors out when enabling unity builds while
  compiling with Wine 7.21 and 7.22.

# Fixed

- Changed the behavior when setting window positions for yabridge's editor. This
  avoids a painfully slow redraw in the _Audio Nebula Aurora FM_ plugin when
  dragging the editor window around. The change may also help with other slow to
  redraw GUI, and especially with window managers that send excessive events on
  window movement like in Cinnamon and XFCE.

### Packaging notes

- The `--unity=on` build option should be removed for the time being as this
  together with the VST3 SDK triggers the above mentioned [Wine
  bug](https://bugs.winehq.org/show_bug.cgi?id=53912). Make sure to run
  `yabridge-host.exe` (just that, with no `wine` in front of it) at least once
  with Wine Staging 7.21 or 7.22 after building to make sure the build works
  correctly. It should print a usage message if it does.

## [5.0.1] - 2022-11-14

# Fixed

- Added a temporary workaround for yabridge hanging indefinitely on startup as
  the result of a new bug in Wine 7.21:
  https://bugs.winehq.org/show_bug.cgi?id=53912

# yabridgectl

- Fixed converted VST 3.7.5 `moduleinfo.json` files being considered orphan
  files immediately after yabridgectl created them as part of the sync
  operation.

## [5.0.0] - 2022-11-02

# Added

- Yabridge 5.0 now supports bridging [CLAP](https://cleveraudio.org/) plugins in
  addition to its existing VST2 and VST3 plugin support. CLAP is a
  [collaborative
  effort](https://github.com/free-audio/clap/blob/main/Contributors.md) by a
  group of plugin and host developers of all backgrounds to create a
  permissively licensed extensible plugin standard that is simple while also
  catering to the needs of plugin developers, host developers, and musicians
  alike. When bridged under yabridge, these plugins are likely to have lower
  bridging overhead than their VST2 and VST3 counterparts while also being more
  responsive and offering better support for instrument plugins and parameter
  modulation.

  Yabridge 5.0.0's CLAP bridging supports [all official CLAP 1.1
  extensions](https://github.com/robbert-vdh/yabridge/blob/master/src/common/serialization/clap/README.md)
  except for the audio thread pool extension. Support for that extension will be
  added in a future yabridge release as Windows-only plugins that rely on the
  feature get released.

- Desktop notifications no longer rely on the `notify-send` command line tool,
  and are now sent by directly talking to D-Bus instead. This ensures that
  you'll always see yabridge's notifications when something important happens,
  even when using more niche distros where you may not have `notify-send`
  installed by default.
- A new `editor_disable_host_scaling` `yabridge.toml` [compatibility
  option](https://github.com/robbert-vdh/yabridge#compatibility-options) lets
  you prevent hosts from setting an explicit DPI scaling factor for a plugin's
  editor. In some cases this can help with inconsistent scaling when using HiDPI
  displays. This option affects both **VST3** and **CLAP** plugins and it
  replaces the older `vst3_no_scaling` option.

# Removed

- The `vst3_no_scaling` compatibility option has been removed in favor of the
  new `editor_disable_host_scaling` option.

# Changed

- Slightly optimized the use of serialization buffers to reduce memory usage for
  VST3 audio threads. This change also potentially speeds up parameter
  information queries for parameters with lots of associated text.

### Fixed

- Fixed a minor memory leak in the Wine->X11 drag-and-drop implementation when
  converting Windows file paths.
- Removed leftover debug prints when opening VST2 editors.

### yabridgectl

- Added support for setting up CLAP plugins.

### Packaging notes

- There are new `libyabridge-clap.so` and `libyabridge-chainloader-clap.so`
  files need to be included in the package.
- The new CLAP support requires version 1.1.2 of the CLAP headers because
  earlier versions did not yet contain calling conventions.
  (<https://github.com/free-audio/clap/issues/153>,
  <https://github.com/free-audio/clap/pull/154>). Building against older
  versions will result in memory errors.
- The VST3 dependency is now at tag `v3.7.5_build_44-patched-2`. The only
  difference with the previous `v3.7.5_build_44-patched` is a fixed version
  number in the `meson.build` file.
- The Meson build now requires the `libdbus-1` development package to be
  installed. Yabridge's binaries don't dynamically link against the shared
  library, but they do use the definitions from the headers to load
  `libdbus-1.so.3` at runtime when it needs to send a desktop notification.

## [4.0.2] - 2022-06-27

### Fixed

- Fixed a rare edge case where a Windows VST3 plugin would incorrectly be
  classified as a bundle-style plugin, which caused loading those plugins to
  fail. This could happen if the directory `foo` contained some random
  directory, containing another directory, containing `foo.vst3`. Yabridge
  always assumed this to be a bundle, even if it was not.
- Fixed Full Bucket's _Ragnarök_ causing some hosts to freeze when changing
  presets due to some mutually recursive function calls that weren't being
  handled as such.

### yabridgectl

- Parsing errors for plugin binaries are now non-fatal. This could happen if
  your Windows plugin directories contain text files with a `.dll` or `.vst3`
  file extension. This would normally never happen, but it can still happen if
  you extracted those Windows plugins from a .zip file that was created on
  macOS. Don't ask me how or why.
- Prematurely abort the `yabridgectl sync` process if `~/.vst/yabridge` or
  `~/.vst3/yabridge` are symlinks to a directory that's part of or contains one
  of yabridgectl's plugin search directories. This prevents an edge cases where
  VST2 plugin .dll files could be replaced by symlinks to themeselves.
- Don't trigger a panic on `yabridgectl sync` if someone `yabridgectl add`'ed
  the inner contents of a Windows VST3 bundle. For the record, you really,
  really, _really_ shouldn't be doing this.

## [4.0.1] - 2022-06-12

### Added

- Added a `system-asio` build option to aid distro packaging.

### Fixed

- Fixed recent _Arturia_ VST3 plugins running into memory errors at the end of a
  plugin scan in **REAPER** and **Ardour**. These plugins would try to read data
  in the Windows message loop without checking whether that data was
  initialized, after the data had just been deinitialized.

### yabridgectl

- Fixed a regression from yabridge 4.0.0 where VST3 plugins in the 'new' Windows
  VST3 bundle format, like _Sforzando_, were not set up correctly.

### Packaging notes

- The new `system-asio` build option forces Asio to be used from the standard
  include directories. Otherwise the dependency is defined as a regular Meson
  dependency. Asio does not have any pkgconfig or CMake [build
  definitions](https://github.com/chriskohlhoff/asio/issues/1071), so it's
  impossible to detect its presence and version in a standard way. Because of
  that the Meson build will always fall back to using the included wrap
  dependency. Configuring the project with
  `meson setup build -Dsystem-asio=true ...` forces `<asio.hpp>` to be used
  instead.
- The `ghc_filesystem` dependency now explicitly mentions the
  `ghcFilesystem::ghc_filesystem` CMake module. `ghc::filesystem`'s naming is
  [inconsistent](https://github.com/gulrak/filesystem/pull/129) so Meson can't
  detect the correct module automatically. It also doesn't expose a
  [version](https://github.com/gulrak/filesystem/issues/148), so even with this
  change version 1.5.12 of the upstream dependency still won't be detected
  correctly. There is a [PR](https://github.com/gulrak/filesystem/pull/149) that
  fixes this.

## [4.0.0] - 2022-06-09

### Added

- Yabridge 4.0 completely revamps the way plugin loading works to allow yabridge
  to be updated without breaking existing yabridge'd plugins while saving disk
  space on filesystems that don't support reflinks and speeding up the
  `yabridgectl sync` process. Up until this point, `yabridgectl sync` has always
  made copies of yabridge's `libyabridge-vst2.so` or `libyabridge-vst3.so`
  plugin libraries for every Windows plugin it sets up. These plugins are
  tightly coupled to the yabridge plugin host binaries, and updating one but not
  the other can thus cause issues. With yabridge 4.0, yabridgectl no longer
  copies the entire plugin libraries. Instead, it now creates copies of these
  new tiny dependencyless shim libraries. When loaded by a plugin host, these
  libraries locate the actual plugin libraries on the system and then
  transparently forward all entry point function calls to them as if the host
  was loading the yabridge plugin library directly. Yabridge internally calls
  these libraries chainloaders due to their similarity to the identically named
  functionality in boot loading process. This allows yabridge to be updated
  independently of these copied chainloading libraries. As such, it is no longer
  possible for yabridge to be out of sync after an update. If you use a distro
  packaged version of yabridge, then that means yabridge can now be updated
  safely without requiring any action from your side.
- Added support for the `effBeginLoadBank` and `effBeginLoadProgram` VST2
  opcodes for loading state as a program or a program bank.

### Changed

- Almost the entirety of yabridge's backend has been rewritten to get rid of all
  dependencies on the Boost libraries. As a consequence, the runtime dependency
  on `Boost.Filesystem` has also been removed. This makes packaging yabridge for
  distros easier, and it makes the packages more reliable by removing the need
  for yabridge to be rebuilt whenever Boost gets updated. Additionally, it also
  makes compiling slightly faster and the binaries are slightly smaller.
- The functionality for the `yabridge-group` binaries has been merged into the
  `yabridge-host` binaries to reduce duplication.
- When the user does not have the permissions to lock the shared audio buffers
  into memory, yabridge will now retry mapping the memory without locking it
  instead of immediately terminating the process. An annoying desktop
  notification will still be shown every time you load a plugin until you fix
  this however.
- Yabridge now prints the path to the `libyabridge-{vst2,vst3}.so` library
  that's being used on startup. This tells you where the chainloader is loading
  the library file from. Because you can never have too much information, right?
- The `with-bitbridge`, `with-vst3`, and `with-winedbg` build options have been
  renamed to `bitbridge`, `vst3`, and `winedbg`.
- `effProcessEvents` VST2 calls are now filtered out from the log when
  `YABRIDGE_DEBUG_LEVEL` is set to 1.

### Removed

- Removed the `with-static-boost` build option since there's no longer a
  dependency on Boost.Filesystem.
- Removed the `yabridge-group` binaries as they are now part of the
  `yabridge-host` binaries. This saves precious megabytes.

### Fixed

- Fixed manually changing channel counts with supported VST3 plugins in
  **REAPER** not working.
- Fixed an obscure issue with VST3 plugins crashing in **Ardour** on
  Arch/Manjaro because of Ardour's misreported parameter queue lengths.
- Fixed yabridge throwing assertion failures on serialization when using some of
  the _Orchestral Tools_ Kontakt libraries in the VST2 version of Kontakt. Some
  of those libraries would output more than 2048 MIDI events in a single buffer.
- Some of yabridge's socket file names contained extremely aesthetically
  unpleasing trailing underscores. Begone pesky underscores!
- Fixed building with VST3 support disabled.

### yabridgectl

- VST2 plugins are now set up in `~/.vst/yabridge` by default. This means that
  you no longer have to add any directory search locations in your DAW. The only
  potential downside is that it's no longer possible for two plugin directories
  (perhaps in different Wine prefixes) to provide the same plugin file, although
  you would not have been able to use both with the same DAW anyways. Like with
  yabridgectl's VST3 support, the subdirectory structure within the plugin
  directory is preserved. You can use `yabridgectl set --vst2-location=inline`
  to revert back to the old behavior of setting the plugins up right next to the
  VST2 plugin `.dll` files. Some migration notes:

  - Because the plugins are now set up in `~/.vst/yabridge` by default instead
    of next to the Windows VST2 plugin .dll file, you will see notices about
    leftover `.so` files the first time you run `yabridgectl sync` after
    updating. Double check the list to make sure there are no files in there
    that shouldn't be removed, and then run `yabridgectl sync --prune` as
    instructed to remove the old `.so` files.
  - Make sure your DAW searches for VST2 plugins in `~/.vst`.
  - You can and should remove any entries for VST2 plugin directories you added
    to your DAW's plugin search locations as they will no longer contain any
    relevant files.
  - If you were using a `yabridge.toml` configuration file to configure VST2
    plugins, then you will now need to move that file in `~/.vst/yabridge`
    instead.

- As mentioned above, yabridgectl now uses the new chainloading libraries when
  setting up plugins. This means that once you've ran `yabridgectl sync` after
  updating to yabridge 4.0, yabridge can now be updated without needing to rerun
  `yabridgectl sync`. This is particularly useful when using a distro packaged
  version of yabridge.
- Added support for the new VST 3.7.5 `moduleinfo.json` format to allow VST3
  plugins to replace VST2 and VST3 plugins with different class IDs.
- Yabridgectl no longer depends on **winedump**. It now parses Windows PE32(+)
  binaries without requiring any external dependencies. Or at least, that's the
  idea. I've come across at least one binary that this new parser can't handle
  (https://github.com/m4b/goblin/issues/307), so it will still fall back to
  winedump when that happens.
- After `yabridgectl sync` has finished setting up plugins, yabridgectl now also
  checks whether `notify-send` is installed as part of its post-installation
  verification process. If `notify-send` is missing, then yabridge won't be able
  to send any notifications when things are going terribly wrong.
- `yabridgectl status` now shows the locations where bridged VST2 and VST3
  plugins will be set up.
- `yabridgectl sync --prune` now also considers broken symlinks.
- The VST3 subdirectory detection has been made more robust and can now handle
  arbitrary plugin directories, not just directories that are called `VST3`.
  This, of course, should not be needed.
- The previously deprecated symlink installation method has been removed from
  yabridgectl, along with the `yabridgectl set --method` option. The terminology
  in `yabridgectl status` has changed accordingly.
- `yabridgectl status` now lists the architecture of
  `libyabridge-chainloader-vst2.so` just like it already did for the VST3
  library.

### Packaging notes

- `libyabridge-chainloader-vst2.so` and `libyabridge-chainloader-vst3.so` are
  new files that should be included in the package.
- The `yabridge-group` binaries no longer exist as they are now part of the
  `yabridge-host` binaries.
- The `with-bitbridge` build option has been renamed to just `bitbridge`.
- Both runtime and compile time dependencies on the Boost libraries have been
  removed.
- There's a new dependency on the headers-only
  [`ghc::filesystem`](https://github.com/gulrak/filesystem) library to replace
  Boost.Filesystem. A Meson wrap is included as a fallback for a distro package.
- The headers-only [Asio](http://think-async.com/Asio/) library now replaces
  Boost.Asio. A Meson wrap is included as a fallback for a distro package.
- Fixed a deprecation warning in the Meson build, causing the minimum supported
  Meson version to be bumped up to **Meson 0.56** from 0.55.
- Yabridge now targets VST3 SDK version 3.7.5 with git tag `v3.7.5_build_44-patched`.

## [3.8.1] - 2022-03-08

### Changed

- Change the low `RLIMIT_RTTIME` warning to mention setting up realtime
  priviliges instead of changing PipeWire's config now that PipeWire MRs
  [!1118](https://gitlab.freedesktop.org/pipewire/pipewire/-/merge_requests/1118)
  and
  [!1120](https://gitlab.freedesktop.org/pipewire/pipewire/-/merge_requests/1120)
  have been merged and PipeWire can use regular realtime scheduling without
  imposing any resource limits out of the box.
- Prevented yabridge's ad-hoc socket acceptors from inheriting realtime
  scheduling when spawned from audio threads. In practice this should not have
  caused any noticeable effects as these threads are sleeping all the time
  except for under very specific circumstances.

### Fixed

- Fixed the **REAPER**-specific `editor_force_dnd` option not working correctly
  when using the `Track -> Insert virtual instrument on new track...` option.
  When using this option REAPER will first embed the plugin in an offscreen
  plugin window and it will only then create the actual FX window and embed the
  other window in it.
- Fixed the VST3 version of _IK Multimedia's T-RackS 5_ producing silent output
  when doing offline rendering. This could happen when exporting or bouncing
  audio in **Bitwig Studio 4.1+**, **Ardour** and in **REAPER**. These plugins
  apparently need to process audio from the main GUI thread when in offline
  rendering mode. If you try to process audio from the...audio thread, then they
  will produce silence and hang afterwards (which a fix in yabridge 3.7.0
  previously addressed).
- Fixed crashes when opening plugin editors under **Crostini** on ChromeOS due
  to non-standard X11 implementations.
- Worked around a bug in the _RandARP_ VST2 plugin where the plugin would report
  that its editor window is 0 by 0 pixels.
- Fixed building under Wine 7.2 and up because of changes to the definitions of
  Wine's numerical types.

### yabridgectl

- `yabridgectl status` no longer mentions anything about installation methods if
  you're using the regular, copy-based installation method. This is a follow-up
  to the changes made in yabridgectl 3.8.0.

## [3.8.0] - 2022-01-15

### Added

- Added support for VST3 plugins interacting directly with the host's context
  menu items. Most plugins that use VST3's context menu support let the host
  handle drawing the actual menu, but it's also possible for plugins to
  incorporate the host's menu items into their own custom context menu. So far
  this feature has only been tested with [Surge
  XT](https://github.com/surge-synthesizer/surge)'s Windows VST3 version since
  very few if any other plugins do this right now, but other plugins may start
  doing this as well in the future.

### Changed

- Added support for Wine 6.23's new fixed winedbg command line argument
  handling.
- Changed the build and cross-compilation definitions to allow
  repository-packaged CMake build configurations to be used for the bitsery and
  function2 dependencies.

### Fixed

- Fixed _Waves_ V13 VST3 plugins crashing when opening the GUI. These plugins
  thought it would be a great idea to randomly dereference null pointers if the
  window they're embedded in is already visible. A day's worth of debugging well
  spent. Even after this, the V13 plugins are a bit unstable under Wine in
  general, and they will likely crash when reopening the editor a couple of
  times or when removing them. So as always, if you can avoid Waves, that would
  be for the best.
- Fixed sluggish UIs in _Output's Thermal_ and likely a handful of other
  JUCE-based plugins with a lot of parameters. These plugins would emit hundreds
  to thousands of events when the GUI changes. Yabridge now detects this, and
  relaxes the throttling we have in place to prevent certain other plugins from
  getting stuck in infinite loops.
- Fixed _DrumCore 3_ crashing when trying to drag grooves from the plugin to
  other applications. This happened because of an integer underflow in that
  plugin, causing the number of reported drag-and-drop formats to be magnitudes
  higher than yabridge's indicated maximum.
- Fixed Wine version detection in the build configuration.
- Fixed VST3 connection point proxies not being disconnected properly. The code
  path for this is not being used for any of the current Linux VST3 hosts, so
  this won't have caused any issues.
- Rewritten the VST3 object handling to prevent some theoretical data races when
  the host inserts or removes plug instances while other instances of that
  plugin are processing audio.

### yabridgectl

- Yabridgectl's help text received some shiny new colors.
- Disallowed adding individual files or symlinks to individual files with
  `yabridgectl add`. Yabridgectl was never intended to be used that way and
  while it does sort of work, it will lead to a number of surprises down the
  line.
- Deprecated support for the symlink-based installation method in yabridgectl
  and removed all remaining mentions of it from the documentation. This feature
  has for all intents and purposes already been made obselete in yabridge 2.1.0,
  but the option still remained available. Enabling this option would lead to a
  lot of surprises because of the way Linux's dynamic linker works. And with
  modern file systems supporting reflinks and yabridge falling back to searching
  for binaries in `~/.local/share/yabridge`, there's zero reason to use this
  feature anymore. Yabridgectl will now print a warning upon syncing when the
  symlink installation method has been enabled, and the feature will be removed
  completely in yabridge 4.0.
- Blacklisted symlinks and symlinked directories are now handled correctly when
  syncing.

### Packaging notes

- The tomlplusplus wrap dependency has been updated to version 3.0.1 because of
  breaking API changes in version 3.0.
- We now target VST3 SDK version 3.7.4 with git tag `v3.7.4_build_25-patched`.
- Yabridgectl now uses Rust 2021 and requires rustc 1.56 or newer to build.

## [3.7.0] - 2021-11-21

### Added

- Added an environment variable for changing the directory yabridge stores its
  sockets and other temporary files in. This is only useful when running the
  Wine process under a separate namespace. If you don't know what this means,
  then you probably don't need this!

### Changed

- Added a workaround for a new
  [bug](https://bugs.winehq.org/show_bug.cgi?id=51919) in Wine 6.20 that would
  cause compilation to fail by redefining common variable names used in the
  standard library. This issue has since been fixed in Wine 6.21 and up.

### Fixed

- Fixed the VST3 version of _IK Multimedia's T-RackS 5_ causing offline
  rendering to stall indefinitely. This could happen when exporting or bouncing
  audio in **Bitwig Studio 4.1**, **Ardour** and in **REAPER**. Those plugins
  deadlock when they receives timer events while doing offline audio processing,
  so we now prevent that from happening.
- The socket endpoints used by plugin group host processes to accept new
  connections now get removed when those processes shut down. Previously this
  would leave behind a file in the temporary directory.

### Packaging notes

- All Meson wraps now use `wrap-git` instead of downloading tarballs from
  GitHub. Previously the bitsery and function2 wraps would use source tarballs.
- The `meson.build` patch overlays for the bitsery and function2 wraps are no
  longer stored in tarballs committed to yabridge's repository. Instead, they
  are now regular directories in the `subprojects/packagefiles` directory. This
  means that building yabridge with these wraps now requires **Meson 0.55** or
  later because of the use of `patch_directory`.
- The bitsery wrap dependency was updated to version 5.2.2.
- The function2 wrap dependency was updated to version 4.2.0.
- The tomlplusplus wrap dependency was updated to slightly after version 2.5.0
  because of an [issue](https://github.com/marzer/tomlplusplus/issues/121) with
  their `meson.build` file that breaks compatibility with Meson 0.60.0 on older
  versions.

## [3.6.0] - 2021-10-15

### Added

- Yabridge will now also show annoying desktop notifications when encountering
  low `RLIMIT_RTTIME` and `RLIMIT_MEMLOCK` values. This can happen on systems
  that have not yet been configured for pro audio work or with using an out of
  the box PipeWire configuration. If these issues are not fixed, then certain
  plugins may crash during initialization. Since these configuration issues may
  not immediately cause any obvious problems, it's better to be upfront about it
  so they can't cause mysterious issues later on. We would already print
  warnings about this to the terminal, but those are easily missed when starting
  a DAW from the GUI.
- Added a new `editor_coordinate_hack` [compatibility
  option](https://github.com/robbert-vdh/yabridge#compatibility-options) to
  replace `editor_double_embed`. This can be useful with buggy plugins that have
  their editor GUIs misaligned after resizing the window. These plugins tend to
  draw their GUI based on (top level) window's absolute screen coordinates
  instead of their own relative position within the parent window. Some known
  plugins that can benefit from this are _PSPaudioware E27_ and _Soundtoys
  Crystallizer_.

### Removed

- The `editor_double_embed` option added in yabridge 1.4.0 has been removed as
  the `editor_coordinate_hack` option supersedes it.

### Changed

- The Wine plugin host applications now print their version information before
  the `Usage: ` string when invoked without any command line arguments.
- VST3 Data (SysEx) events now use the same small buffer optimization yabridge
  already used for VST2 SysEx events. This avoids allocations when a VST3 plugin
  sends or receives a small SysEx event.

### Fixed

- Worked around a [bug](https://svn.boost.org/trac10/changeset/72855) in
  Boost.Process that would cause yabridge to crash with an
  `locale::facet::_S_create_c_locale name not valid` exception when (part of)
  the current locale is invalid. This could happen on Arch Linux if you skipped
  part of the Arch installation process.
- Fixed New Sonic Arts' _Vice_ plugin freezing when loading the plugin. This
  happened because the plugin interacted with the GUI and tried to spawn new
  threads when the host changes the sample rate or block size from the audio
  thread. These things are now done from the main GUI thread, so please let me
  know if there are any new loading issues with other VST2 plugins after this
  update.
- Fixed the drag-and-drop implementation not sending an `XdndStatus` message on
  the very first tick. This fixes drag-and-drop from the _Samplab_ plugin which
  has a broken drag-and-drop implementation and only starts the operation after
  the left mouse button has already been released.
- Fixed the drag-and-drop implementation not properly handling errors caused by
  the pointer being grabbed. This would only happen with _Samplab_.
- Fixed sub 100 millisecond drag-and-drop operations being ignored by certain
  hosts, like **Bitwig Studio**. This would only happen with _Samplab_. The XDND
  implementation now has a warmup phase to prevent this from happening.

### yabridgectl

- `yabridgectl rm` and `yabridgectl blacklist rm` now accept relative paths.

### Packaging notes

- We now target VST3 SDK version 3.7.3 with git tag `v3.7.3_build_20-patched`.
- Because of an
  [update](https://github.com/clap-rs/clap/commit/241d183b9c3fb21ea4ecb6720a9d2118b9533029)
  to clap, the minimum rustc version required to build yabridgectl is now
  `1.54`.

## [3.5.2] - 2021-08-08

### Added

- Added support for VST2 plugins sending and receiving SysEx events. Certain
  MIDI controllers like the _Arturia MiniLab Mk II_ output SysEx events when
  changing between octaves, and some hosts like **REAPER** forwards these events
  directly to the plugin. Before this change this might cause crashes with
  plugins that try to handle SysEx events, like the _D16 Group_ plugins.

### Fixed

- Fixed a regression from yabridge 3.5.1 where certain VST3 plugins wouldn't
  resize to their correct size when opening the editor. This affected
  **Kontakt**, and it was caused by reverting just a little bit too much code in
  the regression fix from the previous release.
- Fixed _D16 Group_ plugins crashing when the host tries to send SysEx events.

## [3.5.1] - 2021-07-31

### Added

- You can now directly focus the plugin's editor instead of allowing the host to
  process keyboard events by holding down the <kbd>Shift</kbd> key while
  entering a plugin's GUI with your mouse. Certain hosts like **Bitwig Studio**
  normally still respond to common key presses like <kbd>Space</kbd> for
  play/pause while interacting with a plugin. That in turn can make it
  impossible to type a space character in those hosts, which may become a
  problem when searching for or naming presets. With this feature you can
  temporarily override this behaviour and allow all keyboard input to go
  directly to Wine. This can also be useful for _Voxengo_ plugins, which don't
  grab input focus in their settings and license dialogs.

### Changed

- Added more tracing for the input focus handling when using the `+editor`
  `YABRIDGE_DEBUG_LEVEL` flag.
- Yabridge will now handle X11 events from within the Win32 message loop. What
  this means is that X11 events are now handled even when the plugin is blocking
  the GUI thread, which can potentially increase responsiveness and help with
  graphical issues in certain situations (although at the moment there aren't
  any known situations where the old approach caused any issues).

### Fixed

- Reverted the workaround for the _Nimble Kick_ plugin freezing added in
  yabridge 3.5.0. This could cause VST3 plugins in **Bitwig Studio** and
  **Ardour** to have frozen, nonfunctional editors when using multiple instances
  of a plugin unless you opened every plugin instance's editor. Since the plugin
  would also cause the native Windows version of Bitwig to crash, we will thus
  simply revert this change.
- Fixed a regression from yabridge 3.5.0 where clicking inside of a plugin GUI
  while the window is already open in the background would not give keyboard
  focus to the plugin.
- Changed how input focus releasing works by more selectively filtering out
  mouse pointer leave events where the pointer is still hovering over a Wine
  window instead of ignoring an entire wider class of events. This should fix
  some edge cases where input focus would not be given back to the host, or
  where dropdown menus could close immediately when hovering over and them
  leaving them with your mouse. The first case would in practice only happen
  when using a touchscreen or drawing tablet, since it would require the mouse
  to instantly move from the plugin GUI to another window without first going
  over the window's borders.
- Similarly, the filter in yabridge 3.5.0's Wine->X11 drag-and-drop
  implementation for distinguishing between Wine windows and other windows (so
  that we won't interfere with Wine's own internal drag-and-drop mechanism) has
  also been made more specific. Before this change we might use our own
  XDND-based drag-and-drop implementation when dragging files from a plugin to a
  standalone Wine application running within the same Wine prefix.

## [3.5.0] - 2021-07-23

### Added

- Added a warning on startup if yabridge may not be able to lock enough shared
  memory for its audio processing. If you have not yet set up realtime
  priviliges and memory locking limits for your user, then yabridge may not be
  able to map enough shared memory for processing audio with plugins that have a
  lot of inputs or outputs channels.
- When this shared memory mapping fails because of a low value being set for
  `RLIMIT_MEMLOCK`, yabridge will now print a more specific error message
  telling you about the issue and how to fix it.
- Added a an optional `+editor` flag to the `YABRIDGE_DEBUG_LEVEL` environment
  variable that causes debug tracing information about the plugin editor window
  to be printed. This can be useful for diagnosing DAW or window manager
  specific issues.

### Changed

- The way editor embedding works has been rewritten. Yabridge now inserts a
  wrapper window between the host's parent window and the embedded Wine window
  instead of embedding the Wine window directly into the host. This should get
  rid of all rare edge cases where the host would ignore the window size
  reported by the plugin and would instead try to detect the plugin's size on
  its own by intercepting configuration events sent to the Wine window. This
  could cause the editor window to grow to fit the entire screen in certain
  hosts under very specific circumstances.
- We now support version 3 and 4 of the XDND specification for the Wine->X11
  drag-and-drop support. Before this yabridge assumed every application
  supported version 5 from 2002, but JUCE based hosts only support XDND version 3.

### Fixed

- Fixed crashes or freezes when a plugin uses the Windows drag-and-drop system
  to transfer arbitrary, vendor specific data. This prevents **Reaktor** from
  freezing when editing a patch after upgrading to yabridge 3.4.0.
- Fixed yabridge thinking that the Wine plugin host process has died when the
  user doesn't have permissions to access the Wine process's memory. This fixes
  a seemingly very rare regression from yabridge 3.4.0 where the Wine plugin
  host application would immediately be seen as dead when using _AppArmor_,
  preventing yabridge from starting.
- Fixed a regression from yabridge 3.4.0 where plugins with zero input and
  output audio channels like FrozenPlain **Obelisk** would result in a crash.
- Fixed a regression from yabridge 3.4.0 where JUCE-based VST3 plugins might
  cause **Ardour** or **Mixbus** to freeze in very specific circumstances.
- As mentioned above, it's now no longer possible for hosts to wrongly detect
  the editor window size. This fixes a rare issue with **Ardour** on older XFCE
  versions where the editor window would extend to cover the entire screen. A
  similar issue also exists with **Carla** 2.3.1.
- This same change also fixes VST3 editors in **Ardour** not rendering past
  their original size when resizing them from the plugin (as opposed to resizing
  the actual window).
- Worked around a **REAPER** bug that would cause REAPER to not process any
  keyboard input when the FX window is active but the mouse cursor is positioned
  outside of the window. We now use the same validation used in `xprop` and
  `xwininfo` to find the host's window instead of always taking the topmost
  window.
- Fixed Wine->X11 drag-and-drop in **Tracktion Waveform**. Waveform only
  supports an old 1998 version of the XDND specification, so it was ignoring our
  messages since we assumed every application would support the most recent XDND
  version from 2002.
- Worked around a race condition in _Nimble Kick_, which would trigger a stack
  overflow when loading the plugin if it wasn't already activated.
- Potentially fixed an obscure issue where the editor would not render at all
  when using multiple displays and the rightmost display was set as the primary
  display. This issue appears to be very rare, and I haven't gotten any response
  back when I asked the people affected by this to test a potential fix, so I'm
  just including it in yabridge anyways in case it helps. If anyone was affected
  by this, please let me know if this update makes any difference!

### yabridgectl

- `yabridgectl status` now also lists the paths to the `yabridge-host.exe` and
  `yabridge-host-32.exe` binaries that yabridge will end up running. This can be
  helpful for diagnosing issues with complex setups.

## [3.4.0] - 2021-07-15

### Added

- Added support for drag-and-drop from Windows plugins running under yabridge to
  native applications, such as your DAW. This makes it much more convenient to
  use plugins like _Scaler 2_ that generate audio or MIDI files. Because of the
  way this is implemented this feature will work with any Wine version.
- When a plugin fails to load or when the Wine plugin host process fails to
  start, yabridge will now show you the error in a desktop notification instead
  of only printing it to the logger. This will make it much faster to quickly
  diagnose issues if you weren't already running your DAW from a terminal. These
  notifications require `libnotify` and its `notify-send` application to be
  installed.
- Similarly, yabridge will show you a warning and a desktop notification with a
  reminder to rerun `yabridgctl sync` when it detects that there's been a
  version mismatch between the plugin and the used Wine plugin host application.
- Added support for building 32-bit versions of the yabridge libraries, allowing
  you to use both 32-bit and 64-bit Windows VST2 and VST3 plugins under 32-bit
  Linux plugin hosts. This should not be needed in any normal situation since
  Desktop Linux has been 64-bit only for a while now, but it could be useful in
  some very specific situations. Building on an actual 32-bit system will also
  work, in which case the 64-bit Wine plugin host applications simply won't be
  built.
- Added the deprecated pre-VST2.4 `main` entry point for VST2 plugins. This
  allows the above mentioned 32-bit version of yabridge to be used in
  **EnergyXT**, allowing you to use both 32-bit and 64-bit Windows VST2 plugins
  there.
- Added an environment variable to disable the watchdog timer. This is only
  needed when running the Wine process under a separate namespace. If you don't
  know that you need this, then you probably don't need this!

### Changed

- The audio processing implementation for both VST2 and VST3 plugins has been
  completely rewritten to use both shared memory and message passing to cut down
  the number of expensive memory copies to a minimum. This reduces the DSP load
  overhead of audio processing even further.
- Respect `$XDG_DATA_HOME` as a fallback when looking for yabridge's plugin host
  binaries instead of hardcoding this to `~/.local/share/yabridge`. This matches
  the existing behaviour in yabridgectl.
- Optimized the management of VST3 plugin instances to reduce the overhead when
  using many instances of a single VST3 plugin.
- Slightly optimized the function call dispatch for VST2 plugins.
- Prevented some more potential unnecessary memory operations during yabridge's
  communication. The underlying serialization library was recreating some
  objects even when this wasn't needed, which could result in unnecessary memory
  allocations under certain circumstances. This is related to the similar issue
  that was fixed in yabridge 3.3.0. A fix for this issue has also been
  upstreamed to the library.

### Fixed

- Fixed mouse cursors disappearing when interacting with some plugin GUIs. This
  often happened with _JUCE_ based plugins, such as Sonic Academy's _Kick 2_ and
  _Anaglyph_. While this is technically a workaround for a bad interaction
  between JUCE and Wine, it should make these plugins much more pleasant to use.
- Fixed _Waves_ VST3 plugins not being able to initialize correctly. These
  plugins would at runtime change their query interface to support more VST3
  interfaces, including the mandatory edit controller interface. Yabridge now
  requeries the supported interfaces at a later stage to work around this.
- Fixed VST2 plugins in **Ardour** not receiving all transport information,
  breaking host sync and LFOs in certain plugins. This was a regression from
  yabridge 3.2.0.
- Fixed input focus handling being broken **REAPER** after reopning a closed FX
  window. Now moving the mouse cursor outside of the plugin's GUI will always
  release input focus, even after closing the window.
- Fixed _Insert Piz Here_'s _midiLooper_ crashing in **REAPER** when the plugin
  tries to use REAPER's [host function
  API](https://www.reaper.fm/sdk/vst/vst_ext.php#vst_host). This currently isn't
  supported by yabridge. We now explicitly ignore these requests.
- Worked around a rare thread safety issue in _MeldaProduction_ VST3 plugins
  where the plugin would deadlock when the host asks for the editor's size while
  plugin is also being initialized from the audio thread at the same time.
- Fixed JUCE VST3 plugins like Tokyo Dawn Records' _SlickEQ M_ causing the host
  to freeze when they send a parameter change from the audio thread using the
  wrong VST3 API while the plugin is also trying to resize the window from the
  GUI thread at the same time. This would happen in _SlickEQ M_ when reopning
  the Smart Ops panel after having used it once. To fix this, yabridge's
  Wine-side VST3 mutual recursion mechanism now only operates when invoked from
  the GUI thread.
- Fixed yabridge's logging seeking the STDERR stream to position 0 every time it
  writes a log message. This would be noticeable when piping the host's STDERR
  stream to a file and `YABRIDGE_DEBUG_LEVEL` wasn't set.
- When printing the Wine version during initialization, the Wine process used
  for this is now run under the same environment that the Wine plugin host
  process will be run under. This means that if you use a custom `WINELOADER`
  script to use different Wine versions depending on the prefix,
  the `wine version:` line in the initialization message will now always match
  the version of Wine the plugin is going to be run under.
- Fixed the plugin-side watchdog timer that allows a yabridge plugin to
  terminate when the Wine plugin host application fails to start treating zombie
  processes as still running, active processes. This could cause plugins to hang
  during scanning if the Wine process crashed in a very specific and likely
  impossible way.
- If a VST3 plugin returns a null pointer from `IEditController::createView()`,
  then this will now be propagated correctly on the plugin side.
- Fixed VST2 speaker arrangement configurations returned by the plugin not being
  serialized correctly. Very few plugins and hosts seem to actually use these,
  so it should not have caused any issues.

### yabridgectl

- Added support for setting up merged VST3 bundles when using a 32-bit version
  of `libyabridge-vst3.so`.
- Fixed the post-installation setup checks when the default Wine prefix over at
  `~/.wine` was created with `WINEARCH=win32` set. This would otherwise result
  in an `00cc:err:process:exec_process` error when running `yabridgectl sync`
  because yabridgectl would try to run the 64-bit `yabridge-host.exe` in that
  prefix. Yabridgectl now detects the architecture of the default prefix first
  and then runs the proper Wine plugin host application for that prefix.
- Copies of `libyabridge-vst2.so` and `libyabridge-vst3.so` are now reflinked
  when supported by the file system. This speeds up the file coyping process
  while also reducing the amount of disk space used for yabridge when using
  Btrfs or XFS.
- If pruning causes a directory to be empty, then the empty directory will now
  also be removed. This avoids having your plugin directories littered with
  empty directories.
- Fixed incorrect new and total plugin counts. These counts are now always
  correct, even when using multiple versions of the same VST3 plugin or when
  multiple plugin directories overlap because of the use of symlinks.
- Aside from pruning only unmanaged VST3 bundles in `~/.vst3/yabridge`, yabridge
  will now also prompt you to prune leftover files from within a managed VST3
  bundle. This makes it easy to switch from the 64-bit version of a plugin to
  the 32-bit version, or from a 64-bit version of yabridge to the 32-bit
  version. I don't know why you would want to do either of those things, but now
  you can!
- Yabridgectl now prints a more descriptive error message instead of panicing if
  running `$WINELOADER --version` during yabridgectl's post-setup verification
  checks does not result in any output. This is only relevant when using a
  custom `WINELOADER` script that modifies Wine's output.

## [3.3.1] - 2021-06-09

### Added

- Added thread names to all worker threads created by yabridge. This makes it
  easier to debug and profile yabridge.

### Fixed

- Fixed the `IPlugView::canResize()` cache added in yabridge 3.2.0 sometimes not
  being initialized properly, preventing host-driven resizes in certain
  situations. This was mostly noticeable in **Ardour**.
- Fixed mouse clicks in VST2 editors in **Tracktion Waveform** being offset
  vertically by a small amount because of the way Waveform embeds VST2 editors.
- Fixed _Shattered Glass Audio_ plugins crashing when opening the plugin editor
  because those plugins don't initialize Microsoft COM before trying to use it.
  We now always initialize the Microsoft COM library unconditionally, instead of
  doing it only when a plugin fails to initialize without it.
- Fixed incorrect version strings being reported by yabridge when building from
  a tarball that has been extracted inside of an unrelated git repository. This
  could happen when building the `yabridge` AUR package with certain AUR
  helpers.
- Fixed the log message for the cached `IPlugView::canResize()` VST3 function
  calls implemented in yabridge 3.2.0.

## [3.3.0] - 2021-06-03

### Added

- Added a [compatibility
  option](https://github.com/robbert-vdh/yabridge#compatibility-options) to
  redirect the Wine plugin host's STDOUT and STDERR output streams directly to a
  file. Enabling this allows _ujam_ plugins and other plugins made with the
  Gorilla Engine, such as the _LoopCloud_ plugins, to function correctly. Those
  plugins crash with a seemingly unrelated error message when their output is
  redirected to a pipe.
- Added a small warning during initialization when `RLIMIT_RTTIME` is set to
  some small value. This happens when using PipeWire with rtkit, and it can
  cause crashes when loading plugins.

### Changed

- Added a timed cache for the `IPlugView::canResize()` VST3 function so the
  result will be remembered during an active resize. This makes resizing VST3
  plugin editor windows more responsive.
- Added another cache for when the host asks a VST3 plugin whether it supports
  processing 32-bit or 64-bit floating point audio. Some hosts, like **Bitwig
  Studio**, call this function at the start of every processing cycle even
  though the value won't ever change. Caching this can significantly reduce the
  overhead of bridging VST3 plugins under those hosts.
- Redesigned the VST3 audio socket handling to be able to reuse the process data
  objects on both sides. This greatly reduces the overhead of our VST3 bridging
  by getting rid of all potential memory allocations during audio processing.
- VST2 audio processing also received the same optimizations. In a few places
  yabridge would still reallocate heap data during every audio processing cycle.
  We now make sure to always reuse all buffers and heap data used in the audio
  processing process.
- Considerably optimized parts of yabridge's communication infrastructure by
  preventing unnecessary memory operations. As it turned out, the underlying
  binary serialization library used by yabridge would always reinitialize the
  type-safe unions yabridge uses to differentiate between single and double
  precision floating point audio buffers in both VST2 and VST3 plugins, undoing
  all of our efforts at reusing objects and preventing memory allocations in the
  process. A fix for this issue has also been upstreamed to the library.
- VST3 output audio buffers are now no longer zeroed out at the start of every
  audio processing cycle. We've been doing this for VST3 plugins since the
  introduction of VST3 bridging in yabridge 3.0.0, but we never did this for
  VST2 plugins. Since not doing this has never caused any issues with VST2
  plugins, it should also be safe to also skip this for VST3 plugins. This
  further reduces the overhead of VST3 audio processing.
- Optimized VST3 audio processing for instruments by preallocating small vectors
  for event and parameter change queues.
- VST2 MIDI event handling also received the same small vector optimization to
  get rid of any last potential allocations during audio processing.
- This small vector optimization has also been applied across yabridge's entire
  communication and event handling architecture, meaning that most plugin
  function calls and callbacks should no longer produce any allocations for both
  VST2 and VST3 plugins.
- Changed the way mutual recursion in VST3 plugins on the plugin side works to
  counter any potential GUI related timing issues with VST3 plugins when using
  multiple instances of a plugin.
- Changed the way realtime scheduling is used on the Wine side to be less
  aggressive, potentially reducing CPU usage when plugins are idle.
- The deserialization part of yabridge's communication is now slightly faster by
  skipping some unnecessary checks.
- Log messages about VST3 query interfaces are now only printed when
  `YABRIDGE_DEBUG_LEVEL` is set to 2 or higher, up from 1.

### Fixed

- Fixed a longstanding thread safety issue when hosting a lot of VST2 plugins in
  a plugin group. This could cause plugins to crash or freeze when initializing
  a new instance of a VST2 plugin in a plugin group while another VST2 plugin in
  that same group is currently processing audio.
- Fixed yabridge's Wine processes inheriting file descriptors in some
  situations. This could cause **Ardour** and **Mixbus** to hang when reopening
  the DAW after a crash. The watchdog timer added in yabridge 3.2.0 addressed
  this issue partially, but it should now be completely fixed. This may also
  prevent rare issues where the **JACK** server would hang after the host
  crashes.
- Fixed _DMG_ VST3 plugins freezing in **REAPER** when the plugin resizes itself
  while the host passes channel context information to the plugin.
- Also fixed _DMG_ VST3 plugins freezing in **REAPER** when restoring multiple
  instances of the plugin at once while the FX window is open and the GUI is
  visible.
- Fixed the _PG-8X_ VST2 plugin freezing in **REAPER** when loading the plugin.
- Fixed _Voxengo_ VST2 plugins freezing in **Renoise** when loading a project or
  when otherwise restoring plugin state.
- Fixed logging traces in the VST2 audio processing functions and the VST3 query
  interfaces causing allocations even when `YABRIDGE_DEBUG_LEVEL` is not set to 2.
- Fixed building on Wine 6.8 after some internal changes to Wine's `windows.h`
  implementation.

### yabridgectl

- Improved the warning yabridgectl shows when it cannot run `yabridge-host.exe`
  as part of the post-installation setup checks.
- Fixed the reported number of new or updated plugins when yabridgectl manages
  both a 32-bit and a 64-bit version of the same VST3 plugin.
- Fixed text wrapping being broken after a dependency update earlier this year.

## [3.2.0] - 2021-05-03

### Added

- During VST2 audio processing, yabridge will now prefetch the current transport
  information and process level before sending the audio buffers over to the
  Windows VST2 plugin. This lets us cache this information on the Wine side
  during the audio processing call, which significantly reduces the overhead of
  bridging VST2 plugins by avoiding one or more otherwise unavoidable back and
  forth function calls between yabridge's native plugin and the Wine plugin
  host. While beneficial to every VST2 plugin, this considerably reduces the
  overhead of bridging _MeldaProduction_ VST2 plugins, and it has an even
  greater impact on plugins like _SWAM Cello_ that request this information
  repeatedly over the course of a single audio processing cycle. Previously
  yabridge had a `cache_time_info` compatibility option to mitigate the
  performance hit for those plugins, but this new caching behaviour supercedes
  that option.
- We now always force the CPU's flush-to-zero flag to be set when processing
  audio. Most plugins will already do this by themselves, but plugins like _Kush
  Audio REDDI_ and _Expressive E Noisy_ that don't will otherwise suffer from
  extreme DSP usage increases when processing almost silent audio.
- Added a new [compatibility
  option](https://github.com/robbert-vdh/yabridge#compatibility-options) to hide
  the name of the DAW you're using. This can be useful with plugins that have
  undesirable or broken DAW-specific behaviour. See the [known
  issues](https://github.com/robbert-vdh/yabridge#known-issues-and-fixes)
  section of the readme for more information on when this may be useful.
- Yabridge now uses a watchdog timer to prevent rare instances where Wine
  processes would be left running after the native host has crashed or when it
  got forcefully terminated. By design yabridge would always try to gracefully
  shut down its Wine processes when native host has crashed and the sockets
  become unavailable, but this did not always happen if the crash occurred
  before the bridged plugin has finished initializing because of the way Unix
  Domain Sockets work. In that specific situation the `yabridge-host.exe`
  process would be left running indefinitely, and depending on your DAW that
  might have also prevented you from actually restarting your DAW without
  running `wineserver -k` first. To prevent any more dangling processes,
  yabridge's Wine plugin hosts now have a watchdog timer that periodically
  checks whether the original process that spawned the bridges is still running.
  If it detects that the process is no longer alive, yabridge will close the
  sockets and shut down the bridged plugin to prevent any more dangling
  processes from sticking around.

### Changed

- Most common VST2 functions that don't have any arguments are now handled
  explicilty. Yabridge could always automatically support most VST2 functions by
  simply inspecting the function arguments and handling those accordingly. This
  works practically everywhere, but _Plugsound Free_ by UVI would sometimes pass
  unreadable function arguments to functions that weren't supposed to have any
  arguments, causing yabridge to crash. Explicitly handling those functions
  should prevent similar situations from happening in the future.
- Yabridge will now try to bypass VST3 connection proxies if possible. Instead
  of connecting two VST3 plugin objects directly, **Ardour** and **Mixbus**
  place a connection proxy between the two plugin objects so that they can only
  interact indirectly through the DAW. In the past yabridge has always honored
  this by proxying the host's connection proxy, but this causes difficult
  situations with plugins that actively communicate over these proxies from the
  GUI thread, like the _FabFilter_ plugins. Whenever possible, yabridge will now
  try to bypass the connection proxies and connect the two objects directly
  instead, only falling back to proxying the proxies when that's not possible.
- Compile times have been slightly lowered by compiling most of the Wine plugin
  host into static libraries first.
- When building the package from source, the targetted Wine version now gets
  printed at configure-time. This can make it a bit easier to diagnose
  Wine-related compilation issues.

### Removed

- The `cache_time_info` compatibility option has been removed since it's now
  obsolete.
- Removed a message that would show up when loading a VST3 plugin in Ardour,
  warning about potential crashes due to Ardour not supporting multiple input
  and output busses. These crashes have been resolved since yabridge 3.1.0.

### Fixed

- Fixed rare X11 errors that could occur when closing a plugin's editor. In
  certain circumstances, closing a plugin editor would trigger an X11 error and
  crash the Wine plugin host, and with that likely the entire DAW. This happened
  because Wine would try to destroy the window after it had already been
  destroyed. This could happen in Renoise and to a lesser degree in REAPER with
  plugins that take a while to close their editors, such as the _iZotope Rx_
  plugins. We now explicitly reparent the window to back the root window first
  before deferring the window closing. This should fix the issue, while still
  keeping editor closing nice and snappy.
- Plugin group host processes now shut down by themselves if they don't get a
  request to host any plugins within five seconds. This can happen when the DAW
  gets killed right after starting the group host process but before the native
  yabridge plugin requests the group host process to host a plugin for them.
  Before this change, this would result in a `yabridge-group.exe` process
  staying around indefinitely.
- Prevented latency introducing VST3 from freezing **Ardour** and **Mixbus**
  when loading the plugin. This stops _Neural DSP Darkglass_ from freezing when
  used under those DAWs.
- Fixed _FabFilter_ VST3 plugins freezing in **Ardour** and **Mixbus** when
  trying to duplicate existing instances of the plugin after the editor GUI has
  been opened.
- Fixed VST3 plugins freezing in **Ardour** and **Mixbus** when the plugin tries
  to automate a parameter while loading a preset.
- Fixed _Voxengo_ VST3 plugins freezing in **Ardour** and **Mixbus** when
  loading a project or when duplicating the plugin instances.
- Fixed potential X11 errors resulting in assertion failures and crashes in
  **Ardour** and **Mixbus** when those hosts hide (unmap) a plugin's editor
  window.
- Fixed saving and loading plugin state for VST3 _iZotope Rx_ plugins in
  **Bitwig Studio**.
- Fixed a regression from yabridge 3.1.0 where **REAPER** would freeze when opening
  a VST3 plugin context menu.
- Fixed a potential freezing issue in **REAPER** that could happen when a VST3
  plugin resizes itself while sending parameter changes to the host when
  REAPER's 'disable saving full plug-in state' option has not been disabled.
- Fixed another potential freeze when loading a VST3 plugin preset while the
  editor is open when the plugin tries to resize itself based on that new
  preset.
- Fixed a potential assertion failure when loading VST3 presets. This would
  depend on the compiler settings and the version of `libstdc++` used to built
  yabridge with.
- Fixed _PSPaudioware InifniStrip_ failing to initialize. The plugin expects the
  host to always be using Microsoft COM, and it doesn't try to initialize it by
  itself. InfiniStrip loads as expected now.
- Fixed _Native Instruments' FM7_ crashing when processing MIDI. In order to fix
  this, MIDI events are now deallocated later then when they normally would have
  to be.
- Fixed extreme DSP usage increases in _Kush Audio REDDI_ and _Expressive E
  Noisy_ due to denormals.
- Fixed the VST3 version of _W. A. Production ImPerfect_ crashing during audio
  setup.
- Fixed _UVI Plugsound Free_ crashing during initialization.
- Fixed the Wine version detection when using a custom `WINELOADER`.
- Fixed incorrect logging output for cached VST3 function calls.
- Because of the new VST2 transport information prefetching, the excessive DSP
  usage in _SWAM Cello_ has now been fixed without requiring any manual
  compatibility options.

## [3.1.0] - 2021-04-15

### Added

- Added support for using 32-bit Windows VST3 plugins in 64-bit Linux VST3
  hosts. This had previously been disabled because of a hard to track down
  corruption issue.
- Added an
  [option](https://github.com/robbert-vdh/yabridge#compatibility-options) to
  prefer the 32-bit version of a VST3 plugin over the 64-bit version if both are
  installed. This likely won't be necessary, but because of the way VST3 bundles
  work there's no clean way to separate these. So when both are installed, the
  64-bit version gets used by default.

### Fixed

- Worked around a regression in Wine 6.5 that would prevent yabridge from
  shutting down ([wine bug
  #50869](https://bugs.winehq.org/show_bug.cgi?id=50869)). With Wine 6.5
  terminating a Wine process no longer terminates its threads, which would cause
  yabridge's plugin and host components to wait for each other to shut down.
- Fixed preset/state loading in both the VST2 and VST3 versions of _Algonaut
  Atlas 2.0_ by loading and saving plugin state from the main GUI thread.
- Added a workaround for a bug present in every current _Bluecat Audio_ VST3
  plugin. Those plugins would otherwise crash yabridge because they didn't
  directly expose a core VST3 interface through their query interface.
- Fixed a multithreading related memory error in the VST3 audio processor socket
  management system.

### yabridgectl

- Added an indexing blacklist, accessible through `yabridgectl blacklist`. You
  most likely won't ever have to use this, but this lets you skip over files and
  directories in yabridgectl's indexing process.
- Minor spelling fixes.

### Packaging notes

- The Meson wrap dependencies for `bitsery`, `function2` and `tomlplusplus` are
  now defined using `dependency()` with a subproject fallback instead of using
  `subproject()` directly. This should make it easier to package.
- The VST3 SDK Meson wrap dependency and the patches in
  `tools/patch-vst3-sdk.sh` are now based on version 3.7.2 of the SDK.
- The VST3 SDK Meson wrap now uses a tag (`v3.7.2_build_28-patched`) instead of
  a commit hash.

## [3.0.2] - 2021-03-07

### Fixed

- Fix bus information queries being performed for the wrong bus index. This
  fixes VST3 sidechaining in _Renoise_, and prevents a number of VST3 plugins
  with a sidechain input from causing _Ardour_ and _Mixbus_ to freeze or crash.

## [3.0.1] - 2021-02-26

### Changed

- Wine 6.2 introduced a
  [regression](https://bugs.winehq.org/show_bug.cgi?id=50670) that would cause
  compile errors because some parts of Wine's headers were no longer valid C++.
  Since we do not need the affecting functionality, yabridge now includes a
  small workaround to make sure that the affected code never gets compiled. This
  has been fixed for Wine 6.3.

### Fixed

- Added support for a new ReaSurround related VST2.4 extension that **REAPER**
  recently started using. This would otherwise cause certain plugins to crash
  under REAPER.
- Fixed a regression from yabridge 3.0.0 where log output would no longer
  include timestamps.

### yabridgectl

- Changed the wording and colors in `yabridgectl status` for plugins that have
  not yet been setup to look less dramatic and hopefully cause less confusion.
- Aside from the installation status, `yabridgectl status` now also shows a
  plugin's type and architecture. This is color coded to make it easier to
  visually parse the output.
- Plugin paths printed during `yabridgectl status` and
  `yabridgectl sync --verbose` are now always shown relative to the plugin
  directory instead of the same path prefix being repeated for every plugin.

## [3.0.0] - 2021-02-14

### Added

- Yabridge 3.0 introduces the first ever true Wine VST3 bridge, allowing you to
  use Windows VST3 plugins in Linux VST3 hosts with full VST 3.7.1
  compatibility. Simply tell yabridgectl to look for plugins in
  `$HOME/.wine/drive_c/Program Files/Common Files/VST3`, run `yabridgectl sync`,
  and your VST3 compatible DAW will pick up the new plugins in
  `~/.vst3/yabridge` automatically. Even though this feature has been tested
  extensively with a variety of VST3 plugins and hosts, there's still a
  substantial part of the VST 3.7.1 specification that isn't used by any of the
  hosts or plugins we could get our hands on, so please let me know if you run
  into any weird behaviour! There's a list in the readme with all of the tested
  hosts and their current VST3 compatibility status.
- Added an
  [option](https://github.com/robbert-vdh/yabridge#compatibility-options) to use
  Wine's XEmbed implementation instead of yabridge's normal window embedding
  method. This can help reduce flickering when dragging the window around with
  certain window managers. Some plugins will have redrawing issues when using
  XEmbed or the editor might not show up at all, so your mileage may very much
  vary.
- Added a [compatibilty
  option](https://github.com/robbert-vdh/yabridge#compatibility-options) to
  forcefully enable drag-and-drop support under _REAPER_. REAPER's FX window
  reports that it supports drag-and-drop itself, which makes it impossible to
  drag files onto a plugin editor embedded there. This option strips the
  drag-and-drop support from the FX window, thus allowing you to drag files onto
  plugin editors again.
- Added a frame rate
  [option](https://github.com/robbert-vdh/yabridge#compatibility-options) to
  change the rate at which events are being handled. This usually also controls
  the refresh rate of a plugin's editor GUI. The default 60 updates per second
  may be too high if your computer's cannot keep up, or if you're using a host
  that never closes the editor such as _Ardour_.
- Added a [compatibility
  option](https://github.com/robbert-vdh/yabridge#compatibility-options) to
  disable HiDPI scaling for VST3 plugins. At the moment Wine does not have
  proper fractional HiDPI support, so some plugins may not scale their
  interfaces correctly when the host tells those plugins to scale their GUIs. In
  some cases setting the font DPI in `winecfg`'s graphics tab to 192 will also
  cause the GUIs to scale correctly at 200%.
- Added the `with-vst3` compile time option to control whether yabridge should
  be built with VST3 support. This is enabled by default.

### Changed

- `libyabridge.so` is now called `libyabridge-vst2.so`. If you're using
  yabridgectl then nothing changes here. **To avoid any potential confusion in
  the future, please remove the old `libyabridge.so` file before upgrading.**
- The release archives uploaded on GitHub are now repackaged to include
  yabridgectl for your convenience.
- Window closing is now deferred. This means that when closing the editor
  window, the host no longer has to wait for Wine to fully close the window.
  Most hosts already do something similar themselves, so this may not always
  make a difference in responsiveness.
- Slightly increased responsiveness when resizing plugin GUIs by preventing
  unnecessary blitting. This also reduces flickering with plugins that don't do
  double buffering.
- VST2 editor idle events are now handled slightly differently. This should
  result in even more responsive GUIs for VST2 plugins.
- Win32 and X11 events in the Wine plugin host are now handled with lower
  scheduling priority than other tasks. This might help get rid of potential DSP
  latency spikes when having the editor open while the plugin is doing expensive
  GUI operations.
- Opening and closing plugin editors is now also no longer done with realtime
  priority. This should get rid of any latency spikes during those operations,
  as this could otherwise steal resources away from the threads that are
  processing audio.
- The way realtime priorities assigned has been overhauled:

  - Realtime scheduling on the plugin side is now a more granular. Instead of
    setting everything to use `SCHED_FIFO`, only the spawned threads will be
    configured to use realtime scheduling. This prevents changing the scheduling
    policy of your host's GUI thread if your host instantiates plugins from its
    GUI thread like _REAPER_ does.
  - Relaying messages printed by the plugin and Wine is now done without
    realtime priority, as this could in theory cause issues with plugins that
    produce a steady stream of fixmes or other output.
  - The realtime scheduling priorities of all audio threads in the Wine plugin
    host are now periodically synchronized with those of the host's audio
    threads.

- When using `yabridge.toml` config files, the matched section or glob pattern
  is now also printed next to the path to the file to make it a bit easier to
  see where settings are being set from.
- The architecture document has been updated for the VST3 support and it has
  been rewritten to talk more about the more interesting bits of yabridge's
  implementation.
- Part of the build process has been changed to account for [this Wine
  bug](https://bugs.winehq.org/show_bug.cgi?id=49138). Building with Wine 5.7
  and 5.8 required a change for `yabridge-host.exe` to continue working, but
  that change now also breaks builds using Wine 6.0 and up. The build process
  now detects which version of Wine is used to build with, and it will then
  apply the change conditionally based on that to be able to support building
  with both older and newer versions of Wine. This does mean that when you
  switch to an older Wine version, you might need to run
  `meson setup build --reconfigure` before rebuilding to make sure that these
  changes take effect.
- `yabridge-host.exe` will no longer remove the socket directories if they're
  outside of a temporary directory. This could otherwise cause a very unpleasant
  surprise if someone were to pass random arguments to it when for instance
  trying to write a wrapper around `yabridge-host.exe`.
- When `YABRIDGE_DEBUG_LEVEL` is set to 2 or higher and a plugin asks the host
  for the current position in the song, yabridge will now also print the current
  tempo to help debugging host bugs.

### Fixed

- VST2 plugin editor resizing in **REAPER** would not cause the FX window to be
  resized like it would in every other host. This has now been fixed.
- The function for suspending and resuming audio, `effMainsChanged()`, is now
  always executed from the GUI thread. This fixes **EZdrummer** not producing
  any sound because the plugin makes the incorrect assumption that
  `effMainsChanged()` is always called from the GUI thread.
- Event handling is now temporarily disabled while plugins are in a partially
  initialized state. The VST2 versions of **T-RackS 5** would have a chance to
  hang indefinitely if the event loop was being run before those plugins were
  fully initialized because of a race condition within those plugins. This issue
  was only noticeable when using plugin groups.
- Fixed a potential issue where an interaction between _Bitwig Studio_ and
  yabridge's input focus grabbing method could cause delayed mouse events when
  clicking on a plugin's GUI in Bitwig. This issue has not been reported for
  yabridge 2.2.1 and below, but it could in theory also affect older versions of
  yabridge.

### yabridgectl

- Updated for the changes in yabridge 3.0. Yabridgectl now allows you to set up
  yabridge for VST3 plugins. Since `libyabridge.so` got renamed to
  `libyabridge-vst2.so` in this version, it's advised to carefully remove the
  old `libyabridge.so` and `yabridgectl` files before upgrading to avoid
  confusing situations.
- Added the `yabridgectl set --path-auto` option to revert back to automatically
  locating yabridge's files after manually setting a path with
  `yabridgectl set --path=<...>`.
- Added the `yabridgectl set --no-verify={true,false}` option to permanently
  disable post-installation setup checks. You can still directly pass the
  `--no-verify` argument to `yabridgectl sync` to disable these checks for only
  a single invocation.

## [2.2.1] - 2020-12-12

### Fixed

- Fixed some plugins, notably the _Spitfire Audio_ plugins, from causing a
  deadlock when using plugin groups in _REAPER_. Even though this did not seem
  to cause any issues in other hosts, the race condition that caused this issue
  could also occur elsewhere.

## [2.2.0] - 2020-12-11

### Added

- Added an option to cache the time and tempo info returned by the host for the
  current processing cycle. This would normally not be needed since plugins
  should ask the host for this information only once per audio callback, but a
  bug in _SWAM Cello_ causes this to happen repeatedly for every sample,
  resutling in very bad performance. See the [compatibility
  options](https://github.com/robbert-vdh/yabridge#compatibility-options)
  section of the readme for more information on how to enable this.

### Changed

- When `YABRIDGE_DEBUG_LEVEL` is set to 2 or higher and a plugin asks the host
  for the current position in the song, yabridge will now print that position in
  quarter notes and samples as part of the debug output.
- `YABRIDGE_DEBUG_LEVEL` 2 will now also cause all audio processing callbacks to
  be logged. This makes recognizing misbheaving plugins a bit easier.
- Symbols in all `libyabridge.so` and all Winelib `.so` files are now hidden by
  default.

### Fixed

- Fixed an issue where in certain situations Wine processes were left running
  after the host got forcefully terminated before it got a chance to tell the
  plugin to shut down. This could happen when using Kontakt in Bitwig, as Bitwig
  sets a limit on the amount of time a plugin is allowed to spend closing when
  you close Bitwig, and Kontakt can take a while to shut down.
- Fixed a potential crash or freeze when removing a lot of plugins from a plugin
  group at exactly the same time.

## [2.1.0] - 2020-11-20

### Added

- Added a separate
  [yabridgectl](https://aur.archlinux.org/packages/yabridgectl/) AUR package for
  Arch and Manjaro. The original idea was that yabridgectl would not require a
  lot of changes and that a single
  [yabridgectl-git](https://aur.archlinux.org/packages/yabridgectl-git/) package
  would be sufficient, but sometimes changes to yabridgectl will be incompatible
  with the current release so it's nicer to also have a separate regular
  package.

### Changed

- Yabridge will now always search for `yabridge-host.exe` in
  `~/.local/share/yabridge` even if that directory is not in the search path.
  This should make setup easier, since you no longer have to modify any
  environment variables when installing yabridge to the default location.
  Because of this, the symlink-based installation method does not have a lot of
  advantages over the copy-based method anymore other than the fact that you
  can't forget to rerun `yabridgectl sync` after an upgrade, so most references
  to it have been removed from the readme.

### Fixed

- Fixed an issue where _Renoise_ would show an error message when trying to load
  a plugin in the mixer.

## [2.0.2] - 2020-11-14

### Fixed

- Added a workaround for a bug in _Ardour 6.3_ which would cause several plugins
  including MT Power Drumkit to crash when opening the editor.
- Fixed linking error in debug build related to the parallel STL.

## [2.0.1] - 2020-11-08

### Fixed

- Fixed a regression where `yabridge-host.exe` would not exit on its own after
  the host crashes or gets terminated without being able to properly close all
  plugins.

## [2.0.0] - 2020-11-08

### Added

- The way communication works in yabridge has been completely redesigned to be
  fully concurrent and to use additional threads as necessary. This was needed
  to allow yabridge to handle nested and mutually recursive function calls as
  well as several other edge cases a synchronous non-concurrent implementation
  would fail. What this boils down to is that yabridge became even faster, more
  responsive, and can now handle many scenarios that would previously require
  workarounds. The most noticeable effects of these changes are as follows:

  - The `hack_reaper_update_display` workaround for _REAPER_ and _Renoise_ to
    prevent certain plugins from freezing is no longer needed and has been
    removed.
  - Opening and scanning plugins becomes much faster in several VST hosts
    because more work can be done simultaneously.
  - Certain plugins, such as Kontakt, no longer interrupt audio playback in
    Bitwig while their editor was being opened.
  - Any loading issues in Bitwig Studio 3.3 beta 1 are no longer present.
  - Hosting a yabridged plugin inside of the VST2 version of Carla now works as
    expected.
  - And probably many more improvements.

  Aside from these more noticeable changes, this has also made it possible to
  remove a lot of older checks and behaviour that existed solely to work around
  the limitations introduced by the old event handling system. I have been
  testing this extensively to make sure that these changes don't not introduce
  any regressions, but please let me know if this did break anything for you.

### Changed

- The way the Wine process handles threading has also been completely reworked
  as part of the communication rework.
- GUI updates for plugins that don't use hardware acceleration are now run at 60
  Hz instead of 30 Hz. This was kept at 30 updates per second because that
  seemed to be a typical rate for Windows VST hosts and because function calls
  could not be processed while the GUI was being updated, but since that
  limitation now no longer exists we can safely bump this up.
- Sockets are now created in `$XDG_RUNTIME_DIR` (which is `/run/user/<user_id>`
  on most systems) instead of `/tmp` to avoid polluting `/tmp`.

### Removed

- The now obsolete `hack_reaper_update_display` option has been removed.
- The previously deprecated `use-bitbridge` and `use-winedbg` compilation
  options have been removed. Please use `with-bitbridge` and `with-winedbg`
  instead.

### Fixed

- Fixed a very long standing issue with plugins groups where unloading a plugin
  could cause a crash. Now you can host over a hundred plugins in a single
  process without any issues.
- Fixed another edge case with plugin groups when simultaneously opening
  multiple plugins within the same group. The fallover behaviour that would
  cause all of those plugins to eventually connect to a single group host
  process would sometimes not work correctly because the plugins were being
  terminated prematurely.
- Fixed the implementation of the accumulative `process()` function. As far as
  I'm aware no VST hosts made in the last few decades even use this, but it just
  feels wrong to have an incorrect implementation as part of yabridge.

## [1.7.1] - 2020-10-23

### Fixed

- Fixed a regression where the `editor_double_embed` option would cause X11
  errors and crash yabridge.
- Fixed a regression where certain fake dropdown menus such as those used in the
  Tokyo Dawn Records plugins would close immediately when hovering over them.
- Fixed an issue where plugins hosted within a plugin group would not shut down
  properly in certain situations. This would cause the VST host to hang when
  removing such a plugin.

### yabridgectl

- When running `yabridgectl sync`, existing .so files will no longer be
  recreated unless necessary. This prevents hosts from rescanning all plugins
  after setting up a single new plugin through yabridgectl. Running
  `yabridgectl sync` after updating yabridge will still recreate all existing
  .so files as usual.
- Added a `--force` option to `yabridgectl sync` to always recreate all existing
  .so files like in previous versions.
- Fixed a regression from yabridgectl 1.6.1 that prevented you from removing
  directories that no longer exist using `yabridgectl rm`.

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
  [readme](https://github.com/robbert-vdh/yabridge#known-issues-and-fixes)
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
