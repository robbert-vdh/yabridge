# yabridgectl

A small, optional utility to help set up and update
[yabridge](https://github.com/robbert-vdh/yabridge) for several directories at
once.

## Usage

Yabridgectl can be downloaded from the [releases
page](https://github.com/robbert-vdh/yabridge/releases) on GitHub and can run
from anywhere. All of the information below can also be found through
`yabridgectl --help`.

### Yabridge path

Yabridgectl will need to know where it can find `libyabridge-vst2.so` and
`libyabridge-vst3.so`. By default it will search for it in both
`~/.local/share/yabridge` (the recommended installation directory when using the
prebuilt binaries), in `/usr/lib` and in `/usr/local/lib`. You can use the
command below to override this behaviour and to use a custom installation
directory instead.

```shell
yabridgectl set --path=<path/to/directory/containing/yabridge/files>
```

### Installation methods

Yabridge can be set up using either copies or symlinks. By default, yabridgectl
will use the copy-based installation method since this will work with any host,
and there's usually no reason to use symlinks anymore. If you are using a DAW
that supports individually sandboxed plugins such as Bitwig Studio, then you can
choose between using copies and symlinks using the command below. Make sure to
rerun `yabridgectl sync` after changing this setting.

```shell
yabridgectl set --method=<copy|symlink>
```

### Managing directories

Yabridgectl can manage multiple Windows plugin install locations for you.
Whenever you run `yabridgectl sync` it will search these directories for VST2
plugins and VST3 modules. To add, remove and list directories, you can use the
commands below. The status command will show you yabridgectl's current settings
and the installation status for all of your plugins.

```shell
# Add a directory containing plugins
# Use the command from the next line to add the most common VST2 plugin directory:
# yabridgectl add "$HOME/.wine/drive_c/Program Files/Steinberg/VstPlugins"
# VST3 plugins are located here:
# yabridgectl add "$HOME/.wine/drive_c/Program Files/Common Files/VST3"
yabridgectl add <path/to/plugins>
# Remove a plugin location, this will ask you if you want to remove any leftover files from yabridge
yabridgectl rm <path/to/plugins>
# List the current plugin locations
yabridgectl list
# Show the current settings and the installation status for all of your plugins
yabridgectl status
```

### Installing and updating

Lastly you can tell yabridgectl to set up or update yabridge for all of your
VST2 and VST3 plugins at the same time using the commands below. Yabridgectl
will warn you if it finds unrelated `.so` files that may have been left after
uninstalling a plugin, or if it finds any unknown VST3 plugins in
`~/.vst3/yabridge`. You can rerun the sync command with the `--prune` option to
delete those files. If you are using the default copy-based installation method,
it will also verify that your search `PATH` has been set up correctly so you can
get up and running faster.

```shell
# Set up or update yabridge for all plugins found under the plugin locations
yabridgectl sync
# Set up or update yabridge, and also remove any leftover .so files
yabridgectl sync --prune
# Set up yabridge or update for all plugins, even if it would not be necessary
yabridgectl sync --force
```

## Alternatives

If you want to script your own installation behaviour and don't feel like using
yabridgectl, then you could use one of the below bash snippets instead to set up
yabridge for VST2 plugins. This approach is slightly less robust and does not
perform any problem detection or status reporting, but it will get you started.
Doing the same thing for VST3 plugins is much more complicated and it involves
[merged
bundle](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/vst3loc.html#mergedbundles)
with the Windows VST3 module symlinked in, so it's recommended to have
yabridgectl do that for you.

```shell
# For use with symlinks
yabridge_home=$HOME/.local/share/yabridge
plugin_dir="$HOME/.wine/drive_c/Program Files/Steinberg/VstPlugins"

find -L "$plugin_dir" -type f -iname '*.dll' -print0 |
  xargs -0 -P$(nproc) -I{} bash -c "(winedump -j export '{}' | grep -qE 'VSTPluginMain|main|main_plugin') && printf '{}\0'" |
  sed -z 's/\.dll$/.so/' |
  xargs -0 -n1 ln -sf "$yabridge_home/libyabridge.so"

# For use with copies
yabridge_home=$HOME/.local/share/yabridge
plugin_dir="$HOME/.wine/drive_c/Program Files/Steinberg/VstPlugins"

find -L "$plugin_dir" -type f -iname '*.dll' -print0 |
  xargs -0 -P$(nproc) -I{} bash -c "(winedump -j export '{}' | grep -qE 'VSTPluginMain|main|main_plugin') && printf '{}\0'" |
  sed -z 's/\.dll$/.so/' |
  xargs -0 -n1 cp "$yabridge_home/libyabridge.so"
```

## Building from source

After installing [Rust](https://rustup.rs/), simply run the command below to
compile and run:

```shell
cargo run --release
```
