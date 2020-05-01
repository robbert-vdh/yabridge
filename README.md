# yabridge

Yet Another way to use Windows VST plugins in Linux VST hosts. Yabridge
seamlessly supports running both 64-bit Windows VST2 plugins as well as 32-bit
Windows VST2 plugins in a 64-bit Linux VST host. This project aims to be as
transparent as possible to achieve the best possible plugin compatibility while
also staying easy to debug and maintain.

## TODOs

There are a few things that should be done before releasing this, including:

- Do some final refactoring/clean up. There are a few small todos left for
  things that could be a made little bit prettier.
- Add missing details if any to the architecture section.
- Rewrite parts of this readme.
- Briefly verify that this also works fine in Reaper and Ardour.

## Tested with

yabridge has been primarily tested under and verified to work correctly with:

- Bitwig Studio 3.1 and the beta releases of 3.2
- Carla 2.1
- Wine Staging 5.5 and 5.6 (the wine-staging-5.7-1 package currently in Arch and
  Manjaro's repositories is broken because of a regression in application
  startup behavior)

Please let me know if there are any issues with other VST hosts.

## Usage

There are two ways to use yabridge.

### Symlinking (recommended)

The recommended way to use yabridge is through symbolic links. This allows you
to update yabridge for all of your plugins in one go, as well as avoiding having
to install it globally.

You can either use the precompiled binaries from the GitHub releases section, or
you could build yabridge directly from source. If you use the precompiled
binaries, then you can simply extract them to `~/.local/share/yabridge` or to
any other location in your home directory. If you choose to build from source,
then you can use the compiled binaries directly from the `build/` directory. For
the section below I'm going to assume you've placed the files in
`~/.local/share/yabridge`.

To set up yabridge for a VST plugin called
`~/.wine/drive_c/Program Files/Steinberg/VstPlugins/plugin.dll`, simply create a
symlink from `~/.local/share/yabridge/libyabridge.so` to
`~/.wine/drive_c/Program Files/Steinberg/VstPlugins/plugin.so` like so:

```shell
ln -s ~/.local/share/yabridge/libyabridge.so "$HOME/.wine/drive_c/Program Files/Steinberg/VstPlugins/plugin.so"
```

As an example, if you wanted to set up yabridge for any of the VST plugins under
`~/.wine/drive_c/Program Files/Steinberg/VstPlugins`, you could run the
following script in Bash. This will also skip any `.dll` files that are not
actually VST plugins.

```shell
find "$HOME/.wine/drive_c/Program Files/Steinberg/VstPlugins" -type f -iname '*.dll' -print0 |
  xargs -0 -P8 -I{} bash -c "(winedump -j export '{}' | grep -qE 'VSTPluginMain|main|main_plugin') && printf '{}\0'" |
  sed -z 's/\.dll$/.so/' |
  xargs -0 -n1 ln -sf ~/.local/share/yabridge/libyabridge.so
```

### Copying

It is also possible to use yabridge by creating copies of `libyabridge.so`
instead of making symlinks. This is not recommended as it makes updating a
hassle, but it may be required if your host has issues using symlinks. If you
choose to do this, then you'll have to make sure `yabridge-host.exe` and
`yabridge-host.exe.so` are somewhere in your search path as otherwise yabridge
won't be able to find them. Either copy them to `/usr/local/bin` (not
recommended) or to `~/.local/bin` and make sure that the directory is in your
`PATH` environment variable.

## Runtime dependencies and known issues

Any VST2 plugin should function out of the box, although some plugins will need
some additional dependencies for their GUIs to work correctly. Notable examples
include:

- **Serum** requires you to disable `d2d1.dll` in `winecfg` and to install
  `gdiplus` through `winetricks`.

Aside from that, these are some known caveats:

- Plugins by **KiloHearts** have file descriptor leaks while esync is enabled,
  or at least they have on my machine. This sadly cannot be fixed in yabridge.
  Simply unset `WINEESYNC` while using yabridge if this is an issue.
- Most recent **iZotope** plugins don't have a functional GUI in a typical Wine
  setup. This is sadly something that can't be fixed on yabridge's side and I
  have not yet been able to figure out a way to reliably make these plugins
  work.
- Dragging and dropping files onto plugin editors works, but the editor does not
  always show visual updates while dragging. This needs further investigation.

