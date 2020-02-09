#pragma once

// Libraries like Boost and msgpack think we're compiling on Windows or using a
// MSVC toolchain. This will cause them to make assumptions about the way
// certain types are defined, which headers are available and which features to
// disable (i.e. POSIX specific features). The only way around this I could
// think of was to just temporarily undefine the macros these libraries use to
// detect it's running under a WIN32 environment. If anyone knows a better way
// to do this, please let me know!

#pragma push_macro("WIN32")
#pragma push_macro("_WIN32")
#pragma push_macro("__WIN32__")
#pragma push_macro("_WIN64")

#undef WIN32
#undef _WIN32
#undef __WIN32__
#undef _WIN64

#include <msgpack.hpp>

#pragma pop_macro("WIN32")
#pragma pop_macro("_WIN32")
#pragma pop_macro("__WIN32__")
#pragma pop_macro("_WIN64")
