# VST3 interfaces

TODO: After merging into master, update this link to just point to GitHub

See [docs/vst3.md](../../../../docs/vst3.md) for more information on how the
serialization works.

VST3 interfaces are implemented as follows:

| Yabridge class      | Interfaces                                             | Notes                                                      |
| ------------------- | ------------------------------------------------------ | ---------------------------------------------------------- |
| `YaComponent`       | `IComponent`, `IPluginBase`, `IAudioProcessor`         |                                                            |
| `YaHostApplication` | `iHostAPplication`                                     | Used as a 'context' to allow the plugin to maek callbacks. |
| `YaPluginFactory`   | `IPluginFactory`, `IPluginFactory2`, `IPluginFactory3` |                                                            |

The following interfaces are implemented purely fur serialization purposes:

| Yabridge class       | Interfaces          | Notes                                                                  |
| -------------------- | ------------------- | ---------------------------------------------------------------------- |
| `YaEventList`        | `IEventList`        | Comes with a lot of serialization wrappers around the related structs. |
| `YaParameterChanges` | `IParameterChanges` |                                                                        |
| `YaParamValueQueue`  | `IParamValueQueue`  |                                                                        |
| `VectorStream`       | `IBStream`          | Used for serializing data streams.                                     |

And finally `YaProcessData` uses the above along with `YaAudioBusBuffers` to
wrap around `ProcessData`.
