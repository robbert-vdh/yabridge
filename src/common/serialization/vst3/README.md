# VST3 serialization

TODO: Once this is more fleshed out, move this document to `docs/`, and perhaps
replace this readme with a link to that document.

TODO: There are now two approaches in use: the factory takes an interface
pointer for serialization and deserializes into an object directly, and the
component uses an args struct because the alternative involving pointers is just
too unsafe (as we also have to communicate additional payload data). This should
probably be unified into only using the latter appraoch.

The VST3 SDK uses an architecture where every object inherits from an interface,
and every interface inherits from `FUnknown` which offers a dynamic casting
interface through `queryInterface()`. Every interface gets a unique identifier.
It then uses a smart pointer system (`FUnknownPtr<I>`) that queries whether the
`FUnknown` matches a certain interface by checking whether the IDs match up,
allowing casts to that interface if the `FUnkonwn` matches.

Another important part of this system is interface versioning. Old interfaces
cannot be changed, so when the SDK adds new functionality to an existing
interface it defines a new interface that inherits from the old one. The
`queryInterface()` implementation should then allow casts to all of the
implemented interface versions.

Lastly, the interfaces mostly provided a lot of getters for data, but some of
the interfaces also provide callback functions that should perform some
operation on the component implementing the interface.

Yabridge's serialization and communication model for VST3 is thus a lot more
complicated than for VST2 since all of these objects are loosely coupled and are
instantiated and managed by the host. The model works as follows:

1. For an interface `IFoo`, we provide a possibly abstract implementation called
   `YaFoo`.
2. This class has a constructor that takes an `IPtr<IFoo>` interface pointer and
   copies all of the data from the interface's functions that do not perform any
   side effects.
3. `YaFoo` then implements all the boilerplate required for `FUnknown`. This
   includes the constructor, destructor and methods required for reference
   counting, as well as the query interface.
4. If `IFoo` is a versioned interface such as `IPluginFactory3`, the above two
   steps work slightly differently. When copying the data for a plugin factory,
   we'll start copying from `IPluginFactory`, and we'll copy data from each
   newer version of the interface that the `IPtr<IPluginFactory>` supports.
   During this process we keep track of which interfaces were supported by the
   native plugin. In our query interface method we then only report support for
   the same itnerfaces that were supported by `IPtr<IPluginFactory`.
5. `YaFoo` implements serialization and deserialization through bitsery so it
   can be sent between the native plugin and the Wine plugin host.
6. If `IFoo` has methods that have side effects (such as instantiating a new
   object), then the implementations of those functions in `YaFoo` will be pure
   virtual. The side that requested the object (so for the plugin factory that
   would be on the side of the native plugin) should then provide a `YaFoo{Plugin,Host}Impl`
   that implements those functions through yabridge's `Vst3MessageHandler`
   callback interface.
7. If the `IFoo` has side effects and thus needs a corresonding 'real' isntance
   on the other side to communicate to, then `YaFoo{Plugin,Host}Impl` should
   implement a destructor that destroys the 'real' object when `YaFoo` proxy
   gets destroyed. See [interface instantiation](#interface-instantiation) for
   more information.

## Interface Instantiation

Creating a new instance of an interface using the plugin factory wroks as
follows:

1. The host calls `createInterface(cid, _iid, obj)` on an IPluginFactory
   implementation exposed to the host as described above.
2. We check which interface we support matches the `_iid`. If we don't support
   the interface, we'll log a message about it and return that we do not support
   the itnerface.
3. If we determine that `_iid` matches `IFoo`, then we'll send a
   `YaFoo::Construct{cid}` to the Wine plugin host process.
4. The Wine plugin host will then call
   `module->getFactory().createInstance<IFoo>(cid)` using the Windows VST3
   plugin's plugin factory to ask it to create an instance of that interface. If
   this operation fails and returns a null pointer, we'll send an `std::nullopt`
   back to indicate that the instantiation was not successful and we relay this
   on the plugin side.
5. We will generate a unique instance identifier for the newly generated object
   so we can refer to it later. We then serialize that identifier along with
   what other static data is available in `IFoo` in a `YaFoo::Arguments` object.
6. We then move `IPtr<IFoo>` to an `std::map<size_t, IPtr<IFoo>>` with that
   unique identifier we generated earlier as a key so we can refer to it later
   in later function calls.
7. On the plugin side we can now use the `YaFoo::Arguments` object we received
   to create a `YaFooPluginImpl` object that can send control messages to the
   Wine plugin host.
8. Finally a pointer to this `YaFooPluginImpl` gets returned as the last step of
   the initialization process.

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
