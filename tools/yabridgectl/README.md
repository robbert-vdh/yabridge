# yabridgectl

A small, optional utility to help set up and update
[yabridge](https://github.com/robbert-vdh/yabridge) for several directories at
once.

## Usage

Yabridgectl can be downloaded from the [releases
section](https://github.com/robbert-vdh/yabridge/releases) on GitHub and can run
from anywhere. All of the information below can also be found through
`yabridgectl --help`.

### Yabridge path

Yabridgectl will need to know where it can find `libyabridge.so`. By default it
will search for it in both `~/.local/share/yabridge` (the recommended
installation directory when using the prebuilt binaries) and in `/usr/lib`. You
can use the command below to override this behaviour and to use a custom
installation directory instead.

```shell
yabridgectl set --path=<path/to/directory/containing/yabridge/files>
```

### Installation methods

Yabridge can be set up using either copies or symlinks. By default, yabridgectl
will use the copy-based installation method since this will work with any VST
host. If you are using a DAW that supports individually sandboxed plugins such
as Bitwig Studio, then you can choose between using copies and symlinks using
the command below. Make sure to rerun `yabridgectl sync` after changing this
setting.

```shell
yabridgectl set --method=<copy|symlink>
```

### Managing directories

Yabridgectl can manage multiple Windows VST plugin install locations for you. To
add, remove and list directories, you can use the commands below. The status
command will show you yabridgectl's current settings and the installation status
for all of your plugins.

```shell
# Add a directory containing plugins
# Use the command from the next line to add the most common VST2 plugin directory
# yabridgectl add "$HOME/.wine/drive_c/Program Files/Steinberg/VstPlugins"
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
plugins at once using the commands below. Yabridgectl will warn you if it finds
unrelated `.so` files that may have been left after uninstalling a plugin. You
can rerun the sync command with the `--prune` option to delete those files. If
you are using the default copy-based installation method, it will also verify
that your search `PATH` has been set up correctly so you can get up and running
faster.

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
yabridgectl, then you could use one of the below bash snippets instead. This
approach is slightly less robust and does not perform any problem detection or
status reporting, but it will get you started.

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
