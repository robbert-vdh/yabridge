#include <vestige/aeffect.h>

#include <iostream>

#include "native-includes.h"

// `native-includes.h` has to be included before any other files as otherwise we
// might get the wrong version of certain libraries
#include "../common/communication.h"

int main() {
    // TODO: The program should terminate automatically when stdin gets closed

    // TODO: Remove debug
    while (true) {
        auto event = read_object<Event>(std::cin);

        EventResult response;
        if (event.opcode == effGetEffectName) {
            response.return_value = 1;
            response.result = "Hello, world!";
        } else {
            response.return_value = 0;
        }

        write_object(std::cout, response);
    }
}
