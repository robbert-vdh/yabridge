# yabridge

Yet Another way to use Windows VST2 plugins in Linux VST hosts.

## TODOs

There are a few things that should be done before making this public, including:

- Document and improve the installation and updating procedure.
- Finish documenting the project setup and the way communication works. In
  particular we're missing the wait void pointers in the event dispatchers are
  handled and how the AEffect struct gets synchronized. I should probably also
  rewrite some parts of it to make it clearer.
- Document what this has been tested on and what does or does not work.
- Document wine32 support.
- Forward updates from the Windows VST plugin's `AEffect` struct, if that's a
  thing.
- Fix `processReplacing` forwarding.
- Implement GUIs.
- The `audioMasterUpdateDisplay` and `audioMasterWantMidi` hsot callbacks
  sometimes cause Carla to crash, but other times they do not. Not really sure
  what the pattern here is.
- Check if we need special handling for the `effGetChunk` and `effSetChunk`
  events.
- Mention precompiled binaries and building from source in the installation
  section.
- Add proper debugging support activated using an environment variable.
  - Write all stdout and stderr output from the plugin to a temporary file so it
    can be inspected when using a host such as Bitwig that hides this by
    default.
  - Catch exceptions during initialization and print them to stderr.

## Usage

There are two ways to use yabridge.

### Symlinking (recommended)

The recommended way to use yabridge is through symbolic links. This allows you
to update yabridge for all of your plugins in one go, and it also avoids having
to install it globally.

You can either use the precompiled binaries from the GitHub releases section, or
you could build yabridge directly from source. If you use the precompiled
binaries, then simply extract them to `~/.local/share/yabridge` or any other
place in your home directory. If you choose to build from source, then you can
directly use the binaries from the `build/` directory. For the section below I'm
going to assume you've placed the files in `~/.local/share/yabridge`.

To set up yabridge for a VST plugin called
`~/.wine/drive_c/Program Files/Steinberg/VstPlugins/plugin.dll`, simply create a
symlink from `~/.local/share/yabridge/libyabridge.so` to
`~/.wine/drive_c/Program Files/Steinberg/VstPlugins/plugin.so` like so:

```shell
ln -s ~/.local/share/yabridge/libyabridge.so "$HOME/.wine/drive_c/Program Files/Steinberg/VstPlugins/plugin.so"
```

For instance, if you wanted to set up yabridge for any of the VST plugins under
`~/.wine/drive_c/Program Files/Steinberg/VstPlugins`, you could do something
like this:

```shell
find "$HOME/.wine/drive_c/Program Files/Steinberg/VstPlugins" -type f -iname '*.dll' -print0 \
  | sed -z 's/\.dll$/.so/' \
  | xargs -0 -n1 ln -sf ~/.local/share/yabridge/libyabridge.so
```

### Copying

It's also possible to use yabridge by making copies of `libyabridge.so` instead
of creating symlinks. This is not recommended as it makes updating a hassle. If
you choose to do this, then you'll have to make sure `yabridge-host.exe` and
`yabridge-host.exe.so` are somewhere in your search path as otherwise yabridge
won't know where to find them. Either copy them to `/usr/local/bin` (not
recommended) or to `~/.local/bin` and make sure that the directory is in your
`PATH` environment variable.

## Building

To compile yabridge, you'll need [Meson](https://mesonbuild.com/index.html) and
the following dependencies:

- gcc (tested using GCC 9.2)
- A Wine installation with `wiengcc` and the development headers.
- Boost

The following dependencies are included as a Meson wrap:

- bitsery

The project can then be compiled as follows:

```shell
meson setup --buildtype=release --cross-file cross-wine64.conf build
ninja -C build
```

When developing or debugging yabridge you can change the build type to either
`debug` or `debugoptimized` to enable debug symbols and optionally also disable
optimizations.

## Debugging

Wine's error messages and warning are typically very helpful whenever a plugin
doesn't work right away. Sadly this information is not always available. For
instance Bitwig hides a plugin's STDOUT and STDERR streams from you. To make it
easier to debug malfunctioning plugins, yabridge offers two environment
variables:

- `YABRIDGE_DEBUG_FILE=<path>` allows you to write the Wine VST host's STDOUT
  and STDERR messages to a file. For example, you could launch your DAW with
  `env YABRIDGE_DEBUG_FILE=/tmp/yabridge.log <daw>`,
  and then use `tail -F /tmp/yabridge.log` to keep track of that file. If this
  option is not absent then yabridge will write its debug messages to STDERR
  instead.
