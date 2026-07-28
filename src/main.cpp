#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "core/i_system.h"
#include "core/m_tables.h"
#include "platform/i_video.h"
#include "play/info.h"
#include "play/p_setup.h"
#include "play/p_tick.h"
#include "play/p_maputl.h"
#include "play/p_map.h"
#include "render/r_bsp.h"
#include "render/r_data.h"
#include "render/r_draw.h"
#include "render/r_things.h"
#include "wad/wadfile.h"

// P3.5a BRIDGE (delete in P3.5b): the renderer still takes the P2a float angle
// whose forward basis is (sin,cos). BAM forward basis is (cos,sin), so the
// equivalent renderer angle is PI/2 - aRad. Algebraically identical to the old
// playerStart formula (90-deg)*PI/180 -- no facing change.
static float bamToRendererRad(angle_t a) {
    constexpr double PI = 3.141592653589793;
    double aRad = static_cast<double>(a) * (2.0 * PI / 4294967296.0);
    return static_cast<float>(PI / 2.0 - aRad);
}

int main(int argc, char** argv) {
    (void)argv;
    SDL_SetMainReady();

    try {
        // Init vanilla trig tables (finesine/finecosine) before any movement/rendering that indexes them.
        R_InitTables();
        // P1 diagnostic: dump a WAD's lump directory and exit.
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--mapinfo" && i + 1 < argc) {
                WadFile wad(argv[i + 1]);
                MapData map = loadMap(wad, "E1M1");
                fixed_t px, py; angle_t ang;
                if (playerStart(map, px, py, ang))
                    std::cout << "start: x=" << (px >> FRACBITS) << " y=" << (py >> FRACBITS)
                              << " ang=" << ang << "\n";
                int ss0 = R_PointInSubsector(map, px, py);
                int sec0 = sectorOf(map, ss0);
                if (sec0 >= 0)
                    std::cout << "start sector " << sec0
                              << " floor=" << map.sectors[sec0].floorheight
                              << " ceil=" << map.sectors[sec0].ceilingheight << "\n";
                // Lowest-floor sectors (pit candidates) with a vertex centroid to teleport near.
                std::vector<int> order(map.sectors.size());
                for (size_t s = 0; s < map.sectors.size(); ++s) order[s] = (int)s;
                std::sort(order.begin(), order.end(), [&](int a, int b){
                    return map.sectors[a].floorheight < map.sectors[b].floorheight; });
                std::cout << "--- 12 lowest-floor sectors ---\n";
                for (int k = 0; k < (int)std::min<size_t>(12, order.size()); ++k) {
                    int s = order[k];
                    double cx = 0, cy = 0; int n = 0;
                    for (const auto& sg : map.segs)
                        if (sg.frontsector == s || sg.backsector == s) {
                            if (sg.v1 >= 0 && sg.v1 < (int)map.vertices.size()) {
                                cx += (map.vertices[sg.v1].x >> 16); cy += (map.vertices[sg.v1].y >> 16); ++n; }
                            if (sg.v2 >= 0 && sg.v2 < (int)map.vertices.size()) {
                                cx += (map.vertices[sg.v2].x >> 16); cy += (map.vertices[sg.v2].y >> 16); ++n; }
                        }
                    if (n) { cx /= n; cy /= n; }
                    std::cout << "sec " << s << " floor=" << map.sectors[s].floorheight
                              << " ceil=" << map.sectors[s].ceilingheight
                              << " centroid=(" << (int)cx << "," << (int)cy << ")\n";
                }
                return 0;
            }
            if (std::string(argv[i]) == "--sample" && i + 2 < argc) {
                WadFile wad(argv[i + 1]);
                MapData map = loadMap(wad, "E1M1");
                fixed_t qx = static_cast<fixed_t>(std::atof(argv[i + 2]) * (1 << FRACBITS));
                fixed_t qy = (i + 3 < argc) ? static_cast<fixed_t>(std::atof(argv[i + 3]) * (1 << FRACBITS))
                                            : 0;
                int ss = R_PointInSubsector(map, qx, qy);
                int sec = sectorOf(map, ss);
                std::cout << "at (" << (qx >> FRACBITS) << "," << (qy >> FRACBITS)
                          << ") subsector=" << ss << " sector=" << sec;
                if (sec >= 0)
                    std::cout << " floor=" << map.sectors[sec].floorheight
                              << " ceil=" << map.sectors[sec].ceilingheight
                              << " floorpic='" << map.sectors[sec].floorpic << "'"
                              << " ceilpic='" << map.sectors[sec].ceilingpic << "'";
                std::cout << "\n";
                return 0;
            }
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
                SpriteCache sprites(wad);
                World w; w.map = &map; w.bm = &map.blockmap;
                P_SetupLevel(w, map);
                P_BuildThingsBySubsec(w);
                fixed_t px, py; angle_t ang;
                if (!playerStart(map, px, py, ang)) { I_Printf("no player start"); return 1; }
                if (i + 3 < argc) {   // optional DOOM-angle override (degrees)
                    int a = std::atoi(argv[i + 3]);
                    ang = degreesToBam(a);
                }
                if (i + 5 < argc) {   // optional [px py] teleport (debug)
                    px = static_cast<fixed_t>(std::atof(argv[i + 4]) * (1 << FRACBITS));
                    py = static_cast<fixed_t>(std::atof(argv[i + 5]) * (1 << FRACBITS));
                }
                constexpr int FW = 320, FH = 200;
                std::vector<std::uint32_t> fb(static_cast<size_t>(FW) * FH, 0);
                int ss0 = R_PointInSubsector(map, px, py);
                int sec0 = sectorOf(map, ss0);
                fixed_t eyeZ0 = (sec0 >= 0 ? (static_cast<fixed_t>(map.sectors[sec0].floorheight) << FRACBITS)
                                           : 0) + VIEWHEIGHT;
                R_RenderView(fb.data(), FW, FH, map, tex,
                             static_cast<float>(px >> FRACBITS), static_cast<float>(py >> FRACBITS),
                             bamToRendererRad(ang), static_cast<float>(eyeZ0 >> FRACBITS), 0,
                             &sprites, &w.thingsBySubsec);
                writeBMP(argv[i + 2], fb.data(), FW, FH);
                // Rendered mobs exclude the player (camera): P_SetupLevel spawns it into w.mobjs
                // as w.player.mo, but P_BuildThingsBySubsec skips it from the render bins. The
                // brief's literal w.mobjs.size() would report 227 (incl. player); subtracting the
                // player preserves the P4 N=226 regression sentinel.
                std::cout << "wrote " << argv[i + 2] << " (mobs="
                          << (w.mobjs.size() - (w.player.mo ? 1u : 0u)) << ")\n";
                return 0;
            }
        }

        // --- window / first-person 3D mode ---
        std::cout << "doomcpp 0.1.0  (P3.5a fixed-point collision)  WASD move, arrows turn, ESC quit\n";
        WadFile wad("assets/freedoom1.wad");
        MapData map = loadMap(wad, "E1M1");
        TextureLookup tex(wad);
        SpriteCache sprites(wad);
        World w; w.map = &map; w.bm = &map.blockmap;
        P_SetupLevel(w, map);
        Player p;
        {   fixed_t sx, sy; angle_t sang;
            if (!playerStart(map, sx, sy, sang)) { sx = 0; sy = 0; sang = 0; }
            p.x = sx; p.y = sy; p.angle = sang;
            int ss = R_PointInSubsector(map, p.x, p.y); p.subsector = ss; p.sector = sectorOf(map, ss);
            p.floorz = (p.sector >= 0) ? (static_cast<fixed_t>(map.sectors[p.sector].floorheight) << FRACBITS) : 0;
            p.viewz = p.floorz + VIEWHEIGHT; }

        constexpr int FB_W = 320, FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp - P3.5a")) return 1;
        std::vector<std::uint32_t> fb(static_cast<size_t>(FB_W) * FB_H, 0);

        const fixed_t moveSpeed = 5 << FRACBITS;        // per 35Hz tic
        const angle_t turnSpeed = degreesToBam(4);      // ~4 deg/tic (matches old ~0.07 rad)
        constexpr uint32_t TIC_MS = 1000 / 35;
        Input in{};
        bool running = true;
        uint32_t prev = SDL_GetTicks(), acc = 0, ticCount = 0;
        while (running) {
            running = i_video::pollEvents(in);
            uint32_t now = SDL_GetTicks();
            acc += (now - prev); prev = now;
            for (int n = 0; n < 4 && acc >= TIC_MS; ++n, acc -= TIC_MS) {
                int fwd = (in.forward ? 1 : 0) - (in.back ? 1 : 0);
                int str = (in.strafeRight ? 1 : 0) - (in.strafeLeft ? 1 : 0);
                int trn = (in.turnLeft ? 1 : 0) - (in.turnRight ? 1 : 0);
                P_MovePlayer(map, map.blockmap, p,
                             fwd * moveSpeed, str * moveSpeed, trn * turnSpeed);
                ++ticCount;
            }
            P_BuildThingsBySubsec(w);
            R_RenderView(fb.data(), FB_W, FB_H, map, tex,
                         static_cast<float>(p.x >> FRACBITS), static_cast<float>(p.y >> FRACBITS),
                         bamToRendererRad(p.angle), static_cast<float>(p.viewz >> FRACBITS),
                         ticCount, &sprites, &w.thingsBySubsec);
            i_video::present(fb.data());
        }

        i_video::shutdown();
        return 0;
    } catch (const std::exception& e) {
        I_Printf(std::string("Fatal: ") + e.what());
        return 1;
    }
}
