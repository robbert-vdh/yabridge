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

#ifdef WITH_ARA

#include "ara-main-factory-proxy.h"

#include <sstream>

#include "../../../common/serialization/vst3/ara-document-controller.h"
#include "../vst3.h"
#include "ara-document-controller-proxy.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
IMPLEMENT_FUNKNOWN_METHODS(YaMainFactoryImpl,
                           ARA::IMainFactory,
                           ARA::IMainFactory::iid)
#pragma GCC diagnostic pop

YaMainFactoryImpl::YaMainFactoryImpl(Vst3PluginBridge& bridge,
                                     YaAraFactory&& factory) noexcept
    : bridge_(bridge), factory_(std::move(factory)) {
    ara_factory_ = factory_.to_ara_factory(
        [this](const ARA::ARADocumentControllerHostInstance* hostInstance,
               const ARA::ARADocumentProperties* properties,
               native_size_t ara_dc_id)
            -> const ARA::ARADocumentControllerInstance* {
            try {
                const std::string doc_name =
                    (properties && properties->name) ? properties->name : "";

                std::ostringstream msg;
                msg << "[ARA] IMainFactory::createDocumentControllerWithDocument("
                    << "name=\"" << doc_name << "\", factoryID=\""
                    << factory_.factoryID << "\")";
                bridge_.logger_.log(msg.str());

                // Pre-register so host callbacks fired during
                // createDocumentControllerWithDocument can resolve ara_dc_id.
                const ARA::ARADocumentControllerInstance* dc_instance =
                    bridge_.register_ara_document_controller(
                        ara_dc_id, hostInstance, &ara_factory_);

                auto response = bridge_.send_message(
                    YaAra::CreateDocumentController{
                        .instance_id = 0,
                        .ara_dc_id = ara_dc_id,
                        .factory_id = factory_.factoryID,
                        .document_properties = YaAraDocumentProperties{
                            .name = doc_name}});

                return std::visit(
                    overload{
                        [&](uint64_t /*wine_dc_ref*/)
                            -> const ARA::ARADocumentControllerInstance* {
                            return dc_instance;
                        },
                        [&](const UniversalTResult&)
                            -> const ARA::ARADocumentControllerInstance* {
                            bridge_.logger_.log(
                                "WARNING: createDocumentControllerWithDocument() "
                                "returned null from Wine side");
                            bridge_.unregister_ara_document_controller(ara_dc_id);
                            return nullptr;
                        }},
                    std::move(response));            } catch (const std::exception& e) {
                bridge_.logger_.log(
                    std::string("WARNING: exception in "
                    "createDocumentControllerWithDocument(): ") + e.what());
                bridge_.unregister_ara_document_controller(ara_dc_id);
                return nullptr;
            } catch (...) {
                bridge_.logger_.log(
                    "WARNING: exception in "
                    "createDocumentControllerWithDocument(), returning null");
                bridge_.unregister_ara_document_controller(ara_dc_id);
                return nullptr;
            }
        });
}

YaMainFactoryImpl::~YaMainFactoryImpl() noexcept {
    factory_.unregister_factory();
}

const ARA::ARAFactory* PLUGIN_API YaMainFactoryImpl::getFactory() {
    return &ara_factory_;
}

#endif  // WITH_ARA
