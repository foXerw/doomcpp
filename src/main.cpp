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
                float px, py, ang;
                if (!playerStart(map, px, py, ang)) { I_Printf("no player start"); return 1; }
                if (i + 3 < argc) {  // optional DOOM-angle override (degrees)
                    int a = std::atoi(argv[i + 3]);
                    ang = (90.0f - static_cast<float>(a)) * 3.14159265f / 180.0f;
                }
                constexpr int FW = 320, FH = 200;
                std::vector<std::uint32_t> fb(static_cast<size_t>(FW) * FH, 0);
                R_RenderView(fb.data(), FW, FH, map, px, py, ang, 41.0f);
                writeBMP(argv[i + 2], fb.data(), FW, FH);
                std::cout << "wrote " << argv[i + 2] << "\n";
                return 0;
            }
        }

        // --- window / automap mode ---
        std::cout << "doomcpp 0.1.0  (P2a automap)\n";

        WadFile wad("assets/freedoom1.wad");
        MapData map = loadMap(wad, "E1M1");
        std::cout << "E1M1: " << map.vertices.size() << " verts, "
                  << map.lines.size() << " lines\n";
        if (map.vertices.empty()) { I_Printf("E1M1 has no vertices"); return 1; }

        constexpr int FB_W = 320, FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp - automap")) return 1;
        std::vector<std::uint32_t> fb(static_cast<size_t>(FB_W) * FB_H, 0xFF000000u);

        // Level bounds; start centered on the centroid, auto-fit scale.
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (const auto& v : map.vertices) {
            float x = static_cast<float>(v.x >> 16), y = static_cast<float>(v.y >> 16);
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
        float px = (minX + maxX) / 2.0f, py = (minY + maxY) / 2.0f, ang = 0.0f;
        float scale = std::min(FB_W / (maxX - minX), FB_H / (maxY - minY)) * 0.9f;

        const float moveSpeed = 8.0f;
        const float turnSpeed = 0.05f;

        Input in{};
        bool running = true;
        while (running) {
            running = i_video::pollEvents(in);

            if (in.forward)     { px += std::sin(ang) * moveSpeed; py += std::cos(ang) * moveSpeed; }
            if (in.back)        { px -= std::sin(ang) * moveSpeed; py -= std::cos(ang) * moveSpeed; }
            if (in.turnLeft)    ang += turnSpeed;
            if (in.turnRight)   ang -= turnSpeed;
            if (in.strafeLeft)  { px += std::cos(ang) * moveSpeed; py -= std::sin(ang) * moveSpeed; }
            if (in.strafeRight) { px -= std::cos(ang) * moveSpeed; py += std::sin(ang) * moveSpeed; }

            std::fill(fb.begin(), fb.end(), 0xFF000000u);
            const float ca = std::cos(ang), sa = std::sin(ang);
            auto project = [&](fixed_t vx, fixed_t vy) -> std::pair<int,int> {
                float dx = static_cast<float>(vx >> 16) - px;
                float dy = static_cast<float>(vy >> 16) - py;
                float sx = (dx * ca - dy * sa) * scale;
                float sy = (dx * sa + dy * ca) * scale;
                return { FB_W / 2 + static_cast<int>(sx),
                         FB_H / 2 - static_cast<int>(sy) };
            };
            for (const auto& L : map.lines) {
                if (L.v1 < 0 || L.v1 >= static_cast<int>(map.vertices.size())) continue;
                if (L.v2 < 0 || L.v2 >= static_cast<int>(map.vertices.size())) continue;
                auto a = project(map.vertices[L.v1].x, map.vertices[L.v1].y);
                auto b = project(map.vertices[L.v2].x, map.vertices[L.v2].y);
                R_DrawLine(fb.data(), FB_W, FB_H, a.first, a.second, b.first, b.second, 0xFFFFFFFFu);
            }
            R_DrawPixel(fb.data(), FB_W, FB_H, FB_W / 2, FB_H / 2, 0xFF0000FFu); // player (red)

            i_video::present(fb.data());
        }

        i_video::shutdown();
        return 0;
    } catch (const std::exception& e) {
        I_Printf(std::string("Fatal: ") + e.what());
        return 1;
    }
}
