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
#include "play/p_maputl.h"
#include "play/p_map.h"
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
                int ss0 = R_PointInSubsector(map, px, py);
                int sec0 = sectorOf(map, ss0);
                float eyeZ0 = (sec0 >= 0 ? static_cast<float>(map.sectors[sec0].floorheight) : 0.0f) + VIEWHEIGHT;
                R_RenderView(fb.data(), FW, FH, map, tex, px, py, ang, eyeZ0, 0);
                writeBMP(argv[i + 2], fb.data(), FW, FH);
                std::cout << "wrote " << argv[i + 2] << "\n";
                return 0;
            }
        }

        // --- window / first-person 3D mode ---
        std::cout << "doomcpp 0.1.0  (P3c collision)  WASD move, arrows turn, ESC quit\n";
        WadFile wad("assets/freedoom1.wad");
        MapData map = loadMap(wad, "E1M1");
        TextureLookup tex(wad);
        float sx, sy, sang;
        if (!playerStart(map, sx, sy, sang)) { sx = 0; sy = 0; sang = 0; }

        Player p;
        p.x = sx; p.y = sy; p.angle = sang;
        {   int ss = R_PointInSubsector(map, p.x, p.y); p.subsector = ss; p.sector = sectorOf(map, ss);
            p.floorz = (p.sector >= 0) ? static_cast<float>(map.sectors[p.sector].floorheight) : 0.0f;
            p.viewz  = p.floorz + VIEWHEIGHT; }

        constexpr int FB_W = 320, FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp - collision")) return 1;
        std::vector<std::uint32_t> fb(static_cast<size_t>(FB_W) * FB_H, 0);

        const float moveSpeed = 5.0f, turnSpeed = 0.07f;   // per 35Hz tic; tunable
        constexpr uint32_t TIC_MS = 1000 / 35;             // 28 ms
        Input in{};
        bool running = true;
        uint32_t prev = SDL_GetTicks();
        uint32_t acc = 0;
        uint32_t ticCount = 0;
        while (running) {
            running = i_video::pollEvents(in);
            uint32_t now = SDL_GetTicks();
            acc += (now - prev);
            prev = now;
            for (int n = 0; n < 4 && acc >= TIC_MS; ++n, acc -= TIC_MS) {   // cap 4 tics/frame
                float fwd = (in.forward ? 1.0f : 0.0f) - (in.back ? 1.0f : 0.0f);
                float str = (in.strafeRight ? 1.0f : 0.0f) - (in.strafeLeft ? 1.0f : 0.0f);
                float trn = (in.turnLeft ? 1.0f : 0.0f) - (in.turnRight ? 1.0f : 0.0f);
                P_MovePlayer(map, map.blockmap, p, fwd * moveSpeed, str * moveSpeed, trn * turnSpeed);
                ++ticCount;
            }
            R_RenderView(fb.data(), FB_W, FB_H, map, tex, p.x, p.y, p.angle, p.viewz, ticCount);
            i_video::present(fb.data());
        }

        i_video::shutdown();
        return 0;
    } catch (const std::exception& e) {
        I_Printf(std::string("Fatal: ") + e.what());
        return 1;
    }
}
