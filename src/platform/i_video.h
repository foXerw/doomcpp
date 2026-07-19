#pragma once
#include <cstdint>
#include <string>

namespace i_video {

// Initialize an SDL2 window + renderer + streaming texture of the given
// logical (framebuffer) resolution. Returns false (and logs) on failure.
bool init(int width, int height, const std::string& title);

// Copy a width*height RGBA8888 pixel buffer to the screen, scaled to the window.
void present(const std::uint32_t* pixels);

// Drain the event queue for one frame. Returns false on window-close or ESC.
bool pollEvents();

// Tear down SDL video resources.
void shutdown();

} // namespace i_video
