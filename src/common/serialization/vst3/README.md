# VST3 interfaces

See
[docs/vst3.md](https://github.com/robbert-vdh/yabridge/blob/master/docs/vst3.md)
for more information on how the serialization works.

The following interfaces are not yet implemented:

- Every interface introduced after VST 3.0.0
- The [Presonus extensions](https://presonussoftware.com/en_US/developer),
  although most of these things seem to overlap with newer VST3 interfaces

VST3 plugin interfaces are implemented as follows:

| yabridge class             | Included in         | Interfaces                                             |
| -------------------------- | ------------------- | ------------------------------------------------------ |
| `YaPluginFactory`          |                     | `IPluginFactory`, `IPluginFactory2`, `IPluginFactory3` |
| `Vst3ConnectionPointProxy` |                     | `IConnectionPoint` through `YaConnectionPoint`         |
| `Vst3PlugViewProxy`        |                     | All of the below:                                      |
| `YaPlugView`               | `Vst3PlugViewProxy` | `IPlugView`                                            |
| `Vst3PluginProxy`          |                     | All of the below:                                      |
| `YaAudioProcessor`         | `Vst3PluginProxy`   | `IAudioProcessor`                                      |
| `YaComponent`              | `Vst3PluginProxy`   | `IComponent`                                           |
| `YaConnectionPoint`        | `Vst3PluginProxy`   | `IConnectionPoint`                                     |
| `YaEditController`         | `Vst3PluginProxy`   | `IEditController`                                      |
| `YaEditController2`        | `Vst3PluginProxy`   | `IEditController2`                                     |
| `YaPluginBase`             | `Vst3PluginProxy`   | `IPluginBase`                                          |
| `YaProgramListData`        | `Vst3PluginProxy`   | `IProgramListData`                                     |
| `YaUnitData`               | `Vst3PluginProxy`   | `IUnitData`                                            |
| `YaUnitInfo`               | `Vst3PluginProxy`   | `IUnitInfo`                                            |

VST3 host interfaces are implemented as follows:

| yabridge class              | Included in                 | Interfaces          |
| --------------------------- | --------------------------- | ------------------- |
| `Vst3HostContextProxy`      |                             | All of the below:   |
| `YaHostApplication`         | `Vst3HostContextProxy`      | `IHostApplication`  |
| `Vst3ComponentHandlerProxy` |                             | All of the below:   |
| `YaComponentHandler`        | `Vst3ComponentHandlerProxy` | `IComponentHandler` |
| `YaUnitHandler`             | `Vst3ComponentHandlerProxy` | `IUnitHandler`      |
| `Vst3PlugFrameProxy`        |                             | All of the below:   |
| `YaPlugFrame`               | `Vst3PlugFrameProxy`        | `IPlugFrame`        |

The following host interfaces are passed as function arguments and are thus also
implemented for serialization purposes:

| yabridge class       | Interfaces          | Notes                                                                  |
| -------------------- | ------------------- | ---------------------------------------------------------------------- |
| `YaAttributeList`    | `IAttributeList`    |                                                                        |
| `YaEventList`        | `IEventList`        | Comes with a lot of serialization wrappers around the related structs. |
| `YaMessage`          | `IMessage`          |                                                                        |
| `YaMessagePtr`       | `IMessage`          | Should be used in inter process communication to exchange messages     |
| `YaParameterChanges` | `IParameterChanges` |                                                                        |
| `YaParamValueQueue`  | `IParamValueQueue`  |                                                                        |
| `VectorStream`       | `IBStream`          | Used for serializing data streams.                                     |

And finally `YaProcessData` uses the above along with `YaAudioBusBuffers` to
wrap around `ProcessData`.
