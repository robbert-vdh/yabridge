# VST3 serialization

TODO: Flesh this out further, update the instantiation part, make the proxying part clearer

TODO: Link to `src/common/serialization/vst3/README.md`

The VST3 SDK uses an architecture where every concrete object inherits from an
interface, and every interface inherits from `FUnknown`. `FUnkonwn` offers a
dynamic casting interface through `queryInterface()` and a reference counting
mechanism that calls `delete this;` when the reference count reaches 0. Every
interface gets a unique identifier. It then uses a smart pointer system
(`FUnknownPtr<I>`) that queries whether the `FUnknown` matches a certain
interface by checking whether the IDs match up, allowing casts to that interface
if the `FUnkonwn` matches. Those smart pointers also use that reference counting
mechanism to destroy the object when the last pointer gets dropped.

Another important part of this system is interface versioning. Old interfaces
cannot be changed, so when the SDK adds new functionality to an existing
interface it defines a new interface that inherits from the old one. The
`queryInterface()` implementation should then allow casts to all of the
implemented interface versions.

Lastly, the interfaces provide both getters for static, non-chancing data (such
as the classes registered in a plugin factory) as well as functions that perform
side effects or return dynamically changing data (such as the input/output
configuration for an audio processor).

Yabridge's serialization and communication model for VST3 is thus a lot more
complicated than for VST2 since all of these objects are loosely coupled and are
instantiated and managed by the host. The basic model works as follows:

1. The main idea behind yabridge's VST3 implementation is that we define
   monolithic proxy objects that can proxy any object created by the Windows
   VST3 plugin. These proxy objects indirectly inherit from all applicable
   interfaces defiend in the VST3 SDK. `Vst3PluginProxy` implements all
   interfaces that can be implemented by plugins, and `Vst3HostProxy` implements
   all interfaces that are to be implemented by the host.
2. For every interface `IFoo`, we provide an abstract implementation called
   `YaFoo`. This implementation mostly contain message object we use to make
   specific function calls on the actual objects we are proxying. The
   implementation also comes with a function that takes an `FUnknown` pointer,
   checks whether the object behind that pointer supports `IFoo`, and then
   stores the result along with any potential static payload data as a
   `YaFoo::ConstrctArgs` object.
3. Proxy object are instantiated while handling
   `IPluginFactory::createInstance()` for `Vst3PluginProxy`, and during
   `IPluginBase::initialize()` and `IPluginFactory::setHostContext()` for
   `Vst3HostProxy`. On the receiving side of those functions (where we call the
   actual function implemented by the plugin or the host), we receive an
   `IPtr<T>` smart pointer to an object provided by the host or the plugin. We
   use this object to iterate over every applicable `YaFoo` as mentioend above.
   All of these `YaFoo::ConstructArgs` objects along with a unique identifier
   for this specific object are then serialized and transmitted to the other
   side. With this information we can create a proxy object that supports all
   the same interfaces (and thus allows calls to the functions in those
   interfaces) as the original object we are proxying.
4. As mentioend, every object we instantiate gets assigned a unique identifier.
   When dealign with objects created by the Windows VST3 plugin, the object's
   `FUnknown` pointer will be stored in an `std::map<size_t, PluginObject>` map.
   This way we can refer to it later on when we receive a request to call a
   specific function on the plugin.
5. If `IFoo` is a versioned interface such as `IPluginFactory{,2,3}`, the
   creation of `YaFoo::ConstrctArgs` and the definition of `YaFoo`'s query
   interface work slightly differently. When copying the data for a plugin
   factory, we'll start copying from `IPluginFactory`, and we'll copy data from
   each newer version of the interface that the `IPtr<IPluginFactory>` supports.
   During this process we keep track of which interfaces were supported by the
   native plugin. In our query interface method we then only report support for
   the same interface versions that were supported by the original
   `IPtr<IPluginFactory>` we are proxying.

## Interface Instantiation

Creating a new instance of an interface using the plugin factory wroks as
follows. This describes the object lifecycle. The actual serialization and
proxying is described in the section above.

1. The host calls `createInterface(cid, _iid, obj)` on an `IPluginFactory`
   implementation exposed to the host as described above.
2. We check which interface we support matches the `_iid`. If we don't support
   the interface, we'll log a message about it and return that we do not support
   the itnerface.
3. If we determine that `_iid` matches `IFoo`, then we'll send a
   `YaFoo::Construct{cid}` to the Wine plugin host process.
4. The Wine plugin host will then call
   `module->getFactory().createInstance<IFoo>(cid)` using the Windows VST3
   plugin's plugin factory to ask it to create an instance of that interface. If
   this operation fails and returns a null pointer, we'll send a
   `kNotImplemented` result code back to indicate that the instantiation was not
   successful and we relay this on the plugin side.
5. As mentioned above, we will generate a unique instance identifier for the
   newly generated object so we can refer to it later. We then serialize that
   identifier along with what other static data is available in `IFoo` in a
   `YaFoo::ConstructArgs` object.
6. We then move `IPtr<IFoo>` to an `std::map<size_t, IPtr<IFoo>>` with that
   unique identifier we generated earlier as a key so we can refer to it later
   in later function calls.
7. On the plugin side we can now use the `YaFoo::Arguments` object we received
   to create a `YaFooPluginImpl` object that can send control messages to the
   Wine plugin host.
8. Finally a pointer to this `YaFooPluginImpl` gets returned as the last step of
   the initialization process.

## Simple objects

For serializing objects of interfaces that purely contain getters and setters
(and thus don't need to perform any host callbacks), we'll simply have a
constructor that takes the `IFoo` by `IPtr` or reference (depending on how it's
used in the SDK) and reads the data from it to create a serializable copy of
that object.

## Safety notes

- None of the destructors in the interfaces defined by the SDK are marked as
  virtual because this could apparently [break binary
  compatibility](https://github.com/steinbergmedia/vst3sdk/issues/21). This
  means that the destructor of the class that implemented `release()` will be
  called. This is something to keep in mind when dealing with inheritence.
- Since everything behind the scenes makes use of these `addRef()` and
  `release()` reference counting functions, we can't use the standard library's
  smart pointers when dealing with objects that are shared with the host or with
  the Windows VST3 plugin. In `IPtr<T>`'s destructor it will call release, and
  the objects will clean themselfs up with a `delete this;` when the reference
  count reaches 0. Combining this with the STL cmart pointers this would result
  in a double free.
