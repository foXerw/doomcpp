#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "core/i_system.h"
#include "core/m_fixed.h"     // FixedMul
#include "core/m_tables.h"    // finecosine/finesine/R_InitTables
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
            if (std::string(argv[i]) == "--dumptex" && i + 3 < argc) {
                // Debug: dump a decoded wall texture to BMP (check alpha/transparency layout).
                WadFile wad(argv[i + 1]);
                TextureLookup tex(wad);
                const Texture* t = tex.wall(argv[i + 2]);
                if (!t) { I_Printf("no such texture"); return 1; }
                std::vector<std::uint32_t> px = t->rgba;
                for (auto& p : px) if (!(p & 0xFFu)) p = 0xFF00FFFFu;   // show holes as magenta
                writeBMP(argv[i + 3], px.data(), t->width, t->height);
                std::cout << "wrote " << argv[i + 3] << " (" << t->name << " "
                          << t->width << "x" << t->height << ")\n";
                return 0;
            }
            if (std::string(argv[i]) == "--gapscan" && i + 1 < argc) {
                // Debug diagnostic: render one world-only frame (no sprites) over a
                // magenta sentinel and count pixels no wall/plane covered. A non-zero
                // count is a real renderer coverage hole (black seams / HOM), not a
                // black texel -- legit black pixels never equal the sentinel.
                // Usage: --gapscan <wad> [angleDeg] [x y]   (same pose args as --dumpframe)
                WadFile wad(argv[i + 1]);
                MapData map = loadMap(wad, "E1M1");
                TextureLookup tex(wad);
                fixed_t px, py; angle_t ang;
                if (!playerStart(map, px, py, ang)) { I_Printf("no player start"); return 1; }
                if (i + 2 < argc) ang = degreesToBam(std::atoi(argv[i + 2]));
                if (i + 4 < argc) {
                    px = static_cast<fixed_t>(std::atof(argv[i + 3]) * (1 << FRACBITS));
                    py = static_cast<fixed_t>(std::atof(argv[i + 4]) * (1 << FRACBITS));
                }
                constexpr int FW = 320, FH = 200;
                constexpr uint32_t SENTINEL = 0xFF00FFFFu;   // magenta
                std::vector<std::uint32_t> fb(static_cast<size_t>(FW) * FH, 0);
                int ss0 = R_PointInSubsector(map, px, py);
                int sec0 = sectorOf(map, ss0);
                fixed_t eyeZ0 = (sec0 >= 0 ? (static_cast<fixed_t>(map.sectors[sec0].floorheight) << FRACBITS)
                                           : 0) + VIEWHEIGHT;
                auto renderGap = [&](bool forceOpaque) {
                    R_SetForceOpaqueForDiagnostics(forceOpaque);
                    R_RenderView(fb.data(), FW, FH, map, tex,
                                 static_cast<float>(px >> FRACBITS), static_cast<float>(py >> FRACBITS),
                                 bamToRendererRad(ang), static_cast<float>(eyeZ0 >> FRACBITS), 0,
                                 nullptr, nullptr, SENTINEL);
                    R_SetForceOpaqueForDiagnostics(false);
                    std::vector<int> rowGaps(FH, 0);
                    int total = 0, minCol = FW, maxCol = -1, minRow = FH, maxRow = -1;
                    for (int y = 0; y < FH; ++y)
                        for (int x = 0; x < FW; ++x)
                            if (fb[static_cast<size_t>(y) * FW + x] == SENTINEL) {
                                ++total; ++rowGaps[y];
                                minCol = std::min(minCol, x); maxCol = std::max(maxCol, x);
                                minRow = std::min(minRow, y); maxRow = std::max(maxRow, y);
                            }
                    return std::make_tuple(total, rowGaps, minCol, maxCol, minRow, maxRow);
                };
                // Pass 1: raw sentinel count (geometry holes + transparent/masked-texel holes).
                // Pass 2: transparent texels painted opaque -> counts only TRUE geometry holes.
                auto [geom, geomRows, gMinCol, gMaxCol, gMinRow, gMaxRow] = renderGap(true);
                auto [total, rowGaps, minCol, maxCol, minRow, maxRow] = renderGap(false);
                std::cout << "gapscan: raw=" << total << " geometry=" << geom;
                if (geom) std::cout << " geombbox=x[" << gMinCol << "," << gMaxCol
                                    << "] y[" << gMinRow << "," << gMaxRow << "]";
                std::cout << "\n";
                if (total) std::cout << "  bbox=x[" << minCol << "," << maxCol << "] y[" << minRow << "," << maxRow << "]\n";
                std::vector<int> rows(FH);
                for (int y = 0; y < FH; ++y) rows[y] = y;
                std::sort(rows.begin(), rows.end(), [&](int a, int b){ return rowGaps[a] > rowGaps[b]; });
                for (int k = 0; k < 10 && rowGaps[rows[k]] > 0; ++k)
                    std::cout << "  row " << rows[k] << ": " << rowGaps[rows[k]] << "\n";
                if (i + 5 < argc) writeBMP(argv[i + 5], fb.data(), FW, FH);   // force-opaque frame
                return 0;
            }
            if (std::string(argv[i]) == "--smoketest" && i + 1 < argc) {
                // Runtime smoke: exercises the REAL init/movement/toss/render path headlessly.
                // Unit tests self-init and --dumpframe renders a static frame, so neither can
                // catch a missing main-path init call (the P3.5a R_InitTables regression: dead
                // WASD while all tests and dumpframes were green). Exit code 1 on any failure.
                WadFile wad(argv[i + 1]);
                MapData map = loadMap(wad, "E1M1");
                TextureLookup tex(wad);
                SpriteCache sprites(wad);
                World w; w.map = &map; w.bm = &map.blockmap;
                P_SetupLevel(w, map);
                int failures = 0;
                auto check = [&](bool ok, const char* what) {
                    std::cout << (ok ? "PASS: " : "FAIL: ") << what << "\n";
                    if (!ok) ++failures;
                };
                check(w.player.mo != nullptr, "P_SetupLevel spawns the player");
                if (!w.player.mo) { std::cout << "SMOKETEST FAILED\n"; return 1; }

                // Toss first (from the open start pose), then let physics run: arc up, fall.
                mobj_t* b = P_TossBarrel(w);
                check(b != nullptr, "G-toss spawns a barrel");
                if (b) {
                    fixed_t z0 = b->z, maxz = z0;
                    bool airborne = false;
                    for (int t = 0; t < 45; ++t) {
                        P_RunThinkers(w);
                        maxz = std::max(maxz, b->z);
                        if (b->z > b->floorz) airborne = true;
                    }
                    check(maxz > z0, "tossed barrel arcs upward (upward momentum applied)");
                    check(airborne, "tossed barrel leaves the floor");
                    check(b->momz <= 0, "gravity reverses the barrel's vertical momentum");
                }

                // Movement path: R_InitTables must have been called on this path or this is dead.
                fixed_t x0 = w.player.mo->x, y0 = w.player.mo->y;
                for (int t = 0; t < 20; ++t) {
                    P_MovePlayer(map, map.blockmap, w.player, 5 << FRACBITS, 0, 0);
                    P_RunThinkers(w);
                }
                check(w.player.mo->x != x0 || w.player.mo->y != y0,
                      "forward movement changes the player position (WASD path live)");

                angle_t a0 = w.player.mo->angle;
                for (int t = 0; t < 10; ++t)
                    P_MovePlayer(map, map.blockmap, w.player, 0, 0, degreesToBam(4));
                check(w.player.mo->angle != a0, "turning changes the player angle");

                // Render path: a static frame must contain actual geometry, not just clear color.
                P_BuildThingsBySubsec(w);
                std::vector<std::uint32_t> fb(static_cast<size_t>(320) * 200, 0);
                R_RenderView(fb.data(), 320, 200, map, tex,
                             static_cast<float>(w.player.mo->x >> FRACBITS),
                             static_cast<float>(w.player.mo->y >> FRACBITS),
                             bamToRendererRad(w.player.mo->angle),
                             static_cast<float>(w.player.viewz >> FRACBITS),
                             w.tic, &sprites, &w.thingsBySubsec);
                int nonblack = 0;
                for (std::uint32_t p : fb) if (p != 0x000000FFu) ++nonblack;
                check(nonblack > 320 * 200 / 4, "renderer fills the frame with geometry");

                std::cout << (failures ? "SMOKETEST FAILED (" : "SMOKETEST PASSED (")
                          << failures << " failures)\n";
                return failures ? 1 : 0;
            }
            if (std::string(argv[i]) == "--tickdump" && i + 3 < argc) {
                // P5a physics visualization (headless): load E1M1, spawn a barrel
                // with forward+up momentum at a fixed (or CLI-overridden) spot,
                // run n thinker tics, print before/after z/momz/x, then write a
                // frame from the player-start camera. Interactive G-key toss is
                // verified by the human; this is the automated proxy.
                WadFile wad(argv[i + 1]);
                MapData map = loadMap(wad, "E1M1");
                TextureLookup tex(wad);
                SpriteCache sprites(wad);
                World w; w.map = &map; w.bm = &map.blockmap;
                P_SetupLevel(w, map);
                // Optional CLI overrides: [x y vx vy vz] at argv[i+4 .. i+8].
                // Each condition `i+k < argc` gates exactly argv[i+k] (the brief's
                // `i+k+1 < argc` forms were off-by-one too strict and read the vy
                // slot for momz; corrected here to match the documented signature).
                fixed_t bx = (i + 4 < argc) ? static_cast<fixed_t>(std::atof(argv[i + 4]) * (1 << FRACBITS))
                                            : 0 << FRACBITS;
                fixed_t by = (i + 5 < argc) ? static_cast<fixed_t>(std::atof(argv[i + 5]) * (1 << FRACBITS))
                                            : 304 << FRACBITS;
                mobj_t* b = P_SpawnMobj(w, bx, by, 0, MT_BARREL);
                int sec0 = sectorOf(map, b->subsector);
                b->floorz   = (sec0 >= 0) ? (static_cast<fixed_t>(map.sectors[sec0].floorheight) << FRACBITS) : 0;
                b->ceilingz = (sec0 >= 0) ? (static_cast<fixed_t>(map.sectors[sec0].ceilingheight) << FRACBITS)
                                          : (128 << FRACBITS);
                b->z = b->floorz;
                b->momx = (i + 6 < argc) ? static_cast<fixed_t>(std::atof(argv[i + 6]) * (1 << FRACBITS))
                                         : (10 << FRACBITS);
                b->momy = (i + 7 < argc) ? static_cast<fixed_t>(std::atof(argv[i + 7]) * (1 << FRACBITS))
                                         : 0;
                b->momz = (i + 8 < argc) ? static_cast<fixed_t>(std::atof(argv[i + 8]) * (1 << FRACBITS))
                                         : (9 << FRACBITS);
                int n = std::atoi(argv[i + 3]);
                std::cout << "before: z=" << (b->z >> FRACBITS)
                          << " momz=" << (b->momz >> FRACBITS)
                          << " x=" << (b->x >> FRACBITS)
                          << " momx=" << (b->momx >> FRACBITS) << "\n";
                for (int t = 0; t < n; ++t) P_RunThinkers(w);
                std::cout << "after(" << n << " tics): z=" << (b->z >> FRACBITS)
                          << " momz=" << (b->momz >> FRACBITS)
                          << " x=" << (b->x >> FRACBITS) << "\n";
                // dump a frame from the player-start camera
                fixed_t px, py; angle_t ang;
                playerStart(map, px, py, ang);
                P_BuildThingsBySubsec(w);
                constexpr int FW = 320, FH = 200;
                std::vector<std::uint32_t> fb(static_cast<size_t>(FW) * FH, 0);
                R_RenderView(fb.data(), FW, FH, map, tex,
                             static_cast<float>(px >> FRACBITS),
                             static_cast<float>(py >> FRACBITS),
                             bamToRendererRad(ang),
                             static_cast<float>(w.player.viewz >> FRACBITS),
                             w.tic, &sprites, &w.thingsBySubsec);
                writeBMP(argv[i + 2], fb.data(), FW, FH);
                std::cout << "wrote " << argv[i + 2] << "\n";
                return 0;
            }
        }

        // --- window / first-person 3D mode ---
        std::cout << "doomcpp 0.1.0  (P5a thinker/mobj)  WASD move, arrows turn, G toss barrel, ESC quit\n";
        WadFile wad("assets/freedoom1.wad");
        MapData map = loadMap(wad, "E1M1");
        TextureLookup tex(wad);
        SpriteCache sprites(wad);
        World w; w.map = &map; w.bm = &map.blockmap;
        P_SetupLevel(w, map);   // spawns w.player.mo (x/y/angle/floorz/subsector) + sets w.player.viewz

        constexpr int FB_W = 320, FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp - P5a")) return 1;
        std::vector<std::uint32_t> fb(static_cast<size_t>(FB_W) * FB_H, 0);

        const fixed_t moveSpeed = 5 << FRACBITS;        // per 35Hz tic
        const angle_t turnSpeed = degreesToBam(4);      // ~4 deg/tic (matches old ~0.07 rad)
        constexpr uint32_t TIC_MS = 1000 / 35;
        Input in{};
        bool running = true;
        uint32_t prev = SDL_GetTicks(), acc = 0, ticCount = 0;
        while (running) {
            running = i_video::pollEvents(in);
            // P5a toss milestone: G spawns an MT_BARREL in front of the player
            // with forward+up momentum (edge-triggered; handled at most once per
            // frame, OUTSIDE the tic batch so a held key never spawns 4×/frame).
            if (in.spawn) {
                in.spawn = false;
                P_TossBarrel(w);
            }
            uint32_t now = SDL_GetTicks();
            acc += (now - prev); prev = now;
            for (int n = 0; n < 4 && acc >= TIC_MS; ++n, acc -= TIC_MS) {
                int fwd = (in.forward ? 1 : 0) - (in.back ? 1 : 0);
                int str = (in.strafeRight ? 1 : 0) - (in.strafeLeft ? 1 : 0);
                int trn = (in.turnLeft ? 1 : 0) - (in.turnRight ? 1 : 0);
                P_MovePlayer(map, map.blockmap, w.player,
                             fwd * moveSpeed, str * moveSpeed, trn * turnSpeed);
                P_RunThinkers(w);
                ++ticCount;
            }
            P_BuildThingsBySubsec(w);
            R_RenderView(fb.data(), FB_W, FB_H, map, tex,
                         static_cast<float>(w.player.mo->x >> FRACBITS),
                         static_cast<float>(w.player.mo->y >> FRACBITS),
                         bamToRendererRad(w.player.mo->angle),
                         static_cast<float>(w.player.viewz >> FRACBITS),
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
