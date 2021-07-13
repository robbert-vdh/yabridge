# yabridge

[![Automated builds](https://github.com/robbert-vdh/yabridge/workflows/Automated%20builds/badge.svg?branch=master&event=push)](https://github.com/robbert-vdh/yabridge/actions?query=workflow%3A%22Automated+builds%22+branch%3Amaster)
[![Discord](https://img.shields.io/discord/786993304197267527.svg?label=Discord&logo=discord&logoColor=ffffff&color=7389D8&labelColor=6A7EC2)](https://discord.gg/pyNeweqadf)

Yet Another way to use Windows VST plugins on Linux. Yabridge seamlessly
supports using both 32-bit and 64-bit Windows VST2 and VST3 plugins in a 64-bit
Linux VST host as if they were native VST2 and VST3 plugins, with optional
support for [plugin groups](#plugin-groups) to enable inter-plugin communication
for VST2 plugins and quick startup times. Its modern concurrent architecture and
focus on transparency allows yabridge to be both fast and highly compatible,
while also staying easy to debug and maintain.

![yabridge screenshot](https://raw.githubusercontent.com/robbert-vdh/yabridge/master/screenshot.png)

### Table of contents

- [Tested with](#tested-with)
- [**Usage**](#usage)
  - [Preliminaries](#preliminaries)
  - [**Automatic setup (recommended)**](#automatic-setup-recommended)
  - [Manual setup](#manual-setup)
  - [DAW setup](#daw-setup)
  - [Bitbridge](#bitbridge)
  - [Wine prefixes](#wine-prefixes)
  - [Downgrading Wine](#downgrading-wine)
- [Configuration](#configuration)
  - [Plugin groups](#plugin-groups)
  - [Compatibility options](#compatibility-options)
  - [Example](#example)
- [**Runtime dependencies and known issues**](#runtime-dependencies-and-known-issues)
- [**Troubleshooting common issues**](#troubleshooting-common-issues)
- [**Performance tuning**](#performance-tuning)
  - [Environment configuration](#environment-configuration)
- [Building](#building)
  - [Building without VST3 support](#building-without-vst3-support)
  - [32-bit bitbridge](#32-bit-bitbridge)
- [Debugging](#debugging)
  - [Attaching a debugger](#attaching-a-debugger)

## Tested with

Yabridge has been tested under the following hosts using Wine Staging 6.12:

| Host                            | VST2               | VST3               |
| ------------------------------- | ------------------ | ------------------ |
| Bitwig Studio 3.3.11/4.0 beta 7 | :heavy_check_mark: | :heavy_check_mark: |
| REAPER 6.32                     | :heavy_check_mark: | :heavy_check_mark: |
| Carla 2.3                       | :heavy_check_mark: | :heavy_check_mark: |
| Qtractor 0.9.21                 | :heavy_check_mark: | :heavy_check_mark: |
| Renoise 3.3.2                   | :heavy_check_mark: | :heavy_check_mark: |
| Waveform 11.5.17                | :heavy_check_mark: | :heavy_check_mark: |
| Ardour 6.8                      | :heavy_check_mark: | :heavy_check_mark: |
| Mixbus 7.0.140                  | :heavy_check_mark: | :heavy_check_mark: |

Please let me know if there are any issues with other hosts.

## Usage

You can either download a prebuilt version of yabridge through GitHub's
[releases](https://github.com/robbert-vdh/yabridge/releases) page, or you can
compile it from source using the instructions in the [build](#Building) section
below. If you're downloading the prebuilt version and you're using a distro
that's older than Ubuntu 20.04 such as Ubuntu 18.04, Debian 10, or Linux Mint
19, then you should download the version that ends with `-ubuntu-18.04.tar.gz`.
Alternatively, there are AUR packages available if you are running Arch or
Manjaro ([yabridge](https://aur.archlinux.org/packages/yabridge/),
[yabridge-bin](https://aur.archlinux.org/packages/yabridge-bin/),
[yabridge-git](https://aur.archlinux.org/packages/yabridge-git/)).

### Preliminaries

Yabridge requires a recent-ish version of Wine (Staging). Users of Debian,
Ubuntu, Linux Mint and Pop!\_OS should install Wine Staging from the [WineHQ
repositories](https://wiki.winehq.org/Download) as the versions of Wine provided
by those distro's repositories may be too old to be used with yabridge.

For a general overview on how to use Wine to install Windows applications, check
out Wine's [user guide](https://wiki.winehq.org/Wine_User%27s_Guide#Using_Wine).

### Automatic setup (recommended)

The easiest way to get up and running is through _yabridgectl_. Yabridgectl is
already included in the archives downloaded from GitHub's releases page. If
you're using Arch or Manjaro, then you can install it using the AUR package
corresponding to yabridge package you installed
([yabridgectl](https://aur.archlinux.org/packages/yabridgectl/),
[yabridgectl-git](https://aur.archlinux.org/packages/yabridgectl-git/), and it's
already included in
[yabridge-bin](https://aur.archlinux.org/packages/yabridge-bin/)). More
comprehensive documentation on yabridgectl can be found in its
[readme](https://github.com/robbert-vdh/yabridge/tree/master/tools/yabridgectl),
or by running `yabridgectl --help`.

First, yabridgectl needs to know where it can find yabridge's files. If you have
downloaded the prebuilt binaries from GitHub, then you can simply extract the
archive to `~/.local/share`. Both yabridge and yabridgectl will then pick up the
files in `~/.local/share/yabridge` automatically. You also won't have to do any
additional work if you're using one of the AUR packages. **Since
`~/.local/share/yabridge` will likely not be in your search `PATH`,** **you may
need to replace `yabridgectl` in the commands below with**
**`~/.local/share/yabridge/yabridgectl`.**

Next, you'll want to tell yabridgectl where it can find your VST2 and VST3
plugins. For this you can use yabridgectl's `add`, `rm` and `list` commands. You
can also use `yabridgectl status` to get an overview of the current settings and
the installation status for all of your plugins. To add the most common VST2
plugin directory, use
`yabridgectl add "$HOME/.wine/drive_c/Program Files/Steinberg/VstPlugins"`. The
directory may be capitalized as `VSTPlugins`
on your system, and some plugins may also install themselves to a similar
directory directly inside of Program Files. VST3 plugins under Windows are
always installed to the same directory, and you can use
`yabridgectl add "$HOME/.wine/drive_c/Program Files/Common Files/VST3"` to add
that one.

Finally, you'll have to run `yabridgectl sync` to finish setting up yabridge for
all of your plugins. For VST2 plugins this will create `.so` files alongside the
Windows VST2 plugins that your DAW will be able to read, so if you tell your
Linux VST2 host to search for VST2 plugins in that same directory you'll be good
to go. VST3 plugins are always set up in `~/.vst3/yabridge` as per the VST3
specification, and your VST3 host will pick those up automatically without any
additional setup. _Don't forget to rerun `yabridgectl sync` whenever you update
yabridge if you are using the default copy-based installation method._

### Manual setup

Setting up yabridge through yabridgectl is the recommended installation method
as it makes setting up plugins and updating yabridge easier. Yabridgectl will
also check for some common issues during the installation process so you can
get up and running faster. To manually set up yabridge for VST2 plugins, first
download and extract yabridge's files just like in the section above. Yabridge's
files have to be extracted to `~/.local/share`, such that
`~/.local/share/yabridge/libyabridge-vst2.so` exists. If you want to set up
yabridge for a VST2 plugin located at
`~/.wine/drive_c/Program Files/Steinberg/VstPlugins/plugin.dll`,
then you'll have to copy `~/.local/share/yabridge/libyabridge-vst2.so` to
`~/.wine/drive_c/Program Files/Steinberg/VstPlugins/plugin.so`. This process has
to be repeated for all of your installed plugins whenever you download a new
version of yabridge.

Doing the same thing for VST3 plugins involves creating a [merged VST3
bundle](https://developer.steinberg.help/display/VST/Plug-in+Format+Structure#PluginFormatStructure-MergedBundle)
by hand with the Windows VST3 plugin symlinked in. Doing this without
yabridgectl is not supported since the process is very error prone.

### DAW setup

After first setting up yabridge for VST2 plugins, open your DAW's plugin
location configuration and tell it to search for VST2 plugins under
`~/.wine/drive_c/Program Files/Steinberg/VstPlugins`, or whichever VST2 plugin
directories you've added in yabridgectl. That way it will automatically pick up
all of your Windows VST2 plugins. For VST3 plugins no additional DAW
configuration is needed, as those plugins will be set up under
`~/.vst3/yabridge`.

If you're using a DAW that does not have an easy way to configure VST2 plugin
paths such as Renoise, then you may want to just symlink the plugin directories
to your DAW's default search location, like this:

```shell
ln -s "$HOME/.wine/drive_c/Program Files/Steinberg/" ~/.vst/yabridge-steinberg
```

### Bitbridge

If you have downloaded the prebuilt version of yabridge or if have followed the
instructions from the [bitbridge](#32-bit-bitbridge) section below, then
yabridge is also able to load 32-bit VST2 and VST3 plugins. The installation
procedure for 32-bit plugins is exactly the same as for 64-bit plugins. Yabridge
will automatically detect whether a plugin is 32-bit or 64-bit on startup and it
will handle it accordingly.

If you want to use the 32-bit version of a VST3 plugin when you also have the
64-bit version installed, then you may have to enable the `vst3_prefer_64bit`
[compatibility
option](https://github.com/robbert-vdh/yabridge#compatibility-options) if those
two plugins are located in the same VST3 bundle in `~/.vst3/yabridge`.

### Wine prefixes

It is also possible to use yabridge with multiple Wine prefixes. Yabridge will
automatically detect and use the Wine prefix the plugin's `.dll` or `.vst3` file
is located in. Alternatively, you can set the `WINEPREFIX` environment variable
to override the Wine prefix for _all instances_ of yabridge.

### Downgrading Wine

There have been a couple of small regressions in Wine after Wine 6.4. If you run
into software or a plugin that does not work correctly with the current version
of Wine Staging, then you may want to try downgrading to an earlier version of
Wine. This can be done as follows:

- On Debian, Ubuntu, Linux Mint and other apt-based distros, you can use the
  command below to install Wine Staging 6.4 after you add the WineHQ
  repositories linked above. This command is a bit complicated because on these
  distros the Wine package is split up into multiple smaller packages, and the
  package versions include the distros codename (e.g. `focal`, or `buster`).

  ```shell
  version=6.4
  codename=$(awk '/^deb https:\/\/dl\.winehq\.org/ { print $3 }' /etc/apt/sources.list)
  sudo apt install --install-recommends {winehq-staging,wine-staging,wine-staging-amd64,wine-staging-i386}=$version~$codename-1
  ```

  If you want to prevent these packages from being updated automatically, then
  you can do so with:

  ```shell
  sudo apt-mark hold winehq-staging
  ```

  Running the same command with `unhold` instead of `hold` will enable updates
  again.

- On Arch and Manjaro, you can install the
  [downgrade](https://aur.archlinux.org/packages/downgrade/) tool from the repos
  or the AUR, then run:

  ```shell
  sudo env DOWNGRADE_FROM_ALA=1 downgrade wine-staging
  ```

  Then select the package for the wine-staging version you want to isntall from
  the list. After installing downgrade will ask if you want to add the package
  to `IgnorePkg`. If you select `yes`, the package will be added to the
  `IgnorePkg` field in `/etc/pacman.conf` and it won't be updated again
  automatically.

## Configuration

Yabridge can be configured on a per plugin basis to host multiple plugins within
a single process using [plugin groups](#plugin-groups), and there are also a
number of [compatibility options](#compatibility-options) available to improve
compatibility with certain hosts and plugins.

Configuring yabridge is done through a `yabridge.toml` file located in either
the same directory as the plugin's `.so` file you're trying to configure, or in
any of its parent directories. This file contains case sensitive
[glob](https://www.man7.org/linux/man-pages/man7/glob.7.html) patterns that
match paths to yabridge `.so` files relative to the `yabridge.toml` file. These
patterns can also match an entire directory to apply settings to all plugins
within that directory. To avoid confusion, only the first `yabridge.toml` file
found and only the first matching glob pattern within that file will be
considered. See below for an [example](#example) of a `yabridge.toml` file. On
startup, yabridge will print used `yabridge.toml` file and the matched section
within it, as well as all of the options that have been set.

### Plugin groups

| Option  | Values            | Description                                                            |
| ------- | ----------------- | ---------------------------------------------------------------------- |
| `group` | `{"<string>",""}` | Defaults to `""`, meaning that the plugin will be hosted individually. |

Some plugins have the ability to communicate with other instances of that same
plugin or even with other plugins made by the same manufacturer. This is often
used in mixing plugins to allow different tracks to reference each other without
having to route audio between them. Examples of plugins that do this are
FabFilter Pro-Q 3, MMultiAnalyzer and the iZotope mixing plugins. In order for
this to work, all instances of a particular plugin will have to be hosted in the
same process.

Yabridge has the concept of _plugin groups_, which are user defined groups of
plugins that will all be hosted inside of a single process. Plugins groups can
be configured for a plugin by setting the `group` option of that plugin to some
name. All plugins with the same group name will be hosted within a single
process. Of course, plugin groups with the same name but in different Wine
prefixes and with different architectures will be run independently of each
other. See below for an [example](#example) of how these groups can be set up.

_Note that because of the way VST3 works, multiple instances of a single VST3
plugin will always be hosted in a single process regardless of whether you have
enabled plugin groups or not._ _The only reason to use plugin groups with VST3
plugins is to get slightly lower loading times the first time you load a new
plugin._

### Compatibility options

| Option                | Values                  | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| --------------------- | ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `disable_pipes`       | `{true,false,<string>}` | When this option is enabled, yabridge will redirect the Wine plugin host's output streams to a file without any further processing. See the [known issues](#runtime-dependencies-and-known-issues) section for a list of plugins where this may be useful. This can be set to a boolean, in which case the output will be written to `$XDG_RUNTIME_DIR/yabridge-plugin-output.log`, or to an absolute path (with no expansion for tildes or environment variables). Defaults to `false`. |
| `editor_double_embed` | `{true,false}`          | Compatibility option for plugins that rely on the absolute screen coordinates of the window they're embedded in. Since the Wine window gets embedded inside of a window provided by your DAW, these coordinates won't match up and the plugin would end up drawing in the wrong location without this option. Currently the only known plugins that require this option are _PSPaudioware_ plugins with expandable GUIs, such as E27. Defaults to `false`.                               |
| `editor_force_dnd`    | `{true,false}`          | This option forcefully enables drag-and-drop support in _REAPER_. Because REAPER's FX window supports drag-and-drop itself, dragging a file onto a plugin editor will cause the drop to be intercepted by the FX window. This makes it impossible to drag files onto plugins in REAPER under normal circumstances. Setting this option to `true` will strip drag-and-drop support from the FX window, thus allowing files to be dragged onto the plugin again. Defaults to `false`.      |
| `editor_xembed`       | `{true,false}`          | Use Wine's XEmbed implementation instead of yabridge's normal window embedding method. Some plugins will have redrawing issues when using XEmbed and editor resizing won't always work properly with it, but it could be useful in certain setups. You may need to use [this Wine patch](https://github.com/psycha0s/airwave/blob/master/fix-xembed-wine-windows.patch) if you're getting blank editor windows. Defaults to `false`.                                                     |
| `frame_rate`          | `<number>`              | The rate at which Win32 events are being handled and usually also the refresh rate of a plugin's editor GUI. When using plugin groups all plugins share the same event handling loop, so in those the last loaded plugin will set the refresh rate. Defaults to `60`.                                                                                                                                                                                                                    |
| `hide_daw`            | `{true,false}`          | Don't report the name of the actual DAW to the plugin. See the [known issues](#runtime-dependencies-and-known-issues) section for a list of situations where this may be useful. This affects both VST2 and VST3 plugins. Defaults to `false`.                                                                                                                                                                                                                                           |
| `vst3_no_scaling`     | `{true,false}`          | Disable HiDPI scaling for VST3 plugins. Wine currently does not have proper fractional HiDPI support, so you might have to enable this option if you're using a HiDPI display. In most cases setting the font DPI in `winecfg`'s graphics tab to 192 will cause plugins to scale correctly at 200% size. Defaults to `false`.                                                                                                                                                            |
| `vst3_prefer_32bit`   | `{true,false}`          | Use the 32-bit version of a VST3 plugin instead the 64-bit version if both are installed and they're in the same VST3 bundle inside of `~/.vst3/yabridge`. You likely won't need this.                                                                                                                                                                                                                                                                                                   |

These options are workarounds for issues mentioned in the [known
issues](#runtime-dependencies-and-known-issues) section. Depending on the hosts
and plugins you use you might want to enable some of them.

### Example

All of the paths used here are relative to the `yabridge.toml` file. A
configuration file for VST2 plugins might look a little something like this:

```toml
# ~/.wine/drive_c/Program Files/Steinberg/VstPlugins/yabridge.toml

["FabFilter Pro-Q 3.so"]
group = "fabfilter"

["MeldaProduction/Tools/MMultiAnalyzer.so"]
group = "melda"

# Matches an entire directory and all files inside it, make sure to not include
# a trailing slash
["ToneBoosters"]
group = "toneboosters"

["PSPaudioware"]
editor_double_embed = true

["Analog Lab 3.so"]
editor_xembed = true

["Chromaphone 3.so"]
hide_daw = true

["sforzando VST_x64.so"]
editor_force_dnd = true
frame_rate = 24

["LoopCloud*"]
disable_pipes = true

# Simple glob patterns can be used to avoid unneeded repetition
["iZotope*/Neutron *"]
group = "izotope"

# Since this file has already been matched by the above glob pattern, this won't
# do anything
["iZotope7/Neutron 2 Mix Tap.so"]
group = "This will be ignored!"

# Of course, you can also add multiple plugins to the same group by hand
["iZotope7/Insight 2.so"]
group = "izotope"

# This would cause all plugins to be hosted within a single process. Doing so
# greatly reduces the loading time of individual plugins, with the caveat being
# that plugins are no longer sandboxed from each other.
#
# ["*"]
# group = "all"
```

For VST3 plugins you should just match the directory instead of the `.so` file
deep within in, like this:

```toml
# ~/.vst3/yabridge/yabridge.toml

["FabFilter*.vst3"]
group = "fabfilter"
vst3_no_scaling = true

["Chromaphone 3.vst3"]
hide_daw = true

["Misstortion2.vst3"]
vst3_no_scaling = true

["*/*Spectral*.vst3"]
vst3_prefer_32bit = true

# These options would be applied to all plugins that do not already have their
# own configuration set
["*"]
editor_force_dnd = true
vst3_no_scaling = true
```

## Runtime dependencies and known issues

Any plugin should function out of the box, although some plugins will need some
additional dependencies for their GUIs to work correctly. Notable examples
include:

- If plugins have missing or invisible text, then installing `corefonts` through
  `winetricks` may help.
- **Serum** requires you to disable `d2d1.dll` in `winecfg` and to install
  `gdiplus` through `winetricks`. You may also want to disable the tooltips by
  going to the global settings tab, unchecking 'Show help tooltips', and
  clicking on the save icon next to 'Preferences'.
- **Native Instruments** plugins work, but Native Access is unable to finish
  installing the plugins. To work around this you can open the .iso file
  downloaded to your downloads directory and run the installer directly. _With
  Wine (Staging) 6.8 or later Native Access might also not be able to finish the
  download, in which case you should downgrade Wine first using the
  [instructions above](#downgrading-wine)._ You may also have to manually
  terminate the ISO driver installation process when installing Native Access
  for the first time to allow the installation to proceed. Some Native
  Instruments .iso files contain hidden files, and the installer will fail
  unless you mount the .iso file with the correct mounting options. To do this,
  first run `udisksctl loop-setup -f ~/Downloads/<filename>.iso` to load the
  .iso file, and then use `udisksctl mount -t udf -o unhide -b /dev/loopX` where
  `/dev/loopX` corresponds to the loop device printed by the `loop-setup`
  command to mount the .iso file to a directory in `/run/media`.

  If you're using an older distro and you're getting a
  `Mount option 'unhide' is not allowed` error when trying to mount the file,
  then you may need to manually create or edit `/etc/udisks2/mount_options.conf`
  first, adding the following to the file:

  ```conf
  [defaults]
  udf_allow=uid=$UID,gid=$GID,iocharset,utf8,umask,mode,dmode,unhide,undelete
  ```

- If **Spitfire Audio** plugins like **BBC Symphony Orchestra** and **LABS** are
  unable to load their sample libraries (_Error #X: Something went wrong_), then
  you can try reinstalling those plugins to a new, clean Wine prefix. To avoid
  potential confusion, make sure to uninstall the Spitfire software along with
  the VST2 and VST3 plugins from your main Wine prefix first.
- Several **JUCE** based plugins have an issue under Wine where the mouse cursor
  will disappear after interacting with certain UI elements. This can usually be
  fixed by mousing over the resize handle in the bottom right corner.
- Arturia's **Pigments**, Sonic Academy's **Kick 2**, Cytomic's **The Drop** and
  likely other plugins have an issue where the GUI freezes when it's trying to
  display a tooltip. This can be fixed by enabling the '_Hide Wine version from
  applications_' option in the Staging tab of winecfg.
- The GUI in **Sforzando** may appear to not respond to mouse clicks depending
  on your Wine and system configuration. This is actually a redrawing issue, and
  the GUI will still be updated even if it doesn't look that way. Dragging the
  window around or just clicking anywhere in the GUI will force a redraw and
  make the GUI render correctly again.
- **MeldaProduction** plugins have minor rendering issues when GPU acceleration
  is enabled. This can be fixed by disabling GPU acceleration in the plugin
  settings. I'm not sure whether this is an issue with Wine or the plugins
  themselves. Notable issues here are missing redraws and incorrect positioning
  when the window gets dragged offscreen on the top and left dies of the screen.
- Knobs in **Tokyo Dawn Records** plugins may not behave as expected when
  dragging long distances. Setting the 'Continuous Drag' option in the plugin's
  options to 'Linear' fixes the issue.
- Similarly, the knobs in **Voxengo** plugins behave better when you enable the
  'Radial knob mode' setting in the global settings.
- If **Scaler 2**'s interface lags, blacks out, or otherwise renders poorly,
  then you can try enabling [software
  rendering](https://forum.scalerplugin.com/t/scaler-2-black-empty-window/3540/8)
  to fix these issues.
- **ujam** plugins and other plugins made with the Gorilla Engine, such as the
  **LoopCloud** plugins, will throw a `JS_EXEC_FAILED` error when trying to load
  the plugin. Enabling the `disable_pipes` [compatibility
  option](#compatibility-options) for those plugins will fix this.
- Plugins by **KiloHearts** have file descriptor leaks when _esync_ is enabled,
  causing Wine and yabridge to eventually stop working after the system hits the
  open file limit. To fix this, either unset `WINEESYNC` while using yabridge or
  switch to using [_fsync_](#performance-tuning) instead.
- **PSPaudioware** plugins with expandable GUIs, such as E27, may have their GUI
  appear in the wrong location after the GUI has been expanded. You can enable
  an alternative [editor hosting mode](#compatibility-options) to fix this.
- When using recent _Applied Acoustics_ plugins like **Chromaphone 3** under
  _Bitwig Studio_, text entry will cause the plugin to crash because Chromaphone
  uses a different text entry method when it detects Bitwig. You can use the
  `hide_daw` [compatibility option](#compatibility-options) to work around this.
- VST2 plugins like **FabFilter Pro-Q 3** that can share data between different
  instances of the same plugin plugins have to be hosted within a single process
  for that functionality to work. See the [plugin groups](#plugin-groups)
  section for instructions on how to set this up. This is not necessary for VST3
  plugins, as multiple instances of those plugins will always be hosted in a
  single process by design.
- Some hosts, particularly _Ardour_, _REAPER_, _Qtractor_, will by default not
  unload VST3 modules after you close the last plugin. This means that the
  associated `yabridge-host.exe` process will keep running until you close the
  project. For REAPER there's an option called
  `Allow complete unload of VST plug-ins` in the `VST` tab of the settings
  dialog to disable this behaviour.
- **Drag-and-drop** from applications running under Wine to X11 does not yet
  work, so you won't be able to drag samples and MIDI files from a plugin to the
  host. At least, not directly. Because Windows applications have to create
  actual files on the disk for drag-and-drop to work, you can keep a file
  manager open and manually drag the generated files into your DAW as a
  workaround. To find out where in `~/.wine` the plugin is creating its files,
  you can use the following command to monitor the Wine prefix for any newly
  created files:

  ```shell
  inotifywait -mre CLOSE_WRITE --format '%w%f' ~/.wine/drive_c
  ```

  **The latest master branch version of yabridge actually does support
  drag-and-drop from plugins running under Wine to native applications.**

- Aside from the above mentioned Wine issue, _drag-and-drop_ to the plugin
  window under **REAPER** doesn't work because of a long standing issue in
  REAPER's FX window implementation. You can use a compatibility option to
  [force drag-and-drop]([editor hosting mode](#compatibility-options)) to work
  around this limitation.

Aside from that, these are some known caveats:

- Most recent **iZotope** plugins don't have a functional GUI in a typical out
  of the box Wine setup because of missing dependencies. Please let me know if
  you know which dependencies are needed for these plugins to render correctly.
- MIDI key labels for VST2 plugins (commonly used for drum machines and
  multisamplers) will not be updated after the host first asks for them since
  VST 2.4 has no way to let the host know that those labels have been updated.
  Deactivating and reactivating the plugin will cause these labels to be updated
  again for the current patch.
- The Cinnamon desktop environment has some quirks with its window management
  that affect yabridge's plugin editor embedding. Most notably some plugins may
  flicker while dragging windows around, and there may be [rendering
  issues](https://github.com/robbert-vdh/yabridge/issues/89) when using multiple
  monitors depending on which screen has been set as primary. Enabling the
  XEmbed [compatibility option](#compatibility-options) may help, but Wine's
  XEmbed implementation also introduces other rendering issues.

There are also some extension features for both VST2.4 and VST3 that have not
been implemented yet because I either haven't seen them used or because we don't
have permission to do so yet. Examples of this are:

- SysEx messages for VST2 plugins. In addition to MIDI, VST 2.4 also supports
  SysEx. I don't know of any hosts or plugins that use this, but please let me
  know if this is needed for something.
- Vendor specific VST2.4 extensions (for instance, for
  [REAPER](https://www.reaper.fm/sdk/vst/vst_ext.php), though most of these
  extension functions will work out of the box without any modifications).
- The [Presonus extensions](https://presonussoftware.com/en_US/developer) to the
  VST3 interfaces. All of these extensions have been superseded by official VST3
  interfaces in later versions of the VST3 SDK.
- VST3 plugin support for
  [ARA](https://www.celemony.com/en/service1/about-celemony/technologies). The
  ARA SDK has recently been [open source](https://github.com/Celemony/ARA_SDK),
  so we can now finally start working on this.

## Troubleshooting common issues

If your problem is not listed here, then feel free to post on the [issue
tracker](https://github.com/robbert-vdh/yabridge/issues) or to ask about it in
the yabridge [Discord](https://discord.gg/pyNeweqadf).

- Using PipeWire's JACK implementation might cause certain plugins to crash.
  PipeWire currently uses rtkit instead of the realtime priorities you would
  normally set up using groups and `/etc/limits.d`, and it will impose a limit
  on the maximum amount of CPU time a realtime process may use at a time. This
  will cause plugins that take a long time to initialize, for instance because
  they're loading a lot of resources, to crash. For the time being the best
  solution for this problem would be to just use JACK2 until PipeWire doesn't
  require rtkit anymore.

- If you have the `WINEPREFIX` environment variable set and you _don't_ want all
  of your plugins to use that specific Wine prefix, then you should unset it to
  allow yabridge to automatically detect Wine prefixes for you.

- If you're seeing errors related to Wine either when running `yabridgectl sync`
  or when trying to load a plugin, then it can be that your installed version of
  Wine is much older than the version that yabridge has been compiled for.
  Yabridgectl will automatically check for this when you run `yabridgectl sync`
  after updating Wine or yabridge. You can also manually verify that Wine is
  working correctly by running one of the VST host applications. Assuming that
  yabridge is installed under `~/.local/share/yabridge`, then running
  `~/.local/share/yabridge/yabridge-host.exe` directly (so _not_
  `wine ~/.local/share/yabridge/yabridge-host.exe`, that won't work) in a
  terminal should print a few messages related to Wine's startup process
  followed by the following line:

  ```
  Usage: yabridge-host.exe <plugin_type> <plugin_location> <endpoint_base_directory>
  ```

  If you're seeing a `002b:err:module:__wine_process_init` error instead, then
  your version of Wine is too old for this version of yabridge and you'll have
  to upgrade your Wine version. Instructions for how to do this on Ubuntu can be
  found on the [WineHQ website](https://wiki.winehq.org/Ubuntu).

  If you're getting a `0024:err:process:exec_process` error, then your Wine
  prefix is set to 32-bit only and it won't be possible to run 64-bit
  applications like `yabridge-host.exe`.

- Sometimes left over Wine processes can cause problems. Run `wineserver -k` to
  terminate Wine related in the current or default Wine prefix.

- If you're using a _lot_ of plugins and you're unable to load any new plugins,
  then you may be running into Xorg's client limit. The exact number of plugins
  it takes for this to happen will depend on your system and the other
  applications running in the background. An easy way to check if this is the
  case would be to try and run `wine cmd.exe` from a terminal. If this prints a
  message about the maximum number of clients being reached (or if you are not
  able to open the terminal at all), then you might want to consider using
  [plugin groups](#plugin-groups) to run multiple instances of your most
  frequently used plugins within a single process.

- If you're using a `WINELOADER` that runs the Wine process under a separate
  namespace while the host is not sandboxed, then you'll have to use the
  `YABRIDGE_NO_WATCHDOG` environment variable to disable the watchdog timer. If
  you know what this means then you probably know what you're doing.

## Performance tuning

Running Windows plugins under Wine should have a minimal performance overhead,
but you may still notice an increase in latency spikes and overall DSP load.
Luckily there are a few things you can do to get rid of most or all of these
negative side effects:

- First of all, you'll want to make sure that you can run programs with realtime
  scheduling. Note that on Arch and Manjaro this does not necessarily require a
  realtime kernel as they include the `PREEMPT` patch set in their regular
  kernels. You can verify that this is working correctly by running `chrt -f 10 whoami`, which should your username, and running `uname -a` should print
  something that contains `PREEMPT` in the output.

- You can also try enabling the `threadirqs` kernel parameter and using which
  can in some situations help with xruns. After enabling this, you can use
  [rtirq](https://github.com/rncbc/rtirq#rtirq) to increase the priority of
  interrupts for your sound card.

- Make sure that you're using the performance frequency scaling governor, as
  changing clock speeds in the middle of a real time workload can cause latency
  spikes.

- The last but perhaps the most important thing you can do is to use a build of
  Wine compiled with Proton's fsync patches. This can improve performance
  significantly when using certain plugins. If you're running Arch or Manjaro,
  then you can use [Tk-Glitch's Wine
  fork](https://github.com/Frogging-Family/wine-tkg-git) for a customizable
  version of Wine with the fsync patches included. Aside from a patched copy of
  Wine you'll also need a supported kernel for this to work. Manjaro's kernel
  supports fsync out of the box, and on Arch you can use the `linux-zen` kernel.
  Finally, you'll have to set the `WINEFSYNC` environment variable to `1` to
  enable fsync. See the [environment configuration](#environment-configuration)
  section below for more information on where to set this environment variable
  so that it gets picked up when you start your DAW.

  You can find a guide to setting these things up on Ubuntu
  [here](https://zezic.github.io/yabridge-benchmark/).

- If you have the choice, the VST3 version of a plugin will likely perform
  better than the VST2 version.

- If the plugin doesn't have a VST3 version, then [plugin
  groups](#plugin-groups) can also greatly improve performance when many
  instances of same VST2 plugin. _VST3 plugins have similar functionality built
  in by design_. Some plugins, like the BBC Spitfire plugins, can share a lot of
  resources between different instances of the plugin. Hosting all instances of
  the same plugin in a single process can in those cases greatly reduce overall
  CPU usage and get rid of latency spikes.

### Environment configuration

This section is relevant if you want to configure environment variables in such
a way that they will be set when you launch your DAW from the GUI instead of
from a terminal. You may want to enable `WINEFSYNC` for fsync support with a
compatible Wine version and kernel, or you may want to change your search `PATH`
to allow yabridge to find the `yabridge-*.exe` binaries if you're using yabridge
directly from the `build` directory. To do this you'll need to change your
_login shell's_ profile, which is different from the configuration loaded during
interactive sessions. And some display manager override your login shell to
always use `/bin/sh`, so you need to be careful to modify the correct file or
else these changes won't work. You can find out your current login shell by
running `echo $SHELL` in a terminal.

- First of all, if you're using GDM, LightDM or LXDM as your display manager
  (for instance if you're using GNOME, XFCE or LXDE), then your display manager
  won't respect your login shell and it will always use `/bin/sh`. In that case
  you will need to add the following line to `~/.profile` to enable fsync:

  ```shell
  export WINEFSYNC=1
  ```

- If you are using the default **Bash** shell and you're not using any of the
  above display managers, then you will want to add the following line to
  `~/.bash_profile` (or `~/.profile` if the former does not exist):

  ```shell
  export WINEFSYNC=1
  ```

- If you are using **Zsh**, then you can add the following line to `~/.zprofile`
  (`~/.zshenv` should also work, but some distros such as Arch Linux overwrite
  the environment after this file has been read):

  ```shell
  export WINEFSYNC=1
  ```

- If you are using **fish**, then you can add the following line to either
  `~/.config/fish/config.fish` or some file in `~/.config/fish/conf.d/`:

  ```shell
  set -gx WINEFSYNC=1
  # Or if you're changing your PATH:
  set -gp fish_user_paths ~/directory/with/yabridge/binaries
  ```

_Make sure to log out and log back in again to ensure that all applications pick
up the new changes._

## Building

To compile yabridge, you'll need [Meson](https://mesonbuild.com/index.html) and
the following dependencies:

- GCC 10+[\*](#building-ubuntu-18.04)
- A Wine installation with `winegcc` and the development headers. The latest
  commits contain a workaround for a winelib [compilation
  issue](https://bugs.winehq.org/show_bug.cgi?id=49138) with Wine 5.7+.
- Boost version 1.66 or higher[\*](#building-ubuntu-18.04)
- libxcb

The following dependencies are included in the repository as a Meson wrap:

- [bitsery](https://github.com/fraillt/bitsery)
- [function2](https://github.com/Naios/function2)
- [tomlplusplus](https://github.com/marzer/tomlplusplus)
- Version 3.7.2 of the [VST3 SDK](https://github.com/robbert-vdh/vst3sdk) with
  some [patches](https://github.com/robbert-vdh/yabridge/blob/master/tools/patch-vst3-sdk.sh)
  to allow Winelib compilation

The project can then be compiled with the command below. You can remove or
change the unity size argument if building takes up too much RAM, or you can
disable unity builds completely by getting rid of `--unity=on` at the cost of
slightly longer build times.

```shell
meson setup build --buildtype=release --cross-file=cross-wine.conf --unity=on --unity-size=1000
ninja -C build
```

After you've finished building you can follow the instructions under the
[usage](#usage) section on how to set up yabridge.

<sup id="building-ubuntu-18.04">
  *The versions of GCC and Boost that ship with Ubuntu 18.04 by default are too
  old to compile yabridge. If you do wish to build yabridge from scratch rather
  than using the <a
  href="https://github.com/robbert-vdh/yabridge/actions?query=workflow%3A%22Automated+builds%22+branch%3Amaster">prebuilt
  binaries</a>, then you should take a look at the <a
  href="https://github.com/robbert-vdh/docker-yabridge/blob/master/bionic/Dockerfile">docker
  image</a> used when building yabridge on Ubuntu 18.04 for on overview of what
  would need to be installed to compile on Ubuntu 18.04.
</sup>

### 32-bit bitbridge

It is also possible to compile a host application for yabridge that's compatible
with 32-bit plugins such as old SynthEdit plugins. This will allow yabridge to
act as a bitbridge, allowing you to run old 32-bit only Windows VST2 plugins in
a modern 64-bit Linux VST host. For this you'll need to have installed the 32
bit versions of the Boost and XCB libraries. This can then be set up as follows:

```shell
# Enable the bitbridge on an existing build
meson configure build -Dwith-bitbridge=true
# Or configure a new build from scratch
meson setup build --buildtype=release --cross-file cross-wine.conf -Dwith-bitbridge=true

ninja -C build
```

This will produce four files called `yabridge-host-32.exe`,
`yabridge-host-32.exe.so`, `yabridge-group-32.exe` and
`yabridge-group-32.exe.so`. Yabridge will detect whether the plugin you're
trying to load is 32-bit or 64-bit, and will run either the regular version or
the `*-32.exe` variant accordingly.

### 32-bit libraries

It also possible to build 32-bit versions of yabridge's libraries, which would
let you use both 32-bit and 64-bit Windows VST2 and VST3 plugins from a 32-bit
Linux plugin host. This is mostly untested since 32-bit only Linux applications
don't really exist anymore, but it should work! The build system will still
assume you're compiling from a 64-bit system, so if you're compiling on an
actual 32-bit system you would need to comment out the 64-bit `yabridge-host`
and `yabridge-group` binaries in `meson.build`:

```shell
meson setup build --buildtype=release --cross-file=cross-wine.conf --unity=on --unity-size=1000 -Dwith-bitbridge=true -Dbuild.cpp_args='-m32' -Dbuild.cpp_link_args='-m32'
ninja -C build
```

Like the above commands, you might need to tweak the unity size based on the
amount of system memory available. See the CI build definitions for some
examples on how to add static linking in the mix if you're going to run this
version of yabridge on some other machine.

## Debugging

Wine's error messages and warning are usually very helpful whenever a plugin
doesn't work right away. However, with some VST hosts it can be hard read a
plugin's output. To make it easier to debug malfunctioning plugins, yabridge
offers these two environment variables to control yabridge's logging facilities:

- `YABRIDGE_DEBUG_FILE=<path>` allows you to write yabridge's debug messages as
  well as all output produced by the plugin and by Wine itself to a file. For
  instance, you could launch your DAW with
  `env YABRIDGE_DEBUG_FILE=/tmp/yabridge.log <daw>`, and then use
  `tail -F /tmp/yabridge.log` to keep track of the output. If this option is not
  present then yabridge will write all of its debug output to STDERR instead.
- `YABRIDGE_DEBUG_LEVEL={0,1,2}` allows you to set the verbosity of the debug
  information. Each level increases the amount of debug information printed:

  - A value of `0` (the default) means that yabridge will only log the output
    from the Wine process and some basic information about the
    environment, the configuration and the plugin being loaded.
  - A value of `1` will log detailed information about most events and function
    calls sent between the VST host and the plugin. This filters out some noisy
    events such as `effEditIdle()` and `audioMasterGetTime()` since those are
    sent multiple times per second by for every plugin.
  - A value of `2` will cause all of the events to be logged without any
    filtering. This is very verbose but it can be crucial for debugging
    plugin-specific problems.

  More detailed information about these debug levels can be found in
  `src/common/logging.h`.

Wine's own [logging facilities](https://wiki.winehq.org/Debug_Channels) can also
be very helpful when diagnosing problems. In particular the `+message`,
`+module` and `+relay` channels are very useful to trace the execution path
within the loaded VST plugin itself.

### Attaching a debugger

To debug the plugin you can just attach gdb to the host as long as any
sandboxing or out of process hosting is disabled (or you'll have to wrap around
that host process). Debugging the Wine plugin host is a bit more difficult. Wine
comes with a GDB proxy for winedbg, but it requires a little bit of additional
setup and it doesn't support arguments containing spaces. To make this a bit
easier, yabridge includes winedbg support behind a build option. You can enable
this using:

```shell
meson configure build --buildtype=debug -Dwith-winedbg=true
```

Currently winedbg's normal GDB proxy is broken, so this option will start a
remote GDB server that you have to connect to. You can use `gdb build/yabridge-host.exe.so` to start GDB, and then use the GDB `target` command
printed to STDERR or `$YABRIDGE_DEBUG_FILE` to start the debugging session. Note
that plugin names with spaces in the actual `.dll` or `.vst3` file name will
have to be renamed first for this approach to work.