There are also some VST2.4 extension features that haven't implemented yet
because I haven't needed them myself. Let me know if any of these features are
required for a certain plugin or plugin host:

- Double precision audio (`processDoubleReplacing`).
- Vendor specific extensions (for instance, for
  [Reaper](https://www.reaper.fm/sdk/vst/vst_ext.php)).

## Building

To compile yabridge, you'll need [Meson](https://mesonbuild.com/index.html) and
the following dependencies:

- gcc (tested using GCC 9.2)
- A Wine installation with `wiengcc` and the development headers.
- Boost
- xcb

The following dependencies are included in the repository as a Meson wrap:

- bitsery

The project can then be compiled as follows:

```shell
meson setup --buildtype=release --cross-file cross-wine.conf build
ninja -C build
```

### 32-bit bitbridge

It's also possible to compile a 32-bit host application for yabridge that's
compatible with 32 bit plugins such as old SynthEdit plugins. This will allow
yabridge to act as a bitbirdge, allowing you to run old 32-bit only Windows VST2
plugins in a modern 64-bit Linux VST host. For this you'll need to have
installed the 32 bit versions of the Boost and XCB libraries. This can be
set up as follows:

```shell
# On an existing build
meson configure build -Duse-bitbridge=true
# Configure a new build from scratch
meson setup --buildtype=release --cross-file cross-wine.conf -Duse-bitbridge=true build

ninja -C build
```

This will produce two files called `yabridge-host-32.exe` and
`yabridge-host-32.exe.so`. Yabridge will detect whether the plugin you're trying
to load is 32-bit or 64-bit, and will run either `yabridge-host.exe` or
`yabridge-host-32.exe` accordingly.

## Debugging

Wine's error messages and warning are generally very helpful whenever a plugin
doesn't work right away. Sadly this information is not always available. Bitwig,
for instance, hides a plugin's STDOUT and STDERR streams from you. To make it
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

  - A value of `0` (the default) means that yabridge will only write messages
    from the Wine process and some basic information such as the plugin being
    loaded and the wineprefix being used.
  - A value of `1` will log information about most events and function calls
    sent between the VST host and the plugin. This filters out some events such
    as `effEditIdle()` and `audioMasterGetTime()` since those are sent tens of
    times per second by for every plugin.
  - A value of `2` will cause all of the events to be logged, including the
    events mentioned above. This can be very verbose but it can be crucial for
    debugging plugin-specific problems.

  More detailed information about these levels can be found in
  `src/common/logging.h`.

Wine's own [logging facilities](https://wiki.winehq.org/Debug_Channels) can also
be very helpful when diagnosing problems. In particular the `message` and
`relay` channels are very useful to trace the execution path within the loading
VST plugin itself.

### Attaching a debugger

When needed, I found the easiest way to debug the plugin to be to load it in an
instance of Carla with gdb attached:

```shell
env YABRIDGE_DEBUG_FILE=/tmp/yabridge.log YABRIDGE_DEBUG_LEVEL=1 carla --gdb
```

Doing the same thing for the Wine VST host can be a bit tricky. You'll need to
launch winedbg in a seperate detached terminal emulator so it doesn't terminate
together with the plugin, and winedbg can be a bit picky in the arguments it
accepts. I've already set this up behind a feature flag for use in KDE Plasma.
Other desktop environments and window managers will require some slight
modifications in `src/plugin/host-bridge.cpp`. To enable this, simply run:

```shell
meson configure build --buildtype=debug -Duse-winedbg=true
```

## Architecture

The project consists of two components, a Linux native VST plugin
(`libyabridge.so`) and a VST host that runs under Wine
(`yabridge-host.exe`/`yabridge-host.exe.so`). I'll refer to a copy of or a
symlink to `libyabridge.so` as _the plugin_, the native Linux VST host that's
hosting the plugin as _the native VST host_, the Wine VST host that's hosting a
Windows `.dll` file as _the Wine VST host_, and the Windows VST plugin that's
loaded in the Wine VST host is simply the _Windows VST plugin_. The whole
process works as follows:

1. Some copy of or a symlink to `libyabridge.so` gets loaded as a VST plugin in
   a Linux VST host. This file should have been renamed to match a Windows VST
   plugin `.dll` file in the same directory. For instance, if there's a
   `Serum_x64.dll` file you'd like to bridge, then there should be a symlink to
   `libyabridge.so` named `Serum_x64.so`.
2. The plugin first attempts to locate:

   - The location of `yabridge-host.exe`. For this it will first search for the
     file either alongside `libyabridge.so`. This is useful for development, as
     it allows you to use a symlink from the build directory to cause yabridge
     to use the `yabridge-host.exe` from that same build directory. If this file
     can't be found, then it will fall back to searching through the search path.
   - The wine prefix plugin is located in.
   - The corresponding Windows VST plugin `.dll` file.

3. The plugin then sets up a Unix domain socket endpoint to communicate with the
   Wine VST host somewhere in a temporary directory and starts listening on it.
   I chose to use Unix domain sockets rather than shared memory because this way
   you get low latency communication with without any busy waits or manual
   synchronisation for free. The added benefit is that it also makes it possible
   to send arbitrarily large data without having to split it up into chunks
   first, which is useful for transmitting audio and preset data which may have
   any arbitrary size.
4. The plugin launches the Wine VST host in the detected wine prefix, passing
   the name of the `.dll` file it should be loading and the path to the Unix
   domain socket that was just created.
5. Communication gets set up using multiple sockets over the same end point.
   This allows us to use blocking read operations from multiple threads to
   handle multiple different events without the risk of receiving packets in the
   wrong order. The following types of events get assigned a socket:

   - Calls from the native VST host to the plugin's `dispatch()` function. These
     get forwarded to the Windows VST plugin through the Wine VST host.
   - Calls from the native VST host to the plugin's `dispatch()` function with
     `opcode=effProcessEvents`. These get forwarded to the Windows VST plugin
     through the Wine VST host. This has to be handled separately from all other
     events because of limitations of the Win32 API. Otherwise the plugin would
     not receive any MIDI events while the GUI is being resized or a dropdown
     menu or message box is open.
   - Host callback calls from the Windows VST plugin loaded into the Wine VST
     host through the `audioMasterCallback` function. These get forwarded to the
     native VST host through the plugin.

     Both the `dispatch()` and `audioMasterCallback()` functions are handled in
     the same way, with some minor variations on how payload data gets
     serialized depending on the opcode of the event being sent.

   - Calls from the native VST host to the plugin's `getParameter()` and
     `setParameter()` functions. Both functions get forwarded to the Windows VST
     plugin through the Wine VST host using a single socket because they're very
     similar and don't need any complicated behaviour.
   - Calls from the native VST host to the plugin's `process()` and
     `processReplacing()` functions. Both functions get forwarded to the Windows
     VST plugin through the Wine VST host using a single socket. The `process()`
     function has been deprecated, so a VST host will never call it if
     `processReplacing()` is supported by the plugin.
   - Updates of the Windows VST plugin's `AEffect` object. This object tells the
     host about the plugin's capabilities. A copy of this is sent over a socket
     from the Wine VST hsot to the plugin after it loads the Windows VST plugin
     so it can return a pointer to it to the native VST host. Whenever this
     struct updates, the Windows VST plugin will call the `audioMasterIOChanged`
     host callback and we'll repeat this process.

   The operations described above are all handled by first serializing the
   function parameters and any payload into an object before they can be sent
   over a socket. The objects used for encoding both the requests and and the
   responses for theses events can be found in `src/common/communication.h`
   along with functions that read and write these objects over streams and
   sockets. The actual binary serialization is handled using
   [bitsery](https://github.com/fraillt/bitsery).

   Sending and receiving events happen in the `send_event()` and
   `passthrough_event()` functions. The `passthrough_event()` function calls the
   callback functions and handles the marshalling between our data types and the
   VST API's different pointer types. Reading data and writing the results back
   for host-to-plugin `dispatcher()` calls and for plugin-to-host
   `audioMaster()` callbacks happen in the `DispatchDataConverter` and
   `HostCallbackDataConverter` classes respectively, with a bit of extra glue
   for GUI related operations in `PluginBridge::dispatch_wrapper`. Rewriting all
   of this tightly coupled logic to be all in one place sadly only makes things
   even more complicated.

6. The Wine VST host loads the Windows VST plugin and starts forwarding messages
   over the sockets described above.
7. After the Windows VST plugin has started loading we will forward all values
   from the plugin's `AEffect` struct to the Linux native VST plugin using the
   socket described above. After this point the plugin will stop blocking and
   has finished loading.
