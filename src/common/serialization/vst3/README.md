# VST3 interfaces

TODO: After merging into master, update this link to just point to GitHub

See [docs/vst3.md](../../../../docs/vst3.md) for more information on how the
serialization works.

VST3 plugin interfaces are implemented as follows:

| yabridge class      | Included in         | Interfaces                                             |
| ------------------- | ------------------- | ------------------------------------------------------ |
| `YaPluginFactory`   |                     | `IPluginFactory`, `IPluginFactory2`, `IPluginFactory3` |
| `Vst3PlugViewProxy` |                     | All of the below:                                      |
| `YaPlugView`        | `Vst3PlugViewProxy` | `IPlugView`                                            |
| `Vst3PluginProxy`   |                     | All of the below:                                      |
| `YaAudioProcessor`  | `Vst3PluginProxy`   | `IAudioProcessor`                                      |
| `YaComponent`       | `Vst3PluginProxy`   | `IComponent`                                           |
| `YaConnectionPoint` | `Vst3PluginProxy`   | `IConnectionPoint`                                     |
| `YaEditController`  | `Vst3PluginProxy`   | `IEditController`                                      |
| `YaEditController2` | `Vst3PluginProxy`   | `IEditController2`                                     |
| `YaPluginBase`      | `Vst3PluginProxy`   | `IPluginBase`                                          |

VST3 host interfaces are implemented as follows:

| yabridge class              | Included in                 | Interfaces          |
| --------------------------- | --------------------------- | ------------------- |
| `Vst3HostContextProxy`      |                             | All of the below:   |
| `YaHostApplication`         | `Vst3HostContextProxy`      | `IHostApplication`  |
| `Vst3ComponentHandlerProxy` |                             | All of the below:   |
| `YaComponentHandler`        | `Vst3ComponentHandlerProxy` | `IComponentHandler` |

The following host interfaces are passed as function arguments and are thus also
implemented for serialization purposes:

| yabridge class       | Interfaces          | Notes                                                                  |
| -------------------- | ------------------- | ---------------------------------------------------------------------- |
| `YaEventList`        | `IEventList`        | Comes with a lot of serialization wrappers around the related structs. |
| `YaParameterChanges` | `IParameterChanges` |                                                                        |
| `YaParamValueQueue`  | `IParamValueQueue`  |                                                                        |
| `VectorStream`       | `IBStream`          | Used for serializing data streams.                                     |

And finally `YaProcessData` uses the above along with `YaAudioBusBuffers` to
wrap around `ProcessData`.
