# yabridgectl

A small, optional utility to set up
[yabridge](https://github.com/robbert-vdh/yabridge) for several directories at
once and to keep them updated.

## Usage

Yabridgectl can be downloaded from the GitHub releases section, and it can run
from anywhere. All of the information below can also be found by running
`yabridgectl --help`.

### Yabridge path

Yabrdgectl will need to know where it can find `libyabridge.so`. By default it
will look for it in both `~/.local/share/yabridge` (the recommended installation
directory when using the prebuilt binaries) and in `/usr/lib` (used when using
the AUR packages). You can use the command below to override this behaviour and
to use a custom installation directory instead.

```shell
yabridgectl set --path=<path/to/directory/containing/yabridge>
```

### Installation modes

By default, yabridgectl will use the copy-based installation method for yabridge
since this installation method works everywhere. If you are using a DAW that
supports individually sandboxed plugins, then you can choose between using
copies and symlinks using the command below.

```shell
yabridgectl set --mode=<copy|symlink>
```

### Managing directories

Yabridgectl manage Windows VST plugin install locations for you. To add, remove
and list directories, or to list the plugins currently installed inside one of
those directories, you can use the command below.

```shell
# Add a directory to watch
# For instance, use the below command for the most common VST2 plugin directory
# yabridgectl add "$HOME/.wine/drive_c/Program Files/Steinberg/VstPlugins"
yabridgectl add <path/to/plugins>
# No longer watch a directory, this will ask you if you want to remove any leftover yabridge files
yabridgectl rm <path/to/plugins>
# List the currently watched directories
yabridgectl list
# Show the current settings and the installation status for all plugins in the watched directories
yabridgectl status
```

### Installing and updating

Finally you can set up or update yabridge for all of your plugins at once using
the command below. By default yabridgectl will warn you if it finds `.so` files
without an accompanying `.dll` file, but it will only delete those when using
the `--prune` option.

```shell
# Set up copies or symlinks of yabridge for all plugins under the watched directories
yabridgectl sync
# Set up yabridge, and also remove any '.so' still leftover after removing a plugin
yabridgectl sync --purge
```

## Building

After installing [Rust](https://rustup.rs/), simply run the below to compile and
run:

```shell
cargo run --release
```
