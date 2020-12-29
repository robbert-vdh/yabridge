# Architecture

TODO: This document has not yet been updated since adding VST3 support

The project consists of two components: a Linux native VST plugin
(`libyabridge.so`) and a VST host that runs under Wine
(`yabridge-host.exe`/`yabridge-host.exe.so`, and
`yabridge-host-32.exe`/`yabridge-host-32.exe.so` if the bitbridge is enabled).
I'll refer to the copy of or the symlink to `libyabridge.so` as _the plugin_,
the native Linux VST host that's hosting the plugin as _the native VST host_,
the Wine VST host application that's hosting a Windows `.dll` file as _the Wine
VST host_, and the Windows VST plugin that's being loaded in the Wine VST host
as the _Windows VST plugin_. The whole process works as follows:

1. Some copy of or a symlink to `libyabridge.so` gets loaded as a VST plugin in
   a Linux VST host. This file should have been renamed to match a Windows VST
   plugin `.dll` file in the same directory. For instance, if there's a
   `Serum_x64.dll` file you'd like to bridge, then there should be a symlink to
   `libyabridge.so` named `Serum_x64.so`.
2. The plugin first attempts to locate and determine:

   - The Windows VST plugin `.dll` file that should be loaded.

   - The architecture of that VST plugin file. This is done by inspecting the
     headers if the `.dll` file.

   - The location of the Wine VST host. This will depend on the architecture
     detected for the plugin. If the plugin was compiled for the `x86_64`
     architecture or the 'Any CPU' target, then we will look for
     `yabridge-host.exe`. If the plugin was compiled for the `x86` architecture,
     when we'll search for `yabridge-host-32.exe`.

     We will first search for this file alongside the actual location of
     `libyabridge.so`. This is useful for development, as it allows you to use a
     symlink to `libyabridge.so` directly from the build directory causing
     yabridge to automatically pick up the right version of the Wine VST host.
     If this file cannot be found, then it will fall back to searching through
     the search path.

   - The Wine prefix the plugin is located in. If the `WINEPREFIX` environment
     variable is specified, then that will be used instead.

3. The plugin then sets up several Unix domain socket endpoints to communicate
   with the Wine VST host somewhere in a temporary directory and starts
   listening on them. We use multiple sockets so we can easily concurrently
   handle multiple data streams from different threads using blocking
   synchronous operations. This greatly simplifies the way communication works
   without compromising on latency. The different sockets are described below.
4. The plugin launches the Wine VST host in the detected wine prefix, passing
   the name of the `.dll` file it should be loading and the base directory for
   the Unix domain sockets that are going to be communciated over as its
   arguments. See the [Wine hosts](#wine-hosts) below for more information on
   the different Wine VST host binaries.
5. The Wine VST host connects to the sockets and communication between the
   plugin and the Wine VST host gets set up. The following types of events are
   handled seperately:

   - Calls from the native VST host to the plugin's `dispatcher()` function.
     These get forwarded to the Windows VST plugin through the Wine VST host.

   - Host callback calls from the Windows VST plugin through the
     `audioMasterCallback` function. These get forwarded to the native VST host
     through the plugin.

     Both the `dispatcher()` and `audioMasterCallback()` functions are handled
     in the same way with some minor variations on how payload data gets
     serialized depending on the opcode of the event being sent. See the [event
     handling section](#event-handling) below this for more details on this
     procedure.

   - Calls from the native VST host to the plugin's `getParameter()` and
     `setParameter()` functions. Both functions get forwarded to the Windows VST
     plugin through the Wine VST host using a single socket because they're very
     similar and don't need any complicated behaviour.

   - Calls from the native VST host to the plugin's `processReplacing()` and
     `processDoubleReplacing()` functions. These functions get forwarded to the
     Windows VST plugin through the Wine VST host. In the rare event that the
     plugin does not support `processReplacing()` and only supports The
     deprecated commutative `process()` function, then the Wine VST host will
     emulate the behavior of `processReplacing()` instead. Single and double
     precision audio go over the same socket since the host will only call one
     or the other, and we just use a variant to determine which one should be
     called on the Wine host side. If the host somehow does end up calling the
     deprecated accumulative `process()` function instead of
     `processReplacing()`, then we'll emulate `process()` using
     `processReplacing()`.

   - And finally there's a separate socket for control messages. At the moment
     this is only used to transfer the Windows VST plugin's `AEffect` object to
     the plugin and the current configuration from the plugin to the Wine VST
     host on startup.

6. The Wine VST host loads the Windows VST plugin and starts forwarding messages
   over the sockets described above.
7. After the Windows VST plugin has started loading we will forward all values
   from the Windows VST plugin's `AEffect` struct to the plugin, and the plugins
   configuration gets sent back over the same socket to the Wine VST host. After
   this point the plugin will stop blocking and the initialization process is
   finished.

## Event handling

Event handling for the host -> plugin `dispatcher()`and plugin -> host
`audioMaster()` functions work in the same way. The function parameters and any
payload data are serialized into a binary format using
[bitsery](https://github.com/fraillt/bitsery). The receiving side then
unmarshalls the payload data into the representation used by VST2, calls the
actual function, and then serializes the results again and sends them back to
the caller. The conversions on the sending side are handled by the
`*DataConverter` classes, and on the receiving side the `passthrough_event()`
function knows how to convert between yabridge's representation types and the
types used by VST2.

One special implementation detail about yabridge's event handling is its use of
sockets. Whenever possible yabridge uses a single long living socket for each of
the operations described in the section above. For event handling however it can
happen that the host is calling `dispatch()` a second time from another thread
while the first call is still pending. Or `audioMaster()` and `dispatch()` can
be called in a mutually recursive fashion. In order to be able to handle those
situations, yabridge will create additional socket connections as needed. The
receiving side listens for incoming connections, and when it accepts a new
connection an additional thread will be spawned to handle the incoming request.
This allows for fully concurrent event handling without any blocking.

Lastly there are some `dispatch()` calls that will have to be handled on the
Wine VST host's main thread. This is because in the Win32 programming model all
GUI operations have to be done from a single thread, so any `dispatch()` calls
that potentially use any of those APIs will have to be handled from the same
thread that's running the Win32 message loop. In
`src/wine-host/bridges/vst2.cpp` there are several opcodes marked as unsafe.
When we encounter one of those events, we'll use Boost.Asio's strands to call
the plugin's `dispatch()` function from within the main IO context which also
handles the Win32 message loop. That way we can easily execute all potential GUI
code from the same thread.

## Wine hosts

Yabridge has four different VST host binaries. There are binaries for hosting a
single plugin and binaries for hosting multiple plugins within a plugin group,
with 32-bit and 64-bit versions of both.

The group host binaries for plugin groups host plugins in the exact same way as
the regular host binaries, but instead of directly hosting a plugin they instead
start listening on a socket for incoming requests to host a particular plugin.
When a group host receives a request to host a plugin, it will initialize the
plugin from within the main Boost.Asio IO context, and it will then spawn a new
thread to start handling events. After that everything works the exact same way
as individually hosted plugins, and when the plugin exits the thread and all the
plugin's resources are cleaned up. Initializing the plugin within the main IO
context is important because all operations potentially using GUI or other Win32
message loop related operations should be performed from the same thread. When
all plugins have exited, the group host process will wait for a few seconds
before it also shuts down.
