// yabridge: a Wine VST bridge
// Copyright (C) 2020-2022 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "plugins.h"

#include <fstream>
#include <sstream>

namespace fs = boost::filesystem;

LibArchitecture find_dll_architecture(const fs::path& plugin_path) {
    std::ifstream file(plugin_path, std::ifstream::binary | std::ifstream::in);

    // The linker will place the offset where the PE signature is placed at the
    // end of the MS-DOS stub, at offset 0x3c
    uint32_t pe_signature_offset;
    file.seekg(0x3c);
    file.read(reinterpret_cast<char*>(&pe_signature_offset),
              sizeof(pe_signature_offset));

    // The PE32 signature will be followed by a magic number that indicates the
    // target architecture of the binary
    uint32_t pe_signature;
    uint16_t machine_type;
    file.seekg(pe_signature_offset);
    file.read(reinterpret_cast<char*>(&pe_signature), sizeof(pe_signature));
    file.read(reinterpret_cast<char*>(&machine_type), sizeof(machine_type));

    constexpr char expected_pe_signature[4] = {'P', 'E', '\0', '\0'};
    if (pe_signature !=
        *reinterpret_cast<const uint32_t*>(expected_pe_signature)) {
        throw std::runtime_error("'" + plugin_path.string() +
                                 "' is not a valid .dll file");
    }

    // These constants are specified in
    // https://docs.microsoft.com/en-us/windows/win32/debug/pe-format#machine-types
    switch (machine_type) {
        case 0x014c:  // IMAGE_FILE_MACHINE_I386
            return LibArchitecture::dll_32;
            break;
        case 0x8664:  // IMAGE_FILE_MACHINE_AMD64
        case 0x0000:  // IMAGE_FILE_MACHINE_UNKNOWN
            return LibArchitecture::dll_64;
            break;
    }

    // When compiled without optimizations, GCC 9.3 will warn that the function
    // does not return if we put this in a `default:` case instead.
    std::ostringstream error_msg;
    error_msg << "'" << plugin_path
              << "' is neither a x86 nor a x86_64 PE32 file. Actual "
                 "architecture: 0x"
              << std::hex << machine_type;
    throw std::runtime_error(error_msg.str());
}

PluginType plugin_type_from_string(const std::string& plugin_type) noexcept {
    if (plugin_type == "VST2") {
        return PluginType::vst2;
    } else if (plugin_type == "VST3") {
        return PluginType::vst3;
    } else {
        return PluginType::unknown;
    }
}

std::string plugin_type_to_string(const PluginType& plugin_type) {
    // We'll capitalize the acronyms because this is also our human readable
    // format
    if (plugin_type == PluginType::vst2) {
        return "VST2";
    } else if (plugin_type == PluginType::vst3) {
        return "VST3";
    } else {
        return "<unknown>";
    }
}
