#include "bridge.h"

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/process/io.hpp>
#include <boost/process/search_path.hpp>
#include <iostream>
#include <msgpack.hpp>

#include "../common/communication.h"

// TODO: I should track down the VST2 SDK for clarification on some of the
//       implementation details, such as the use of intptr_t isntead of void*
//       here.

namespace bp = boost::process;
namespace fs = boost::filesystem;

constexpr auto yabridge_wine_host_name = "yabridge-host.exe";

fs::path find_wine_vst_host();

Bridge::Bridge()
    : vst_stdin(),
      vst_stdout(),
      vst_host(find_wine_vst_host(),
               bp::std_in = vst_stdin,
               bp::std_out = vst_stdout) {
    // TODO: Wineprefix detection
}

/**
 * Handle an event sent by the VST host. Most of these opcodes will be passed
 * through to the winelib VST host.
 */
intptr_t Bridge::dispatch(AEffect* /*plugin*/,
                          int32_t opcode,
                          int32_t parameter,
                          intptr_t value,
                          void* result,
                          float option) {
    // Some events need some extra handling
    // TODO: Handle other things such as GUI itneraction
    switch (opcode) {
        case effClose:
            // TODO: Gracefully close the editor?
            // XXX: Boost.Process will send SIGKILL to the process for us, is
            //      there a way to manually send a SIGTERM signal instead?

            // The VST API does not have an explicit function for releasing
            // resources, so we'll have to do it here. The actual plugin
            // instance gets freed by the host, or at least I think it does.
            delete this;

            return 0;
            break;
    }

    Event event{opcode, parameter, value, option};
    write_object(vst_stdin, event);

    auto response = read_object<EventResult>(vst_stdout);
    if (response.result) {
        std::copy(response.result->begin(), response.result->end(),
                  static_cast<char*>(result));
    }

    return response.return_value;
}

void Bridge::process(AEffect* /*plugin*/,
                     float** /*inputs*/,
                     float** /*outputs*/,
                     int32_t /*sample_frames*/) {
    // TODO: Unimplmemented
}

void Bridge::set_parameter(AEffect* /*plugin*/,
                           int32_t /*index*/,
                           float /*value*/) {
    // TODO: Unimplmemented
}

float Bridge::get_parameter(AEffect* /*plugin*/, int32_t /*index*/
) {
    // TODO: Unimplmemented
    return 0.0f;
}

/**
 * Finds the Wine VST hsot (named `yabridge-host.exe`). For this we will search
 * in two places:
 *
 *   1. Alongside libyabridge.so if the file got symlinked. This is useful
 *      when developing, as you can simply symlink the the libyabridge.so
 *      file in the build directory without having to install anything to
 *      /usr.
 *   2. In the regular search path.
 *
 * @return The a path to the VST host, if found.
 * @throw std::runtime_error If the Wine VST host could not be found.
 */
fs::path find_wine_vst_host() {
    fs::path host_path = fs::canonical(boost::dll::this_line_location());
    host_path.remove_filename().append(yabridge_wine_host_name);
    if (fs::exists(host_path)) {
        return host_path;
    }

    // Bosot will return an empty path if the file could not be found in the
    // search path
    fs::path vst_host_path = bp::search_path(yabridge_wine_host_name);
    if (vst_host_path == "") {
        throw std::runtime_error("Could not locate '" +
                                 std::string(yabridge_wine_host_name) + "'");
    }

    return vst_host_path;
}
