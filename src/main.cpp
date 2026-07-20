#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "core/i_system.h"
#include "platform/i_video.h"
#include "play/p_setup.h"
#include "render/r_bsp.h"
#include "render/r_data.h"
#include "render/r_draw.h"
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
            if (std::string(argv[i]) == "--dumpframe" && i + 2 < argc) {
                WadFile wad(argv[i + 1]);
                MapData map = loadMap(wad, "E1M1");
                TextureLookup tex(wad);
                float px, py, ang;
                if (!playerStart(map, px, py, ang)) { I_Printf("no player start"); return 1; }
                if (i + 3 < argc) {  // optional DOOM-angle override (degrees)
                    int a = std::atoi(argv[i + 3]);
                    ang = (90.0f - static_cast<float>(a)) * 3.14159265f / 180.0f;
                }
                constexpr int FW = 320, FH = 200;
                std::vector<std::uint32_t> fb(static_cast<size_t>(FW) * FH, 0);
                R_RenderView(fb.data(), FW, FH, map, tex, px, py, ang, 41.0f);
                writeBMP(argv[i + 2], fb.data(), FW, FH);
                std::cout << "wrote " << argv[i + 2] << "\n";
                return 0;
            }
        }

        // --- window / first-person 3D mode ---
        std::cout << "doomcpp 0.1.0  (P3a textured walls)  WASD move, arrows turn, ESC quit\n";
        WadFile wad("assets/freedoom1.wad");
        MapData map = loadMap(wad, "E1M1");
        TextureLookup tex(wad);
        float px, py, ang;
        if (!playerStart(map, px, py, ang)) { px = 0; py = 0; ang = 0; }

        constexpr int FB_W = 320, FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp - textured")) return 1;
        std::vector<std::uint32_t> fb(static_cast<size_t>(FB_W) * FB_H, 0);

        const float moveSpeed = 4.0f, turnSpeed = 0.04f;
        Input in{};
        bool running = true;
        while (running) {
            running = i_video::pollEvents(in);
            if (in.forward)     { px += std::sin(ang) * moveSpeed; py += std::cos(ang) * moveSpeed; }
            if (in.back)        { px -= std::sin(ang) * moveSpeed; py -= std::cos(ang) * moveSpeed; }
            if (in.strafeLeft)  { px += std::cos(ang) * moveSpeed; py -= std::sin(ang) * moveSpeed; }
            if (in.strafeRight) { px -= std::cos(ang) * moveSpeed; py += std::sin(ang) * moveSpeed; }
            if (in.turnLeft)    ang += turnSpeed;
            if (in.turnRight)   ang -= turnSpeed;

            R_RenderView(fb.data(), FB_W, FB_H, map, tex, px, py, ang, 41.0f);
            i_video::present(fb.data());
        }

        i_video::shutdown();
        return 0;
    } catch (const std::exception& e) {
        I_Printf(std::string("Fatal: ") + e.what());
        return 1;
    }
}
