// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include "pluginterfaces/vst/ivstchannelcontextinfo.h"

/**
 * Keys for channel context attributes passed in
 * `IInfoListener::setChannelContextInfos` that contain a string value.
 */
const static char* channel_context_string_keys[] = {
    Steinberg::Vst::ChannelContext::kChannelUIDKey,
    Steinberg::Vst::ChannelContext::kChannelNameKey,
    Steinberg::Vst::ChannelContext::kChannelIndexNamespaceKey};

/**
 * Keys for channel context attributes passed in
 * `IInfoListener::setChannelContextInfos` that contain an integer value.
 */
const static char* channel_context_integer_keys[] = {
    Steinberg::Vst::ChannelContext::kChannelUIDLengthKey,
    Steinberg::Vst::ChannelContext::kChannelNameLengthKey,
    Steinberg::Vst::ChannelContext::kChannelColorKey,
    Steinberg::Vst::ChannelContext::kChannelIndexKey,
    Steinberg::Vst::ChannelContext::kChannelIndexNamespaceOrderKey,
    Steinberg::Vst::ChannelContext::kChannelIndexNamespaceLengthKey,
    Steinberg::Vst::ChannelContext::kChannelPluginLocationKey};

/**
 * Keys for channel context attributes passed in
 * `IInfoListener::setChannelContextInfos` that contain a binary value.
 */
const static char* channel_context_binary_keys[] = {
    Steinberg::Vst::ChannelContext::kChannelImageKey};

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

tresult YaAttributeList::write_back(
    Steinberg::Vst::IAttributeList* stream) const {
    if (!stream) {
        return Steinberg::kInvalidArgument;
    }

    for (const auto& [key, value] : attrs_int) {
        stream->setInt(key.c_str(), value);
    }
    for (const auto& [key, value] : attrs_float) {
        stream->setFloat(key.c_str(), value);
    }
    for (const auto& [key, value] : attrs_string) {
        stream->setString(key.c_str(), u16string_to_tchar_pointer(value));
    }
    for (const auto& [key, value] : attrs_binary) {
        stream->setBinary(key.c_str(), value.data(), value.size());
    }

    return Steinberg::kResultOk;
}

YaAttributeList YaAttributeList::read_channel_context(
    Steinberg::Vst::IAttributeList* context) {
    YaAttributeList attributes{};
    // Copy over all predefined channel context attributes. `IAttributeList`
    // does not offer any interface to enumerate the stored keys.
    Steinberg::Vst::String128 vst_string{0};
    for (const auto& key : channel_context_string_keys) {
        vst_string[0] = 0;
        if (context->getString(key, vst_string, sizeof(vst_string)) ==
            Steinberg::kResultOk) {
            attributes.setString(key, vst_string);
        }
    }

    int64 vst_integer;
    for (const auto& key : channel_context_integer_keys) {
        if (context->getInt(key, vst_integer) == Steinberg::kResultOk) {
            attributes.setInt(key, vst_integer);
        }
    }

    const void* vst_binary_ptr;
    uint32 vst_binary_size;
    for (const auto& key : channel_context_binary_keys) {
        if (context->getBinary(key, vst_binary_ptr, vst_binary_size) ==
            Steinberg::kResultOk) {
            attributes.setBinary(key, vst_binary_ptr, vst_binary_size);
        }
    }

    return attributes;
}

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
