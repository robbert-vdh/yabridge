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

#pragma once

#include <bitsery/ext/std_optional.h>
#include <pluginterfaces/vst/ivstmessage.h>

#include "attribute-list.h"
#include "base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IMessage` for serialization purposes. We create instances of
 * these in `IHostApplication::createInstance()` so the Windows VST3 plugin can
 * send messages between objects. There is one huge caveat here: it is
 * impossible to work with arbitrary `IMessage` objects, since there's no way to
 * retrieve all of the keys in the attribute list. With this approach we support
 * hosts that indirectly connect the processor and the controller through a
 * proxy (like Ardour), but we still require a dynamic cast from the `IMessage*`
 * passed to `YaConnectionPoint::notify()` to a `YaMessage*` for this to work
 * for the above mentioned reason.
 */
class YaMessage : public Steinberg::Vst::IMessage {
   public:
    /**
     * Default constructor with an empty message. The plugin can use this to
     * write a message.
     */
    YaMessage();

    ~YaMessage();

    DECLARE_FUNKNOWN_METHODS

    virtual Steinberg::FIDString PLUGIN_API getMessageID() override;
    virtual void PLUGIN_API
    setMessageID(Steinberg::FIDString id /*in*/) override;
    virtual Steinberg::Vst::IAttributeList* PLUGIN_API getAttributes() override;

    template <typename S>
    void serialize(S& s) {
        s.ext(message_id, bitsery::ext::StdOptional{},
              [](S& s, std::string& id) { s.text1b(id, 1024); });
        s.object(attribute_list);
    }

   private:
    /**
     * The implementation that comes with the SDK returns a null pointer when
     * the ID has not yet been set, so we'll do the same thing.
     */
    std::optional<std::string> message_id;

    YaAttributeList attribute_list;
};
