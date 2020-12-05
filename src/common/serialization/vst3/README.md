# VST3 serialization

TODO: Once this is more fleshed out, move this document to `docs/`, and perhaps
replace this readme with a link to that document.

The VST3 SDK uses an architecture where every object inherits from an interface,
and every interface inherits from `FUnknown` which offers a sort of query
interface. Newer versions of an interface with added functionality then inherit
from the previous version of that interface. Every interface (and thus also
newer versions of an old interface) get a unique identifier. It then uses a
smart pointer system (`FUnknownPtr<I>`) that queries whether the `FUnknown`
matches a certain interface by checking whether the IDs match up, allowing casts
to that interface if the `FUnkonwn` matches. This means that an
`IPluginFactory*` may also be an `IPluginFactory2*` or an `IPluginFactory3*`.
For yabridge we need to be able to pass concrete serializable objects that
implement these interfaces around.

## Serializing simple objects

TODO: Think of a better naming scheme

Serializing an object that implements `ISimple` that only stores data and can't
perform any callbacks works as follows:

1. We define a class called `YaSimple` that inherits from `ISimple`.
2. We fetch all data from `ISimple` and store it in `YaSimple`.
3. `YaSimpl` can then be serialized with bitsery and transmitted like any other
   object.

Our
solution approach for serializing Our solution here is to build an object that's compatible with
`IPluginFactory3` that copies all data from the original object

## Serializing versioned interfaces

For serializing versioned interfaces, such as `IPluginFactory3`, we'll do
something similar:

1. As with simple object, we define a class called `YaPluginFactory` that
   inherits from `IPluginFactory3`.
2. Now we start copying data starting with `IPluginFactory`, then moving on to
   `IPluginFactory2`, and then finally `IPluginFactory3`. When at some point our
   `FUnknownPtr<I>` returns a null pointer we know that the object doesn't
   implement that version of the interface and we can stop.
3. During the copying process we'll also copy over the `iid`. This allows our
   object to appear as the highest version of the interface we were able to copy
   from. Doing this avoids complicated inheritance chains in our own
   implemetnation.
4. `YaPluginFactory` can then be serialized with bitsery and transmitted like
   any other object.

## Processors and controllers

TODO: Not sure how this will work yet.
