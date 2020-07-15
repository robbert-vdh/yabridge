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

Yabrdgectl will need to know where it can find `libyabridge.so`. By default it
will look for it in both `~/.local/share/yabridge` (the recommended installation
directory when using the prebuilt binaries) and in `/usr/lib`. You can use the
command below to override this behaviour and to use a custom installation
directory instead.

```shell
yabridgectl set --path=<path/to/directory/containing/yabridge/files>
```

### Installation methods

By default, yabridgectl will use the copy-based installation method for yabridge
since this installation method works with any VST host. If you are using a DAW
that supports individually sandboxed plugins such as Bitwig Studio, then you can
choose between using copies and symlinks using the command below.

```shell
yabridgectl set --method=<copy|symlink>
```

### Managing directories

Yabridgectl manage Windows VST plugin install locations for you. To add, remove
and list directories, or to list the plugins currently installed inside one of
those directories, you can use the command below.

```shell
# Add a location containing plugins
# For instance, use the below command for the most common VST2 plugin directory
# yabridgectl add "$HOME/.wine/drive_c/Program Files/Steinberg/VstPlugins"
yabridgectl add <path/to/plugins>
# Remove a plugin location, this will ask you if you want to remove any leftover yabridge files
yabridgectl rm <path/to/plugins>
# List the current plugin locations
yabridgectl list
# Show the current settings and the installation status for all plugins found
yabridgectl status
```

### Installing and updating

Finally you can tell yabridgectl to set up or update yabridge for all of your
plugins at once using the commands below. By default yabridgectl will warn you
if it finds leftover `.so` files, but it will only delete those when ran using
the `--prune` option.

```shell
# Set up copies or symlinks of yabridge for all plugins under the watched directories
yabridgectl sync
# Set up yabridge, and also remove any '.so' still leftover after removing a plugin
yabridgectl sync --prune
```

## Alternatives

If you want to script your own installation behaviour and don't feel like using
yabridgectl, then you could use one of the below bash snippets as a base. This
approach is slightly less robust and does not do any problem detection or status
reporting, but it will get you started.

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

## Building

After installing [Rust](https://rustup.rs/), simply run the command below to
compile and run:

```shell
cargo run --release
```
