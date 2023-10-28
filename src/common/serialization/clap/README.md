# CLAP extensions

See
[docs/architecture.md](https://github.com/robbert-vdh/yabridge/blob/master/docs/architecture.md)
for more information on how the serialization works.

Yabridge currently tracks CLAP 1.1.2. The implementation status for CLAP's core feature and extensions can be found below:

| core feature                              | status                                                   |
| ----------------------------------------- | -------------------------------------------------------- |
| Core plugin and host functionality        | :heavy_check_mark:                                       |
| Audio processing                          | :heavy_check_mark:                                       |
| Events                                    | :heavy_check_mark:                                       |
| Streams                                   | :heavy_check_mark:                                       |
| `clap.plugin-factory`                     | :heavy_check_mark:                                       |
| `clap.plugin-invalidation-factory/draft0` | :x: Will be supported once the extension gets stabilized |

| extension                 | status                                                                  |
| ------------------------- | ----------------------------------------------------------------------- |
| `clap.audio-ports`        | :heavy_check_mark:                                                      |
| `clap.audio-ports-config` | :heavy_check_mark:                                                      |
| `clap.event-registry`     | :heavy_exclamation_mark: Not needed for any of the supported extensions |
| `clap.gui`                | :heavy_check_mark: Currently only does embedded GUIs                    |
| `clap.latency`            | :heavy_check_mark:                                                      |
| `clap.log`                | :heavy_check_mark: Always supported with or without bridging            |
| `clap.note-name`          | :heavy_check_mark:                                                      |
| `clap.note-ports`         | :heavy_check_mark:                                                      |
| `clap.params`             | :heavy_check_mark:                                                      |
| `clap.posix-fd-support`   | :heavy_exclamation_mark: Not used by Windows plugins                    |
| `clap.render`             | :heavy_check_mark:                                                      |
| `clap.state`              | :heavy_check_mark:                                                      |
| `clap.tail`               | :heavy_check_mark:                                                      |
| `clap.thread-check`       | :heavy_check_mark: No bridging involved                                 |
| `clap.thread-pool`        | :x: Not supported yet                                                   |
| `clap.timer-support`      | :heavy_check_mark: No bridging involved                                 |
| `clap.voice-info`         | :heavy_check_mark:                                                      |

| draft extension                        | status                                                   |
| -------------------------------------- | -------------------------------------------------------- |
| `clap.ambisonic.draft*`                | :x: Will be supported once the extension gets stabilized |
| `clap.configurable-audio-ports.draft*` | :x: Will be supported once the extension gets stabilized |
| `clap.extensible-audio-ports.draft*`   | :x: Will be supported once the extension gets stabilized |
| `clap.check_for_update.draft*`         | :x: Will be supported once the extension gets stabilized |
| `clap.cv.draft*`                       | :x: Will be supported once the extension gets stabilized |
| `clap.file-reference.draft*`           | :x: Will be supported once the extension gets stabilized |
| `clap.midi-mappings.draft*`            | :x: Will be supported once the extension gets stabilized |
| `clap.preset-load.draft*`              | :x: Will be supported once the extension gets stabilized |
| `clap.quick-controls.draft*`           | :x: Will be supported once the extension gets stabilized |
| `clap.state-context.draft*`            | :x: Will be supported once the extension gets stabilized |
| `clap.surround.draft*`                 | :x: Will be supported once the extension gets stabilized |
| `clap.track-info.draft*`               | :x: Will be supported once the extension gets stabilized |
| `clap.transport-control.draft*`        | :x: Will be supported once the extension gets stabilized |
| `clap.tuning.draft*`                   | :x: Will be supported once the extension gets stabilized |
