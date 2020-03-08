#include "communication.h"

intptr_t send_event(boost::asio::local::stream_protocol::socket& socket,
                    bool is_dispatch,
                    int opcode,
                    int index,
                    intptr_t value,
                    void* data,
                    float option,
                    Logger* logger) {
    // Encode the right payload type for this event. Check the documentation for
    // `EventPayload` for more information.
    EventPayload payload = nullptr;
    if (data != nullptr) {
        // There are some events that need specific structs that we can't simply
        // serialize as a string because they might contain null bytes
        if (is_dispatch && opcode == effProcessEvents) {
            payload = *static_cast<VstEvents*>(data);
        } else {
            // TODO: More of these structs

            // Assume buffers are zeroed out, this is probably not the case
            char* c_string = static_cast<char*>(data);
            if (c_string[0] != 0) {
                payload = std::string(c_string);
            } else {
                payload = NeedsBuffer{};
            }
        }
    }

    if (logger != nullptr) {
        logger->log_event(is_dispatch, opcode, index, value, payload, option);
    }

    const Event event{opcode, index, value, option, payload};
    write_object(socket, event);

    const auto response = read_object<EventResult>(socket);
    if (logger != nullptr) {
        logger->log_event_response(is_dispatch, response.return_value,
                                   response.data);
    }
    if (response.data.has_value()) {
        char* output = static_cast<char*>(data);

        // For correctness we will copy the entire buffer and add a terminating
        // null byte ourselves. In practice `response.data` will only ever
        // contain C-style strings, but this would work with any other data
        // format that can contain null bytes.
        std::copy(response.data->begin(), response.data->end(), output);
        output[response.data->size()] = 0;
    }

    return response.return_value;
}
