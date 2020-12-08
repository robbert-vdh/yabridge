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

#include "vst3.h"

#include <sstream>
#include "src/common/serialization/vst3.h"

// TODO: Reconsider the output format
// TODO: Maybe think of an alterantive that's a little less boilerplaty

Vst3Logger::Vst3Logger(Logger& generic_logger) : logger(generic_logger) {}

void Vst3Logger::log_request(bool is_host_vst, const YaComponent::Create&) {
    if (BOOST_UNLIKELY(logger.verbosity >= Logger::Verbosity::most_events)) {
        std::ostringstream message;
        // TODO: Log the cid in some readable way, if possible
        message << get_log_prefix(is_host_vst)
                << " >> IPluginFactory::createComponent(cid, IComponent::iid, "
                   "&obj)";

        log(message.str());
    }
}

void Vst3Logger::log_request(bool is_host_vst, const WantsConfiguration&) {
    if (BOOST_UNLIKELY(logger.verbosity >= Logger::Verbosity::most_events)) {
        std::ostringstream message;
        message << get_log_prefix(is_host_vst)
                << " >> Requesting <Configuration>";

        log(message.str());
    }
}

void Vst3Logger::log_request(bool is_host_vst, const WantsPluginFactory&) {
    if (BOOST_UNLIKELY(logger.verbosity >= Logger::Verbosity::most_events)) {
        std::ostringstream message;
        message << get_log_prefix(is_host_vst)
                << " >> Requesting <IPluginFactory*>";

        log(message.str());
    }
}

void Vst3Logger::log_response(bool is_host_vst, const Configuration&) {
    if (BOOST_UNLIKELY(logger.verbosity >= Logger::Verbosity::most_events)) {
        std::ostringstream message;
        message << get_log_prefix(is_host_vst) << "    <Configuration>";

        log(message.str());
    }
}

void Vst3Logger::log_response(bool is_host_vst, const YaComponent&) {
    if (BOOST_UNLIKELY(logger.verbosity >= Logger::Verbosity::most_events)) {
        std::ostringstream message;
        // TODO: Add the instance ID after we implement that
        message << get_log_prefix(is_host_vst) << "    <IComponent*>";

        log(message.str());
    }
}

void Vst3Logger::log_response(bool is_host_vst,
                              const YaPluginFactory& factory) {
    if (BOOST_UNLIKELY(logger.verbosity >= Logger::Verbosity::most_events)) {
        std::ostringstream message;
        message << get_log_prefix(is_host_vst) << "    <IPluginFactory*> with "
                << const_cast<YaPluginFactory&>(factory).countClasses()
                << " registered classes";

        log(message.str());
    }
}
