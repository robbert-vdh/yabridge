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

#include "message.h"

YaMessagePtr::YaMessagePtr(){FUNKNOWN_CTOR}

YaMessagePtr::YaMessagePtr(IMessage& message)
    : message_id(message.getMessageID()
                     ? std::make_optional<std::string>(message.getMessageID())
                     : std::nullopt),
      original_message_ptr(static_cast<native_size_t>(
          reinterpret_cast<size_t>(&message))){FUNKNOWN_CTOR}

      YaMessagePtr::~YaMessagePtr() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_FUNKNOWN_METHODS(YaMessagePtr,
                           Steinberg::Vst::IMessage,
                           Steinberg::Vst::IMessage::iid)
#pragma GCC diagnostic pop

Steinberg::Vst::IMessage* YaMessagePtr::get_original() const {
    // See the docstrings on `YaMessage` and `YaMessagePtr`
    return reinterpret_cast<IMessage*>(
        static_cast<size_t>(original_message_ptr));
}

Steinberg::FIDString PLUGIN_API YaMessagePtr::getMessageID() {
    if (message_id) {
        return message_id->c_str();
    } else {
        return nullptr;
    }
}

void PLUGIN_API YaMessagePtr::setMessageID(Steinberg::FIDString id /*in*/) {
    if (id) {
        message_id = id;
    } else {
        message_id.reset();
    }
}

Steinberg::Vst::IAttributeList* PLUGIN_API YaMessagePtr::getAttributes() {
    return &attribute_list;
}

YaMessage::YaMessage(){FUNKNOWN_CTOR}

YaMessage::~YaMessage() {
    FUNKNOWN_DTOR
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_FUNKNOWN_METHODS(YaMessage,
                           Steinberg::Vst::IMessage,
                           Steinberg::Vst::IMessage::iid)
#pragma GCC diagnostic pop

Steinberg::FIDString PLUGIN_API YaMessage::getMessageID() {
    if (message_id) {
        return message_id->c_str();
    } else {
        return nullptr;
    }
}

void PLUGIN_API YaMessage::setMessageID(Steinberg::FIDString id /*in*/) {
    if (id) {
        message_id = id;
    } else {
        message_id.reset();
    }
}

Steinberg::Vst::IAttributeList* PLUGIN_API YaMessage::getAttributes() {
    return &attribute_list;
}
