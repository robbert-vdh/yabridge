// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2026 Robbert van der Helm
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

#ifdef WITH_ARA

#include <ARAVST3.h>

#include "../../../common/serialization/vst3/plugin/ara-factory.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

class Vst3PluginBridge;

class YaMainFactoryImpl : public ARA::IMainFactory {
   public:
    YaMainFactoryImpl(Vst3PluginBridge& bridge,
                      YaAraFactory&& factory) noexcept;

    virtual ~YaMainFactoryImpl() noexcept;

    YaMainFactoryImpl(const YaMainFactoryImpl&) = delete;
    YaMainFactoryImpl& operator=(const YaMainFactoryImpl&) = delete;
    YaMainFactoryImpl(YaMainFactoryImpl&&) = delete;
    YaMainFactoryImpl& operator=(YaMainFactoryImpl&&) = delete;

    DECLARE_FUNKNOWN_METHODS

    const ARA::ARAFactory* PLUGIN_API getFactory() override;

   private:
    Vst3PluginBridge& bridge_;
    YaAraFactory factory_;
    ARA::ARAFactory ara_factory_;
};

#pragma GCC diagnostic pop

#endif  // WITH_ARA
