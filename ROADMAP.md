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

- An easier [updater](https://github.com/robbert-vdh/yabridge/issues/51) through
  a new `yabridgectl update` command for distros that don't package yabridge.

# Somewhere in the future, possibly

- CLAP audio thread pool support. Implementing this efficiently is less than
  trivial, and there are currently no plugins that can benefit from it.
- REAPER's vendor specific [VST2.4](https://www.reaper.fm/sdk/vst/vst_ext.php)
  and
  [VST3](https://github.com/justinfrankel/reaper-sdk/blob/main/sdk/reaper_vst3_interfaces.h)
  extensions.
- [Presonus' extensions](https://presonussoftware.com/en_US/developer) to the
  VST3 interfaces. All of these extensions have been superseded by official VST3
  interfaces in later versions of the VST3 SDK, so it's unlikely that there are
  many plugins that still rely on these older extensions.
