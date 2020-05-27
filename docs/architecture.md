# Architecture

The project consists of two components: a Linux native VST plugin
(`libyabridge.so`) and a VST host that runs under Wine
(`yabridge-host.exe`/`yabridge-host.exe.so`, and
`yabridge-host-32.exe`/`yabridge-host-32.exe.so` if the bitbirdge is enabled).
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

3. The plugin then sets up a Unix domain socket endpoint to communicate with the
   Wine VST host somewhere in a temporary directory and starts listening on it.
   I chose to communicate over Unix domain sockets rather than using shared
   memory directly because this way you get low latency communication with
   without any busy waits or manual synchronisation for free. The added benefit
   is that it also makes it possible to send arbitrarily large chunks of data
   without having to split it up first. This is useful for transmitting audio
   and preset data which may have any arbitrary size.
4. The plugin launches the Wine VST host in the detected wine prefix, passing
   the name of the `.dll` file it should be loading and the path to the Unix
   domain socket that was just created as its arguments.
5. Communication gets set up using multiple sockets over the end point created
   previously. This allows us to easily handle multiple data streams from
   different threads using blocking read operations for synchronization. Doing
   this greatly simplifies the way communication works without compromising on
   latency. The following types of events each get their own socket:

   - Calls from the native VST host to the plugin's `dispatcher()` function.
     These get forwarded to the Windows VST plugin through the Wine VST host.
   - Calls from the native VST host to the plugin's `dispatcher()` function with
     the `effProcessEvents` opcode. These also get forwarded to the Windows VST
     plugin through the Wine VST host. This has to be handled separately from
     all other events because of limitations of the Win32 API. Without doing
     this the plugin would not be able to receive any MIDI events while the GUI
     is being resized or a dropdown menu or message box is shown.
   - Host callback calls from the Windows VST plugin through the
     `audioMasterCallback` function. These get forwarded to the native VST host
     through the plugin.

     Both the `dispatcher()` and `audioMasterCallback()` functions are handled
     in the same way, with some minor variations on how payload data gets
     serialized depending on the opcode of the event being sent. See the section
     below this for more details on this procedure.

   - Calls from the native VST host to the plugin's `getParameter()` and
     `setParameter()` functions. Both functions get forwarded to the Windows VST
     plugin through the Wine VST host using a single socket because they're very
     similar and don't need any complicated behaviour.
   - Calls from the native VST host to the plugin's `processReplacing()`
     function. This function gets forwarded to the Windows VST plugin through
     the Wine VST. In the rare event that the plugin does not support
     `processReplacing()` and only supports The deprecated commutative
     `process()` function, then the Wine VST host will emulate the behavior of
     `processReplacing()` instead.

   The operations described above involving the host -> plugin `dispatcher()`and
   plugin -> host `audioMaster()` functions are all handled by first serializing
   the function parameters and any payload data into a binary format so they can
   be sent over a socket. The objects used for encoding both the requests and
   the responses for theses events can be found in `src/common/serialization.h`,
   and the functions that actually read and write these objects over the sockets
   are located in `src/common/communication.h`. The actual binary serialization
   is handled using [bitsery](https://github.com/fraillt/bitsery).

   Actually sending and receiving the events happens in the `send_event()` and
   `receive_event()` functions. When calling either `dispatch()` or
   `audioMaster()`, the caller will oftentimes either pass along some kind of
   data structure through the void pointer function argument, or they expect the
   function's return value to be a pointer to some kind of struct provided by
   the plugin or host. The behaviour for reading from and writing into these
   void pointers and returning pointers to objects when needed is encapsulated
   in the `DispatchDataConverter` and `HostCallbackDataCovnerter` classes for
   the `dispatcher()` and `audioMaster()` functions respectively. For operations
   involving the plugin editor there is also some extra glue in
   `Vst2Bridge::dispatch_wrapper`. On the receiving end of the function calls,
   the `passthrough_event()` function which calls the callback functions and
   handles the marshalling between our data types created by the
   `*DataConverter` classes and the VST API's different pointer types. This
   behaviour is separated from `receive_event()` so we can handle MIDI events
   separately. This is needed because a select few plugins only store pointers
   to the received events rather than copies of the objects. Because of this,
   the received event data must live at least until the next audio buffer gets
   processed so it needs to be stored temporarily.

6. The Wine VST host loads the Windows VST plugin and starts forwarding messages
   over the sockets described above.
7. After the Windows VST plugin has started loading we will forward all values
   from the plugin's `AEffect` struct to the Linux native VST plugin over the
   `dispatcher()` socket. This is only done once at startup. After this point
   the plugin will stop blocking and has finished loading.

TODO: Document plugin groups
