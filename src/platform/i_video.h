#pragma once
#include <cstdint>
#include <string>

// Movement/turn input state, SDL-free for the header.
struct Input {
    bool forward=false, back=false;
    bool turnLeft=false, turnRight=false;
    bool strafeLeft=false, strafeRight=false;
    bool spawn=false;       // P5a: edge-triggered (set on G KEYDOWN w/ repeat==0)
    bool fire=false;        // P5b: held Ctrl fires the ready weapon
    bool use=false;         // P6a: edge-triggered Space opens doors/uses switches
    int  weaponSwitch=-1;   // P5b: edge-triggered weapon slot (1..8 -> weapontype or -1)
};

namespace i_video {

// Initialize an SDL2 window + renderer + streaming texture of the given
// logical (framebuffer) resolution. Returns false (and logs) on failure.
bool init(int width, int height, const std::string& title);

// Copy a width*height RGBA8888 pixel buffer to the screen, scaled to the window.
void present(const std::uint32_t* pixels);

// Drain the event queue for one frame. Returns false on window-close or ESC.
bool pollEvents();

// Same, plus fills movement/turn state from held keys (WASD + arrows).
bool pollEvents(Input& input);

// Tear down SDL video resources.
void shutdown();

} // namespace i_video
