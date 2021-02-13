# VST3 interfaces

See
[docs/architecture.md](https://github.com/robbert-vdh/yabridge/blob/master/docs/architecture.md)
for more information on how the serialization works.

We currently support all official VST 3.7.1 interfaces.

VST3 plugin interfaces are implemented as follows:

| yabridge class                      | Included in         | Interfaces                                             |
| ----------------------------------- | ------------------- | ------------------------------------------------------ |
| `Vst3PluginFactoryProxy`            |                     | All of the below:                                      |
| `YaPluginFactory3`                  |                     | `IPluginFactory`, `IPluginFactory2`, `IPluginFactory3` |
| `Vst3ConnectionPointProxy`          |                     | `IConnectionPoint` through `YaConnectionPoint`         |
| `Vst3PlugViewProxy`                 |                     | All of the below:                                      |
| `YaParameterFinder`                 | `Vst3PlugViewProxy` | `IParameterFinder`                                     |
| `YaPlugView`                        | `Vst3PlugViewProxy` | `IPlugView`                                            |
| `YaPlugViewContentScaleSupport`     | `Vst3PlugViewProxy` | `IPlugViewContentScaleSupport`                         |
| `Vst3PluginProxy`                   |                     | All of the below:                                      |
| `YaAudioPresentationLatency`        | `Vst3PluginProxy`   | `IAudioPresentationLatency`                            |
| `YaAudioProcessor`                  | `Vst3PluginProxy`   | `IAudioProcessor`                                      |
| `YaAutomationState`                 | `Vst3PluginProxy`   | `IAutomationState`                                     |
| `YaComponent`                       | `Vst3PluginProxy`   | `IComponent`                                           |
| `YaConnectionPoint`                 | `Vst3PluginProxy`   | `IConnectionPoint`                                     |
| `YaEditController`                  | `Vst3PluginProxy`   | `IEditController`                                      |
| `YaEditController2`                 | `Vst3PluginProxy`   | `IEditController2`                                     |
| `YaEditControllerHostEditing`       | `Vst3PluginProxy`   | `IEditControllerHostEditing`                           |
| `YaInfoListener`                    | `Vst3PluginProxy`   | `IInfoListener`                                        |
| `YaKeyswitchController`             | `Vst3PluginProxy`   | `IKeyswitchController`                                 |
| `YaMidiLearn`                       | `Vst3PluginProxy`   | `IMidiLearn`                                           |
| `YaMidiMapping`                     | `Vst3PluginProxy`   | `IMidiMapping`                                         |
| `YaNoteExpressionController`        | `Vst3PluginProxy`   | `INoteExpressionController`                            |
| `YaNoteExpressionPhysicalUIMapping` | `Vst3PluginProxy`   | `INoteExpressionPhysicalUIMapping`                     |
| `YaParameterFunctionName`           | `Vst3PluginProxy`   | `IParameterFunctionName`                               |
| `YaPluginBase`                      | `Vst3PluginProxy`   | `IPluginBase`                                          |
| `YaProcessContextRequirements`      | `Vst3PluginProxy`   | `IProcessContextRequirements`                          |
| `YaPrefetchableSupport`             | `Vst3PluginProxy`   | `IPrefetchableSupport`                                 |
| `YaProgramListData`                 | `Vst3PluginProxy`   | `IProgramListData`                                     |
| `YaUnitData`                        | `Vst3PluginProxy`   | `IUnitData`                                            |
| `YaUnitInfo`                        | `Vst3PluginProxy`   | `IUnitInfo`                                            |
| `YaXmlRepresentationController`     | `Vst3PluginProxy`   | `IXmlRepresentationController`                         |

VST3 host interfaces are implemented as follows:

| yabridge class                    | Included in                 | Interfaces                       |
| --------------------------------- | --------------------------- | -------------------------------- |
| `Vst3HostContextProxy`            |                             | All of the below:                |
| `YaHostApplication`               | `Vst3HostContextProxy`      | `IHostApplication`               |
| `YaPlugInterfaceSupport`          | `Vst3HostContextProxy`      | `IPlugInterfaceSupport`          |
| `Vst3ComponentHandlerProxy`       |                             | All of the below:                |
| `YaComponentHandler`              | `Vst3ComponentHandlerProxy` | `IComponentHandler`              |
| `YaComponentHandler2`             | `Vst3ComponentHandlerProxy` | `IComponentHandler2`             |
| `YaComponentHandler3`             | `Vst3ComponentHandlerProxy` | `IComponentHandler3`             |
| `YaComponentHandlerBusActivation` | `Vst3ComponentHandlerProxy` | `IComponentHandlerBusActivation` |
| `YaProgress`                      | `Vst3ComponentHandlerProxy` | `IProgress`                      |
| `YaUnitHandler`                   | `Vst3ComponentHandlerProxy` | `IUnitHandler`                   |
| `YaUnitHandler2`                  | `Vst3ComponentHandlerProxy` | `IUnitHandler2`                  |
| `Vst3ContextMenuProxy`            |                             | All of the below:                |
| `YaContextMenu`                   | `Vst3ContextMenuProxy`      | `IContextMenu`                   |
| `Vst3PlugFrameProxy`              |                             | All of the below:                |
| `YaPlugFrame`                     | `Vst3PlugFrameProxy`        | `IPlugFrame`                     |

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
