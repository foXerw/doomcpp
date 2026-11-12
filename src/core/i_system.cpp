#include "i_system.h"
#include <iostream>
#include <stdexcept>

namespace {
SoundHook g_soundHook = nullptr;
}

void I_SetSoundHook(SoundHook hook) { g_soundHook = hook; }

void I_StartSound(int id, int volume) {
    if (g_soundHook) g_soundHook(id, volume);
}

void I_Printf(std::string_view msg) {
    std::clog << msg << '\n';
}

[[noreturn]] void I_Error(std::string_view msg) {
    std::cerr << "I_Error: " << msg << '\n';
    throw std::runtime_error(std::string(msg));
}
