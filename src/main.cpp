#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "core/i_system.h"
#include "platform/i_video.h"
#include "wad/wadfile.h"

int main(int argc, char** argv) {
    (void)argv;
    SDL_SetMainReady();

    try {
        // P1 diagnostic: dump a WAD's lump directory and exit.
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--listlumps" && i + 1 < argc) {
                WadFile wad(argv[i + 1]);
                std::cout << wad.numLumps() << " lumps in " << argv[i + 1]
                          << " (" << wad.magic() << ")\n";
                for (int l = 0; l < wad.numLumps(); ++l) {
                    std::cout << "  " << l << ": " << wad.lumpName(l)
                              << " (" << wad.lumpSize(l) << " bytes)\n";
                }
                return 0;
            }
        }

        std::cout << "doomcpp 0.1.0\n";

        constexpr int FB_W = 320;
        constexpr int FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp")) {
            return 1;
        }

        std::vector<std::uint32_t> framebuffer(
            static_cast<size_t>(FB_W) * FB_H, 0xFF000000u);

        bool running = true;
        while (running) {
            running = i_video::pollEvents(); // false on ESC / window-close

            // P0 placeholder: solid opaque fill to prove the framebuffer
            // pipeline works. (RGBA8888: 0xRRGGBBAA; channel use finalized in P2.)
            std::fill(framebuffer.begin(), framebuffer.end(), 0xA02020FFu);

            i_video::present(framebuffer.data());
        }

        i_video::shutdown();
        return 0;
    } catch (const std::exception& e) {
        I_Printf(std::string("Fatal: ") + e.what());
        return 1;
    }
}
