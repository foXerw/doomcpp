#include "i_system.h"
#include <iostream>
#include <stdexcept>

void I_Printf(std::string_view msg) {
    std::clog << msg << '\n';
}

[[noreturn]] void I_Error(std::string_view msg) {
    std::cerr << "I_Error: " << msg << '\n';
    throw std::runtime_error(std::string(msg));
}
