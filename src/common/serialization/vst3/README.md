# VST3 serialization

TODO: Once this is more fleshed out, move this document to `docs/`, and perhaps
replace this readme with a link to that document.

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
   virtual and both the native plugin and the Wine plugin host should provide
   their own implementation. Since the functions will ever only be called from
   one of the two sides, the other side can just throw in their implementation.

## Plugin Factory

Aside form the above, the plugin factory is the only place where we may
potentially report different values from those reported by the Windows VST3
plugin. If we encounter an itnerface we do not yet support, we will log a
warning and we'll skip the interface since we wouldn't know how to handle it.
