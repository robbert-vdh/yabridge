# yabridge

Yet Another way to use Windows VST2 plugins in Linux VST hosts.

## TODOs

There are a few things that should be done before making this public, including:

- Document and improve the installation and updating procedure.
- Document the project setup and the way communication works.
- Document what this has been tested on and what does or does not work.
- Document wine32 support.

## Building

To compile yabridge, you'll need [Meson](https://mesonbuild.com/index.html) and
the following dependencies:

- gcc (tested using GCC 9.2)
- A Wine installation with `wiengcc` and the development headers.
- [msgpack-c](git@github.com:msgpack/msgpack-c.git)

The project can then be compiled as follows:

```shell
meson setup --buildtype=release --cross-file cross-wine64.conf build
ninja -C build
```

When developing or debugging yabridge you can change the build type to either
`debug` or `debugoptimized` to enable debug symbols and optionally also disable
optimizations.
