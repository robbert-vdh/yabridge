// yabridge: a Wine VST bridge
// Copyright (C) 2020  Robbert van der Helm
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

#include "attribute-list.h"

YaAttributeList::YaAttributeList(){FUNKNOWN_CTOR}

YaAttributeList::~YaAttributeList() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_FUNKNOWN_METHODS(YaAttributeList,
                           Steinberg::Vst::IAttributeList,
                           Steinberg::Vst::IAttributeList::iid)
#pragma GCC diagnostic pop

tresult PLUGIN_API YaAttributeList::setInt(AttrID id, int64 value) {
    attrs_int[id] = value;
    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaAttributeList::getInt(AttrID id, int64& value) {
    if (const auto it = attrs_int.find(id); it != attrs_int.end()) {
        value = it->second;
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

tresult PLUGIN_API YaAttributeList::setFloat(AttrID id, double value) {
    attrs_float[id] = value;
    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaAttributeList::getFloat(AttrID id, double& value) {
    if (const auto it = attrs_float.find(id); it != attrs_float.end()) {
        value = it->second;
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

tresult PLUGIN_API
YaAttributeList::setString(AttrID id, const Steinberg::Vst::TChar* string) {
    if (!string) {
        return Steinberg::kInvalidArgument;
    }

    attrs_string[id] = tchar_pointer_to_u16string(string);
    return Steinberg::kResultOk;
}

tresult PLUGIN_API YaAttributeList::getString(AttrID id,
                                              Steinberg::Vst::TChar* string,
                                              uint32 sizeInBytes) {
    if (!string) {
        return Steinberg::kInvalidArgument;
    }

    if (const auto it = attrs_string.find(id); it != attrs_string.end()) {
        // We may only copy `sizeInBytes / 2` UTF-16 characters to `string`,
        // We'll also have to make sure it's null terminated, so we'll reserve
        // another byte for that.
        const size_t copy_characters = std::min(
            (static_cast<size_t>(sizeInBytes) / sizeof(Steinberg::Vst::TChar)) -
                1,
            it->second.size());
        std::copy_n(it->second.begin(), copy_characters, string);
        string[copy_characters] = 0;

        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}

tresult PLUGIN_API YaAttributeList::setBinary(AttrID id,
                                              const void* data,
                                              uint32 sizeInBytes) {
    if (!data) {
        return Steinberg::kInvalidArgument;
    }

    const uint8_t* data_bytes = static_cast<const uint8_t*>(data);
    attrs_binary[id].assign(data_bytes, data_bytes + sizeInBytes);
    return Steinberg::kResultOk;
}
tresult PLUGIN_API YaAttributeList::getBinary(AttrID id,
                                              const void*& data,
                                              uint32& sizeInBytes) {
    if (const auto it = attrs_binary.find(id); it != attrs_binary.end()) {
        data = it->second.data();
        sizeInBytes = it->second.size();
        return Steinberg::kResultOk;
    } else {
        return Steinberg::kResultFalse;
    }
}
