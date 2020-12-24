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

#include "message.h"

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
