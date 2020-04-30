#include <cassert>

#include "defines.h"

std::string to_string(PromiseTypes v) {
    switch (v) {
        case PromiseTypes::NATIVE:
            return "native";

        case PromiseTypes::ACTIVE:
            return "active";

        default:
            assertM(false, "Unknown PromiseTypes %d", (unsigned char)v);
            return "";
    }
}