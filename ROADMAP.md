# Roadmap

Yabridge's VST2 and VST3 bridging are feature complete and should work great,
but there are still some other features that may be worth implementing. This
page lists some of those.

# Short-ish term

- [ARA](https://www.celemony.com/en/service1/about-celemony/technologies)
  support for VST3 plugins. The ARA SDK has recently been [open
  source](https://github.com/Celemony/ARA_SDK), so we can now finally start
  working on this.

# Longer term

- [CLAP](https://github.com/free-audio/clap) plugin bridging. Implementing this
  only makes sense once Windows-only CLAP plugins start appearing.
- An easier [updater](https://github.com/robbert-vdh/yabridge/issues/51) through
  a new `yabridgectl update` command for distros that don't package yabridge.

# For a major release

- Completely remove the now deprecated symlink-based installation method from
  yabridgectl.
- Replace the use of `notify-send` for notifications with using `libdbus`
  directly. Most systems will have both available by default, but some less
  common distros split `notify-send` from the rest of the `libnotify` package.
- Possibly combine the `yabridge-host` and `yabridge-group` binaries to save
  some disk space as 95% of their code overlaps.
- Consider adding an option for yabridgectl to set up VST2 plugins in `~/.vst`.
  As discussed in a couple places already doing so would come with a number of
  downsides and potential pitfalls so this may not happen.
- Consider chainloading the real `libyabridge-vst2.so` and `libyabridge-vst3.so`
  files from smaller stub libraries. This would avoid having to rerun
  `yabridgectl sync` after an upgrade, and it would save disk space on systems
  without support for reflinks.
- Consider replacing Boost.Asio with the standalone Asio library,
  Boost.Filesystem with a similar headers only library (as `std::filesystem`
  doesn't work under `wineg++`) and all other components with custom wrappers
  around Linux and Windows APIs. Getting rid of the Boost.Filesystem dependency
  would be nice as it makes packaging easier, but it would require a lot of work
  to make it happen.

# Somewhere in the future, possibly

- REAPER's vendor specific [VST2.4](https://www.reaper.fm/sdk/vst/vst_ext.php)
  and
  [VST3](https://github.com/justinfrankel/reaper-sdk/blob/main/sdk/reaper_vst3_interfaces.h)
  extensions.
- [Presonus' extensions](https://presonussoftware.com/en_US/developer) to the
  VST3 interfaces. All of these extensions have been superseded by official VST3
  interfaces in later versions of the VST3 SDK, so it's unlikely that there are
  many plugins that still rely on these older extensions.
