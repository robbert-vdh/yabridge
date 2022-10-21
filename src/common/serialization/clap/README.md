# CLAP extensions

See
[docs/architecture.md](https://github.com/robbert-vdh/yabridge/blob/master/docs/architecture.md)
for more information on how the serialization works.

Yabridge currently tracks CLAP 1.1.1. The implementation status for CLAP's core feature and extensions can be found below:

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
| `clap.timer-support`      | :x: Not supported yet                                                   |
| `clap.voice-info`         | :heavy_check_mark:                                                      |

| draft extension                  | status                                                   |
| -------------------------------- | -------------------------------------------------------- |
| `clap.ambisonic.draft/0`         | :x: Will be supported once the extension gets stabilized |
| `clap.check_for_update.draft/0`  | :x: Will be supported once the extension gets stabilized |
| `clap.cv.draft/0`                | :x: Will be supported once the extension gets stabilized |
| `clap.file-reference.draft/0`    | :x: Will be supported once the extension gets stabilized |
| `clap.midi-mappings.draft/0`     | :x: Will be supported once the extension gets stabilized |
| `clap.preset-load.draft/0`       | :x: Will be supported once the extension gets stabilized |
| `clap.quick-controls.draft/0`    | :x: Will be supported once the extension gets stabilized |
| `clap.state-context.draft/1`     | :x: Will be supported once the extension gets stabilized |
| `clap.surround.draft/1`          | :x: Will be supported once the extension gets stabilized |
| `clap.track-info.draft/0`        | :x: Will be supported once the extension gets stabilized |
| `clap.transport-control.draft/0` | :x: Will be supported once the extension gets stabilized |
| `clap.tuning.draft/2`            | :x: Will be supported once the extension gets stabilized |
