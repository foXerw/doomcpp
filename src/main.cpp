#include <iostream>
#include <string>
#include "core/i_system.h"

int main() {
    try {
        std::cout << "doomcpp 0.1.0\n";
        return 0;
    } catch (const std::exception& e) {
        I_Printf(std::string("Fatal: ") + e.what());
        return 1;
    }
}
