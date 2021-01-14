# VST3 interfaces

See
[docs/vst3.md](https://github.com/robbert-vdh/yabridge/blob/master/docs/vst3.md)
for more information on how the serialization works.

The following interfaces are not yet implemented:

- Interfaces introduced after VST 3.6.6
- The [Presonus extensions](https://presonussoftware.com/en_US/developer),
  although most of these things seem to overlap with newer VST3 interfaces

VST3 plugin interfaces are implemented as follows:

| yabridge class                  | Included in         | Interfaces                                             |
| ------------------------------- | ------------------- | ------------------------------------------------------ |
| `YaPluginFactory`               |                     | `IPluginFactory`, `IPluginFactory2`, `IPluginFactory3` |
| `Vst3ConnectionPointProxy`      |                     | `IConnectionPoint` through `YaConnectionPoint`         |
| `Vst3PlugViewProxy`             |                     | All of the below:                                      |
| `YaParameterFinder`             | `Vst3PlugViewProxy` | `IParameterFinder`                                     |
| `YaPlugView`                    | `Vst3PlugViewProxy` | `IPlugView`                                            |
| `YaPlugViewContentScaleSupport` | `Vst3PlugViewProxy` | `IPlugViewContentScaleSupport`                         |
| `Vst3PluginProxy`               |                     | All of the below:                                      |
| `YaAudioPresentationLatency`    | `Vst3PluginProxy`   | `IAudioPresentationLatency`                            |
| `YaAudioProcessor`              | `Vst3PluginProxy`   | `IAudioProcessor`                                      |
| `YaAutomationState`             | `Vst3PluginProxy`   | `IAutomationState`                                     |
| `YaComponent`                   | `Vst3PluginProxy`   | `IComponent`                                           |
| `YaConnectionPoint`             | `Vst3PluginProxy`   | `IConnectionPoint`                                     |
| `YaEditController`              | `Vst3PluginProxy`   | `IEditController`                                      |
| `YaEditController2`             | `Vst3PluginProxy`   | `IEditController2`                                     |
| `YaEditControllerHostEditing`   | `Vst3PluginProxy`   | `IEditControllerHostEditing`                           |
| `YaInfoListener`                | `Vst3PluginProxy`   | `IInfoListener`                                        |
| `YaKeyswitchController`         | `Vst3PluginProxy`   | `IKeyswitchController`                                 |
| `YaMidiMapping`                 | `Vst3PluginProxy`   | `IMidiMapping`                                         |
| `YaNoteExpressionController`    | `Vst3PluginProxy`   | `INoteExpressionController`                            |
| `YaPluginBase`                  | `Vst3PluginProxy`   | `IPluginBase`                                          |
| `YaPrefetchableSupport`         | `Vst3PluginProxy`   | `IPrefetchableSupport`                                 |
| `YaProgramListData`             | `Vst3PluginProxy`   | `IProgramListData`                                     |
| `YaUnitData`                    | `Vst3PluginProxy`   | `IUnitData`                                            |
| `YaUnitInfo`                    | `Vst3PluginProxy`   | `IUnitInfo`                                            |
| `YaXmlRepresentationController` | `Vst3PluginProxy`   | `IXmlRepresentationController`                         |

VST3 host interfaces are implemented as follows:

| yabridge class              | Included in                 | Interfaces           |
| --------------------------- | --------------------------- | -------------------- |
| `Vst3HostContextProxy`      |                             | All of the below:    |
| `YaHostApplication`         | `Vst3HostContextProxy`      | `IHostApplication`   |
| `Vst3ComponentHandlerProxy` |                             | All of the below:    |
| `YaComponentHandler`        | `Vst3ComponentHandlerProxy` | `IComponentHandler`  |
| `YaComponentHandler2`       | `Vst3ComponentHandlerProxy` | `IComponentHandler2` |
| `YaComponentHandler3`       | `Vst3ComponentHandlerProxy` | `IComponentHandler3` |
| `YaUnitHandler`             | `Vst3ComponentHandlerProxy` | `IUnitHandler`       |
| `YaUnitHandler2`            | `Vst3ComponentHandlerProxy` | `IUnitHandler2`      |
| `Vst3ContextMenuProxy`      |                             | All of the below:    |
| `YaContextMenu`             | `Vst3ContextMenuProxy`      | `IContextMenu`       |
| `Vst3PlugFrameProxy`        |                             | All of the below:    |
| `YaPlugFrame`               | `Vst3PlugFrameProxy`        | `IPlugFrame`         |

The following host interfaces are passed as function arguments and are thus also
implemented for serialization purposes:

| yabridge class        | Interfaces                                         | Notes                                                                 |
| --------------------- | -------------------------------------------------- | --------------------------------------------------------------------- |
| `YaAttributeList`     | `IAttributeList`                                   |                                                                       |
| `YaBStream`           | `IBStream`, `ISizeableStream`, `IStreamAttributes` | Used for serializing data streams                                     |
| `YaContextMenuTarget` | `IContextMenuTarget`                               | Used in `YaContextMenu` to proxy specific menu items                  |
| `YaEventList`         | `IEventList`                                       | Comes with a lot of serialization wrappers around the related structs |
| `YaMessage`           | `IMessage`                                         |                                                                       |
| `YaMessagePtr`        | `IMessage`                                         | Should be used in inter process communication to exchange messages    |
| `YaParameterChanges`  | `IParameterChanges`                                |                                                                       |
| `YaParamValueQueue`   | `IParamValueQueue`                                 |                                                                       |

And finally `YaProcessData` uses the above along with `YaAudioBusBuffers` to
wrap around `ProcessData`.
