# Architecture

- [General architecture](#general-architecture)
  - [Communication](#communication)
  - [Editor embedding](#editor-embedding)
- [VST2 plugins](#vst2-plugins)
- [VST3 plugins](#vst3-plugins)
- [Audio buffers](#audio-buffers)

## General architecture

The project consists of multiple components: several native Linux plugins
(`libyabridge-vst2.so` for VST2 plugins, and `libyabridge-vst3.so` for VST3
plugins) and a few different plugin host applications that can run under Wine
(`yabridge-host.exe`/`yabridge-host.exe.so`, and
`yabridge-host-32.exe`/`yabridge-host-32.exe.so` if the bitbridge is enabled).

The main idea is that when the host loads a plugin, the plugin will try to
locate the corresponding Windows plugin, and it will then start a Wine process
to host that Windows plugin. Depending on the architecture of the Windows plugin
and the configuration in the `yabridge.toml` config files (see the readme for
more information), yabridge will pick between the four plugin host applications
named above. When a plugin has been configured to use plugin groups, instead of
spawning a new host process the plugin will try to connect to an existing group
host process first and ask it to host the Windows plugin within that process.

### Communication

Once the Wine plugin host has started or the group host process has accepted the
request to host the plugin, communication between the native plugin and the
Windows plugin host will be set up using a series of Unix domain sockets. How
exactly these are used and distributed depends on the plugin format but the
basic approach remains the same. When the plugin or the host calls a function or
performs a callback, the arguments to that function and any additional payload
data gets serialized into a struct which then gets sent over the socket. This is
done using the [bitsery](https://github.com/fraillt/bitsery) binary
serialization library. On the receiving side there will be a thread idly waiting
for data to be sent over the socket, and when it receives a request it will pass
the payload data over to the corresponding function and then returns the results
again using the same serialization process.

One important detail for this approach is the ability to spawn additional
sockets when needed. Because reads and writes on these sockets are necessarily
blocking (requests may not arrive out of order, and on the receiving side there
is no other work to do anyways), a socket can only be used to handle a single
function call at a time. This can cause issues with certain mutually recursive
function calling sequences, particularly when dealing with opening and resizing
editors. To work around this, for some sockets yabridge will spawn an additional
background thread that asynchronously accepts new connections on that socket
endpoint. When the host or the plugin wants to call a function over a socket
that is currently being written to (i.e. when the mutex for that socket is
locked), yabridge will make a new socket connection and it will send the payload
data over that new socket. This will cause a new thread to be spawned on the
receiving side which then handles the request. All of this behaviour is
encapsulated and further documented in the `AdHocSocketHandler` class and all of
the classes derived from it.

Another important detail when it comes to communication is the handling of
certain function calls on the Wine plugin host side. On Windows anything that
interacts with the Win32 message loop or the GUI has to be done from the same
thread (or typically the main thread). To do this yabridge will execute certain
'unsafe' functions that are likely to interact with these things from the main
thread. The main thread also periodically handles Win32 and optionally also X11
events (when there are open editors) using a Boost.Asio timer, so these function
calls can all be done from that same thread by posting a task to the Boost.Asio
IO context.

On the native Linux side it usually doesn't matter which thread functions are
called from, but since REAPER does not allow any function calls that interact
with the GUI from any non-GUI threads, we'll also do something similar when
handling `audioMasterSizeWindow()` for VST2 plugins
`IPlugFrame::resizeView()`/`IContextMenu::popup()` for VST3 plugins.

Lastly there are a few specific situations where the above two issues of mutual
recursion and functions that can only be called from a single thread are
combined. In those cases we need to the send over the socket on a new thread, so
that the calling thread can handle other tasks through another IO context. See
`Vst3HostBridge::send_mutually_recursive_message()` and
`Vst3Bridge::send_mutually_recursive_message()` for the actual implementation
with more details. This applies to the functions related to resizing VST3
editors on both the Linux and the Wine sides.

### Editor embedding

Everything related to editor embedding happens in `src/wine-host/editor.h`. To
embed the Windows plugin's editor in the X11 window provided by the host we'll
create a Wine window, embed that window into the host's window, and then ask the
Windows plugin to embed itself into that Wine window. For embedding the Wine
window into the host's window we support two different implementations:

- The main approach involves reparenting the Wine window to the host window, and
  then manually sending X11 `ConfigureNotify` events to the corresponding X11
  window whenever its size or position on the screen changes. This is needed
  because while the reparented Wine window is located at the (relative)
  coordinates `(0, 0)`, Wine willl think that these coordinates are absolute
  screen coordinates and without sending this event a lot of Windows
  applications will either render in the wrong location or have broken knobs and
  sliders. By manually sending the event instead of actually reconfiguring the
  window Wine will think the window is located at its actual screen coordinates
  and user interaction works as expected.
- Alternatively there's an option to use Wine's own XEmbed implementation.
  XEmbed is the usual solution for embedding one application window into
  approach. However this sadly does have a few quirks, including flickering with
  some plugins that use VSTGUI and windows that don't properly rendering until
  they are reopened in some hosts. Because of that the above embedding behaviour
  that essentially fakes this XEmbed support is the default and XEmbed can be
  enabled separately on a plugin by plugin basis by setting a flag in a
  `yabridge.toml` config file.

Aside from embedding the window we also manage keyboard focus grabbing. Since
it's not possible for us to know when the Windows plugin wants keyboard focus,
we'll grab keyboard focus automatically when the mouse enters editor window
while that editor is active (so we don't end up grabbing focus when the window
is in the background or when the plugin has opened a popup), and we'll reset
keyboard focus to the host's window when the mouse leaves the editor window
again while it is active. This makes it possible to enter text and to use
keyboard combinations in a plugin while still allowing regular control over the
host. For hosts like REAPER where the editor window is embedded in a larger
window with more controls this is even more important as it allows you to still
interact with those controls using the keyboard.

## VST2 plugins

When a VST2 plugin gets initialized using the process described above, we'll
send the VST2 plugin's `AEffect` object from the Wine plugin host to the native
plugin over a control socket. We'll also send the plugin's configuration
obtained by parsing a `yabridge.toml` file from the native plugin to the Wine
plugin host so it can. After that we'll use the following sockets to communicate
over:

- Calls from the host to the plugin's `dispatcher()` function will be forwarded
  to the Windows plugin running under the Wine plugin host. For this we'll use
  the approach described above where we'll spawn additional sockets and threads
  as necessary. Because the `dispatcher()` (and the `audioMaster()` function
  below) are already in fairly easily serializable format, we use the
  `*DataConverter` classes to read and write payload data depending on the
  opcode (or to make a best guess estimate if we're dealing with some unknown
  undocumented function), and we then `Vst2EventHandler::send_event()`,
  `Vst2EventHandler::receive_events()`, and `passthrough_event()` to pass through
  these function calls.
- For callbacks made by the Windows plugin using the provided `audioMaster()`
  function we do exactly the same as the above, but the other way around.
- Getting and setting parameters through the plugin's `getParameter()` and
  `setParameter()` functions is done over a single socket.
- Finally processing audio gets a dedicated socket. The native VST2 plugin
  exposes the `processReplacing()`, the legacy `process()`, if supported by the
  Windows plugin also the `processDoubleReplacing()` functions. Since
  `process()` is never used (nor should it be), we'll simply emulate it in terms
  of `processReplacing()` by summing the results to existing output values and
  the outputs returned by that `processReplacing()` call. On the Wine host side
  we'll also check whether the plugin supports `processReplacing()`, and if it
  for some reason does not then we'll simply call `process()` with zeroed out
  buffers.

## VST3 plugins

VST3 plugins are architecturally very different from VST2 plugins. A VST3 plugin
is a module, that when loaded by the host exposes a plugin factory that can be
used to create various classes known to that factory. Normally this factory
contains one or more audio processing classes (which are based on the
`IComponent` class), and then that same number of edit controller classes (which
are based on the `IEditController` class) belonging to those audio processors. A
VST3 host loads the VST3 module, calls the `ModuleEntry()` function, requests
the plugin's factory, iterates over the available classes, and then asks the
plugin to instantiate the objects it wants. A very important consequence of this
approach is that a single VST3 module can provide multiple processor and edit
controller instances which will then appear in your DAW as multiple plugins.
Because of that all instances of a single VST3 plugin will always have to be
hosted in a single Wine process.

VST3 plugin object instances are also very different from the VST2 `AEffect`
instances. The VST3 architecture is based on Microsoft COM and uses a system
where an object can implement any number of interfaces that are exposed through
a query interface and an associated reference counting dynamically casting smart
pointer. This allows the VST3 SDK to be modular and its functionality to be
expanded upon over time, but it does make proxying such an object more
difficult. Yabridge's approach for this problem is described below.

Communication for VST3 modules within yabridge uses one communication channel
for function calls from the native host to the Windows plugin, one channel for
callbacks from the Windows plugin to the native host, and then one additional
channel per audio processor for performance reasons. All of these communication
channels allow for additional sockets and threads to be spawned using the means
outlined above.

When the host loads the VST3 module, we'll go through a similar process as when
initialzing the VST2 version of yabridge. After initialization the host will ask
for the plugin factory which we'll request a copy of from the Windows plugin.
We'll also once again copy any configuration for the plugin set in a
`yabridge.toml` configuration file to the Wine plugin host. The returned plugin
factory acts as a _proxy_, and when the host requests an object to be created
using it we'll create the corresponding object on the Wine plugin host side and
then build a perfect proxy of that object on the plugin side. This means that
the object we return should support all of the same VST3 interfaces as the
original object, so that plugin proxy object will act identically to the
original object instance provided by the Windows VST3 plugin.

Every plugin proxy objects each gets assigned a unique identifier. This way we
can identify it and any other associated objects during function calls.

Any function calls made on a proxy object will be passed through to the other
side over one of the sockets mentioned above. For this we use dedicated request
objects per function call or operation with an associated type for the expected
response type. Combining that with `std::variant<Ts...>` and C++20 templated
lambdas allows this communication system to be type safe while still having
easily readable error messages.

When a function call returns another interface object instance, we also have to
create a proxy of that.
[src/common/serialization/vst3/README.md](https://github.com/robbert-vdh/yabridge/blob/master/src/common/serialization/vst3/README.md)
outlines all of these proxy classes and the interfaces implemented. This goes
three levels deep at most (`Vst3PluginProxy` to `Vst3PlugViewProxy` to
`Vst3PlugFrameProxy`). Here we once again detect all of the interfaces the
actual object supports so that the proxy object can report to support those same
interfaces.

Creating proxies happens using these monolithic `Vst3*Proxy` classes defined in
the document linked above. These inherit from a number of application `YaFoo`
classes which are simply wrappers around the corresponding `IFoo` VST3 interface
with their associated message structs for handling function calls and a field
indicating whether the object supported that interface or not. These
`Vst3*Proxy` classes are also where we'll implement the `FUnknown` interface,
which is where the functionality for reference counting is implemented. A VST3
object will call `delete this;` when its reference count reaches zero to clean
itself up. Because of binary compatibility reasons destructors in the VST3 SDK
are non-virtual, but we can safely make them virtual in our case.
`Vst3*ProxyImpl` then provides an implementation for all of the applicable
`IFoo` interfaces that perform function calls using those message structs.

## Audio buffers

Starting from yabridge 3.4.0, audio processing is now handled using a hybrid of
both shared memory and the socket-based message passing mechanism. Yabridge uses
sockets instead of shared memory everywhere else because of the added
flexibility in terms of messages we can handle and so we can concurrently handle
multiple messages, but the downside of this approach is that you will always
need to do additional work during the (de)serialization process mostly in terms
of copying and moving memory. Since audio buffers are large and have a maximum
size that is known before audio processing begins, we can simply store the audio
buffers in a big block of shared memory and use the sockets for all other data
that gets sent along with the actual audio buffers. This also means that the
sockets act as a form of synchronisation, so we do not need any additional
inter-process locking. These shared memory audio buffers are defined as part of
`AudioShmBuffer`, and they are configured while handling `effMainsChanged` for
VST2 plugins and during `IAudioProcessor::setupProcessing()` for VST3 plugins.
For VST2 plugins this does mean that we will need to keep track of the maximum
block size and the sample size reported by the host, since this information is
not passed along with `effMainsChanged`.
