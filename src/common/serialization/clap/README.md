# CLAP extensions

See
[docs/architecture.md](https://github.com/robbert-vdh/yabridge/blob/master/docs/architecture.md)
for more information on how the serialization works.

Yabridge currently tracks CLAP 1.1.1. The implementation status for CLAP's core feature and extensions can be found below:

| core feature                              | status                                                 |
| ----------------------------------------- | ------------------------------------------------------ |
| Core plugin and host functionality        | :warning: Everything but actual audio processing works |
| Audio processing                          | :heavy_check_mark:                                     |
| Events                                    | :heavy_check_mark:                                     |
| Streams                                   | :heavy_check_mark:                                     |
| `clap.plugin-factory`                     | :heavy_check_mark:                                     |
| `clap.plugin-invalidation-factory/draft0` | :x: Not supported yet                                  |

| extension                 | status                                                       |
| ------------------------- | ------------------------------------------------------------ |
| `clap.audio-ports`        | :heavy_check_mark:                                           |
| `clap.audio-ports-config` | :x: Not supported yet                                        |
| `clap.event-registry`     | :x: Not needed for any of the supported extensions           |
| `clap.gui`                | :heavy_check_mark: Currently only does embedded GUIs         |
| `clap.latency`            | :heavy_check_mark:                                           |
| `clap.log`                | :heavy_check_mark: Always supported with or without bridging |
| `clap.note-name`          | :x: Not supported yet                                        |
| `clap.note-ports`         | :heavy_check_mark:                                           |
| `clap.params`             | :heavy_check_mark:                                           |
| `clap.posix-fd-support`   | :x: Not supported yet                                        |
| `clap.render`             | :heavy_check_mark:                                           |
| `clap.state`              | :heavy_check_mark:                                           |
| `clap.tail`               | :heavy_check_mark:                                           |
| `clap.thread-check`       | :heavy_check_mark: No bridging involved                      |
| `clap.thread-pool`        | :x: Not supported yet                                        |
| `clap.timer-support`      | :x: Not supported yet                                        |
| `clap.voice-info`         | :heavy_check_mark:                                           |

| draft extension                  | status                |
| -------------------------------- | --------------------- |
| `clap.ambisonic.draft/0`         | :x: Not supported yet |
| `clap.check_for_update.draft/0`  | :x: Not supported yet |
| `clap.cv.draft/0`                | :x: Not supported yet |
| `clap.file-reference.draft/0`    | :x: Not supported yet |
| `clap.midi-mappings.draft/0`     | :x: Not supported yet |
| `clap.preset-load.draft/0`       | :x: Not supported yet |
| `clap.quick-controls.draft/0`    | :x: Not supported yet |
| `clap.state-context.draft/1`     | :x: Not supported yet |
| `clap.surround.draft/1`          | :x: Not supported yet |
| `clap.track-info.draft/0`        | :x: Not supported yet |
| `clap.transport-control.draft/0` | :x: Not supported yet |
| `clap.tuning.draft/2`            | :x: Not supported yet |