- `YABRIDGE_DEBUG_LEVEL={0,1}` allows you to set the verbosity of the debug
  information. Every level increases the verbosity of the debug information:

  - A value of `0` (the default) means that yabridge will only output messages
    from the Wine process and some basic information such as the plugin being
    loaded and the wineprefix being used.
  - A value of `1` will log information about all events and function calls
    being sent between the VST host and the plugin. This can be very verbose but
    it makes it easier to see if yabridge is handling things incorrectly.

  More detailed information about these levels can be found in
  `src/common/logging.h`.

## Rationale

I started this project because the alternatives were either unmaintained, not
self-contained or very difficult to work with. With this implementation I'd like
to prioritize maintainability and correctness, even if it would cause slightly
more overhead than a more optimized solution would. Please let me know if you
have any suggestions on how to improve this!

## Architecture

The project consists of two components, a Linux native VST plugin
(`libyabridge.so`) and a VST host that runs under Wine
(`yabridge-host.exe`/`yabridge-host.exe.so`). I'll refer to a copy or symlink of
`libyabridge.so` as _the plugin_, the native Linux VST host that's hosting the
plugin as _the native VST host_, the Wine VST host that's hosting a Windows
`.dll` file as _the Wine VST host_, and the Windows VST plugin that's loaded in
the Wine VST host is simply the _Windows VST plugin_. The whole process works as
follows:

1. Some copy of or a symlink to `libyabridge.so` gets loaded as a VST plugin in
   a Linux VST host. This file should have been renamed to match a Windows VST
   plugin `.dll` file in the same directory. For instance, if there's a
   `Serum_x64.dll` file you'd like to bridge, then `libyabridge.so` should be
   renamed to `Serum_x64.so`.
2. The plugin first attempts to locate:

   - The location of `yabridge-host.exe`. For this it will first search for the
     file either alongside plugin. This is useful for development, as it allows
     you to use a symlink to `libyabridge.so` from the build directory causing
     yabridge to use the corresponding `yabridge-host.exe` from the same build
     directory. If this file can't be found, it will fall back to searching
     through the search path.
   - The wine prefix plugin is located in
   - The corresponding Windows VST plugin `.dll` file.

3. The plugin then sets up a Unix domain socket endpoint to communicate with the
   Wine VST host somewhere in a temporary directory. I chose to use Unix domain
   sockets rather than shared memory to avoid having to do manual
   synchronization and because they have very low overhead. This also makes it
   possible to send arbitrarily large data without having to split it into
   chunks first, which is useful for transmitting audio and preset data.
4. The plugin launches the Wine VST host in the detected wine prefix, passing
   the name of the `.dll` file it should be loading and the path to the Unix
   domain socket that was just created.
5. Communication gets set up using multiple sockets over the same end point.
   This allows us to use blocking read operations while handling a certain event
   type to avoid receiving messages out of order. The following types of events
   get assigned a socket:

   - Calls from the native VST host to the plugin's `dispatch()` function. These
     get forwarded to the Windows VST plugin through the Wine VST host.
   - Host callback calls from the Windows VST plugin loaded into the Wine VST
     host through the `audioMasterCallback` function. These get forwarded to the
     native VST host through the plugin.
   - Calls from the native VST host to the plugin's `getParameter()` and
     `setParameter()` functions. Both functions get forwarded to the Windows VST plugin
     through the Wine VST host using a single socket.
   - Calls from the native VST host to the plugin's `process()` and
     `processReplacing()` functions. Both functions get forwarded to the Windows
     VST plugin through the Wine VST host using a single socket.

   The first step when passing through any of these function calls over a socket
   is to serialize the function's parameters as binary data. Both request and
   the corresponding response objects for all of these function calls can be
   found in `src/common/communication.h`, along with functions to read and write
   these objects over streams and sockets. The actual binary serialization is
   handled using [bitsery](https://github.com/fraillt/bitsery).

6. The Wine VST host loads the Windows VST plugin and starts forwarding messages
   over the sockets described above.
7. After the Windows VST plugin has started loading we will forward all values
   from the plugin's `AEffect` struct to the Linux native VST plugin. After this
   point the plugin will stop blocking and has finished loading.

   TODO: Do plugins update their `AEffect` struct update itself after
   initialization? For instance to change the number of parameters. Is there any
   way to catch this other than checking for updates ourselves?
