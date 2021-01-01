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

#pragma once

#include <bitsery/ext/std_optional.h>
#include <pluginterfaces/vst/ivstmessage.h>

#include "../common.h"
#include "attribute-list.h"
#include "base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * A serialization wrapper around `IMessage`. As explained in `YaMessage`, we
 * can't exchange the regular `YaMessage` object when dealing with
 * `IConnectionPoint` connection proxies. Instead, we'll use this wrapper that
 * only stores the ID (for logging purposes) and a pointer to the original
 * object. That way we can pass the original message created by the plugin to
 * the receiver without having to know what object the host's connection proxy
 * is actually connecting us to.
 *
 * @note THis object should _not_ be passed to the plugin directly. The only
 *   purpose of this object is to be able to pass the original `IMessage*`
 *   object passed the connection proxy to the receiver, by wrapping a pointer
 *   to it in this object. `YaMessagePtr::get_original()` can be used to
 *   retrieve the original object.
 */
class YaMessagePtr : public Steinberg::Vst::IMessage {
   public:
    YaMessagePtr();

    /**
     * Create a proxy for this message. We'll store the message's ID for logging
     * purposes as well as a pointer to it so we can retrieve the object after a
     * round trip from the Wine plugin host, to the native plugin, to the host,
     * back to the native plugin, and then finally back to the Wine plugin host
     * again.
     */
    explicit YaMessagePtr(IMessage& message);

    ~YaMessagePtr();

    DECLARE_FUNKNOWN_METHODS

    /**
     * Get back a pointer to the original `IMessage` object passed to the
     * constructor. This should be used on the Wine plugin host side when
     * handling `IConnectionPoint::notify`.
     */
    Steinberg::Vst::IMessage* get_original() const;

    virtual Steinberg::FIDString PLUGIN_API getMessageID() override;
    virtual void PLUGIN_API
    setMessageID(Steinberg::FIDString id /*in*/) override;
    virtual Steinberg::Vst::IAttributeList* PLUGIN_API getAttributes() override;

    template <typename S>
    void serialize(S& s) {
        s.ext(message_id, bitsery::ext::StdOptional{},
              [](S& s, std::string& id) { s.text1b(id, 1024); });
        s.value8b(original_message_ptr);
    }

   private:
    /**
     * The implementation that comes with the SDK returns a null pointer when
     * the ID has not yet been set, so we'll do the same thing.
     */
    std::optional<std::string> message_id;

    /**
     * The pointer to the message passed during the constructor, as a 64-bit
     * unsigned integer. This way we can retrieve the original object after a
     * round trip.
     */
    native_size_t original_message_ptr = 0;

    /**
     * An empty attribute list, in case the host checks this for some reason.
     */
    YaAttributeList attribute_list;
};

/**
 * A `IMessage` implementation the plugin can use to exchange messages with. We
 * create instances of these in `IHostApplication::createInstance()` so the
 * Windows VST3 plugin can send messages between objects. A plugin's controller
 * or processor will fill the message with data and then try to send it to the
 * connected object using `IConnectionPoint::notify()`. For directly connected
 * objects this works exactly like you'd expect. When the host places a proxy
 * between the two, it becomes a bit more interesting, and we'll have to proxy
 * that proxy. In that case we won't send the actual `YaMessage` object from the
 * Wine plugin host to the native plugin, and then back to the Wine plugin host.
 * Instead, we'll send a thin wrapper that only stores a name and a pointer to
 * the actual object. This is needed in case the plugin tries to store the
 * `IMessage` object, thinking it's backed by a smart pointer. This means that
 * the message we pass while handling `IConnectionPoint::notify` should live as
 * long as the original message object, thus we'll use a pointer to get back the
 * original message object.
 *
 * @relates YaMessagePtr
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

   private:
    /**
     * The implementation that comes with the SDK returns a null pointer when
     * the ID has not yet been set, so we'll do the same thing.
     */
    std::optional<std::string> message_id;

    YaAttributeList attribute_list;
};
