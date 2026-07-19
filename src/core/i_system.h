#pragma once
#include <string_view>

// Engine-wide logging and fatal-error handling.
// Mirrors the original engine's i_system.c I_Printf / I_Error contract.

// Normal logging (stderr).
void I_Printf(std::string_view msg);

// Fatal error: logs the message then throws std::runtime_error to unwind
// back to main(), where it is caught and the program exits non-zero.
[[noreturn]] void I_Error(std::string_view msg);
