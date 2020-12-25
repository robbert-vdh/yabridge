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

#include <unordered_map>

#include <bitsery/ext/std_map.h>
#include <bitsery/ext/std_optional.h>
#include <pluginterfaces/vst/ivstmessage.h>

#include "base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IAttributeList` for storing parameters in `YaMessage`.
 */
class YaAttributeList : public Steinberg::Vst::IAttributeList {
   public:
    /**
     * Default constructor with an empty attributeList. The plugin can use this
     * to write a attributeList.
     */
    YaAttributeList();

    ~YaAttributeList();

    DECLARE_FUNKNOWN_METHODS

    virtual tresult PLUGIN_API setInt(AttrID id, int64 value) override;
    virtual tresult PLUGIN_API getInt(AttrID id, int64& value) override;
    virtual tresult PLUGIN_API setFloat(AttrID id, double value) override;
    virtual tresult PLUGIN_API getFloat(AttrID id, double& value) override;
    virtual tresult PLUGIN_API
    setString(AttrID id, const Steinberg::Vst::TChar* string) override;
    virtual tresult PLUGIN_API getString(AttrID id,
                                         Steinberg::Vst::TChar* string,
                                         uint32 sizeInBytes) override;
    virtual tresult PLUGIN_API setBinary(AttrID id,
                                         const void* data,
                                         uint32 sizeInBytes) override;
    virtual tresult PLUGIN_API getBinary(AttrID id,
                                         const void*& data,
                                         uint32& sizeInBytes) override;

    template <typename S>
    void serialize(S& s) {
        s.ext(attrs_int, bitsery::ext::StdMap{1 << 20},
              [](S& s, std::string& key, int64& value) {
                  s.text1b(key, 1024);
                  s.value8b(value);
              });
        s.ext(attrs_float, bitsery::ext::StdMap{1 << 20},
              [](S& s, std::string& key, double& value) {
                  s.text1b(key, 1024);
                  s.value8b(value);
              });
        s.ext(attrs_string, bitsery::ext::StdMap{1 << 20},
              [](S& s, std::string& key, std::u16string& value) {
                  s.text1b(key, 1024);
                  s.text2b(value, 1 << 20);
              });
        s.ext(attrs_binary, bitsery::ext::StdMap{1 << 20},
              [](S& s, std::string& key, std::vector<uint8_t>& value) {
                  s.text1b(key, 1024);
                  s.container1b(value, 1 << 20);
              });
    }

   private:
    std::unordered_map<std::string, int64> attrs_int;
    std::unordered_map<std::string, double> attrs_float;
    std::unordered_map<std::string, std::u16string> attrs_string;
    std::unordered_map<std::string, std::vector<uint8_t>> attrs_binary;
};
