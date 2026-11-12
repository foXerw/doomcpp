#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "core/i_system.h"
#include "core/m_fixed.h"     // FixedMul
#include "core/m_tables.h"    // finecosine/finesine/R_InitTables
#include "platform/i_video.h"
#include "platform/i_sound.h"
#include "play/info.h"
#include "play/p_setup.h"
#include "play/p_tick.h"
#include "play/p_pspr.h"
#include "play/p_spec.h"
#include "play/p_enemy.h"
#include "play/sfx.h"
#include "play/p_maputl.h"
#include "play/p_map.h"
#include "render/r_bsp.h"
#include "render/r_data.h"
#include "render/r_draw.h"
#include "render/r_things.h"
#include "ui/st_stuff.h"
#include "ui/f_font.h"
#include "ui/f_intermission.h"
#include "play/g_game.h"
#include "play/g_save.h"
#include "play/midi.h"
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

// Overlay the player's first-person weapon/flash sprites after the 3D view.
static void drawPlayerSprites(const World& w, const SpriteCache& sprites,
                              std::uint32_t* fb, int fw, int fh) {
    for (int i = 0; i < 2; ++i) {
        const pspdef_t& ps = w.player.psprites[i];
        if (!ps.state || ps.state->sprite == SPR_NONE) continue;
        char frame = static_cast<char>('A' + (ps.state->frame & FF_FRAMEMASK));
        R_DrawPSprite(fb, fw, fh, sprnames[ps.state->sprite], frame, ps.sx, ps.sy, sprites);
    }
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
                constexpr int VIEW_H = FH - 32;   // vanilla 320x168 view + 32-row status bar
                R_RenderView(fb.data(), FW, VIEW_H, map, tex,
                             static_cast<float>(px >> FRACBITS), static_cast<float>(py >> FRACBITS),
                             bamToRendererRad(ang), static_cast<float>(eyeZ0 >> FRACBITS), 0,
                             &sprites, &w.thingsBySubsec);
                drawPlayerSprites(w, sprites, fb.data(), FW, VIEW_H);
                StatusBar stbar(wad);
                stbar.draw(fb.data(), FW, FH, w.player);
                writeBMP(argv[i + 2], fb.data(), FW, FH);
                // Rendered mobs exclude the player (camera): P_SetupLevel spawns it into w.mobjs
                // as w.player.mo, but P_BuildThingsBySubsec skips it from the render bins. The
                // brief's literal w.mobjs.size() would report 227 (incl. player); subtracting the
                // player preserves the P4 N=226 regression sentinel.
                std::cout << "wrote " << argv[i + 2] << " (mobs="
                          << (w.mobjs.size() - (w.player.mo ? 1u : 0u)) << ")\n";
                return 0;
            }
            if (std::string(argv[i]) == "--dumpframe-map" && i + 3 < argc) {
                // Like --dumpframe but for an arbitrary map: <wad> <map> <bmp> [angle] [x y]
                WadFile wad(argv[i + 1]);
                MapData map = loadMap(wad, argv[i + 2]);
                TextureLookup tex(wad);
                SpriteCache sprites(wad);
                World w; w.map = &map; w.bm = &map.blockmap; w.mapName = argv[i + 2];
                P_SetupLevel(w, map);
                P_BuildThingsBySubsec(w);
                fixed_t px, py; angle_t ang;
                if (!playerStart(map, px, py, ang)) { I_Printf("no player start"); return 1; }
                if (i + 4 < argc) ang = degreesToBam(std::atoi(argv[i + 4]));
                if (i + 6 < argc) {
                    px = static_cast<fixed_t>(std::atof(argv[i + 5]) * (1 << FRACBITS));
                    py = static_cast<fixed_t>(std::atof(argv[i + 6]) * (1 << FRACBITS));
                }
                constexpr int FW = 320, FH = 200;
                std::vector<std::uint32_t> fb(static_cast<size_t>(FW) * FH, 0);
                int ss0 = R_PointInSubsector(map, px, py);
                int sec0 = sectorOf(map, ss0);
                fixed_t eyeZ0 = (sec0 >= 0 ? (static_cast<fixed_t>(map.sectors[sec0].floorheight) << FRACBITS)
                                           : 0) + VIEWHEIGHT;
                constexpr int VIEW_H = FH - 32;
                R_RenderView(fb.data(), FW, VIEW_H, map, tex,
                             static_cast<float>(px >> FRACBITS), static_cast<float>(py >> FRACBITS),
                             bamToRendererRad(ang), static_cast<float>(eyeZ0 >> FRACBITS), 0,
                             &sprites, &w.thingsBySubsec);
                StatusBar stbar(wad);
                stbar.draw(fb.data(), FW, FH, w.player);
                writeBMP(argv[i + 3], fb.data(), FW, FH);
                std::cout << "wrote " << argv[i + 3] << " (" << argv[i + 2] << ")\n";
                return 0;
            }
            if (std::string(argv[i]) == "--dumpintermission" && i + 2 < argc) {
                // Render the real STCFN-font intermission screen to a BMP for visual checks.
                WadFile wad(argv[i + 1]);
                Font font(wad);
                Intermission inter(wad);
                constexpr int FW = 320, FH = 200;
                std::vector<std::uint32_t> fb(static_cast<size_t>(FW) * FH, 0);
                inter.draw(fb.data(), FW, FH, "E1M1", 75, 50, 0, "E1M2", false);
                writeBMP(argv[i + 2], fb.data(), FW, FH);
                std::cout << "wrote " << argv[i + 2] << " (font valid=" << font.valid() << ")\n";
                return 0;
            }
            if (std::string(argv[i]) == "--dumpmobs" && i + 3 < argc) {
                // Render one frame per monster type spawned in front of the E1M1 start.
                // Usage: --dumpmobs <wad> <bmpPrefix> [distance]
                WadFile wad(argv[i + 1]);
                MapData map = loadMap(wad, "E1M1");
                TextureLookup tex(wad);
                SpriteCache sprites(wad);
                static const mobjtype_t types[] = {
                    MT_POSSESSED, MT_SHOTGUY, MT_TROOP, MT_SERGEANT, MT_SHADOWS,
                    MT_SKULL, MT_HEAD, MT_BRUISER, MT_CYBORG, MT_SPIDER,
                };
                static const char* names[] = {
                    "POSS", "SPOS", "TROO", "SARG", "SHAD",
                    "SKUL", "HEAD", "BOSS", "CYBR", "SPID",
                };
                fixed_t px, py; angle_t ang;
                if (!playerStart(map, px, py, ang)) return 1;
                const fixed_t dist = (i + 3 < argc)
                    ? static_cast<fixed_t>(std::atof(argv[i + 3]) * FRACUNIT)
                    : (160 << FRACBITS);
                const int fi = static_cast<int>((ang >> ANGLETOFINESHIFT) & FINEMASK);
                const fixed_t sx = px + FixedMul(finecosine[fi], dist);
                const fixed_t sy = py + FixedMul(finesine[fi], dist);
                int ss0 = R_PointInSubsector(map, px, py);
                int sec0 = sectorOf(map, ss0);
                const fixed_t eyeZ = (sec0 >= 0
                    ? (static_cast<fixed_t>(map.sectors[sec0].floorheight) << FRACBITS) : 0) + VIEWHEIGHT;
                for (size_t k = 0; k < sizeof(types) / sizeof(types[0]); ++k) {
                    World w; w.map = &map; w.bm = &map.blockmap;
                    P_SetupLevel(w, map);
                    mobj_t* mo = P_SpawnMobj(w, sx, sy, 0, types[k]);
                    mo->angle = ang + ANG180;
                    P_BuildThingsBySubsec(w);
                    std::vector<std::uint32_t> fb(320u * 200u, 0);
                    R_RenderView(fb.data(), 320, 200 - 32, map, tex,
                                 static_cast<float>(px >> FRACBITS), static_cast<float>(py >> FRACBITS),
                                 bamToRendererRad(ang), static_cast<float>(eyeZ >> FRACBITS), 0,
                                 &sprites, &w.thingsBySubsec);
                    StatusBar stbar(wad);
                    stbar.draw(fb.data(), 320, 200, w.player);
                    std::string out = std::string(argv[i + 2]) + names[k] + ".bmp";
                    writeBMP(out, fb.data(), 320, 200);
                    std::cout << "wrote " << out << "\n";
                }
                return 0;
            }
            if (std::string(argv[i]) == "--dumppatch" && i + 3 < argc) {
                // Debug: dump an arbitrary patch lump (HUD/status-bar graphics) to BMP.
                WadFile wad(argv[i + 1]);
                if (wad.checkNumForName(argv[i + 2]) < 0) { I_Printf("no such lump"); return 1; }
                auto raw = wad.readLumpByName(argv[i + 2]);
                auto pal = loadPalette(wad);
                Patch p = decodePatch(raw.data(), raw.size(), pal.data());
                std::vector<std::uint32_t> px = p.rgba;
                for (auto& c : px) if (!(c & 0xFFu)) c = 0xFF00FFFFu;
                writeBMP(argv[i + 3], px.data(), p.width, p.height);
                std::cout << "wrote " << argv[i + 3] << " (" << argv[i + 2] << " "
                          << p.width << "x" << p.height << ")\n";
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
                // P6b: every new monster/projectile sprite decodes from the IWAD.
                {
                    static const char* newSprites[] = {
                        "BOSS", "HEAD", "SKUL", "CYBR", "SPID", "BAL1", "BAL2", "BAL7"
                    };
                    bool allSprites = true;
                    for (const char* nm : newSprites)
                        if (!sprites.resolve(nm, 'A', 0).patch) allSprites = false;
                    check(allSprites, "new monster/projectile sprites decode from the IWAD");
                }
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

                // Weapon path: put a barrel in front and hold fire; the pistol must damage it.
                {
                    mobj_t* mo = w.player.mo;
                    int fi = (mo->angle >> ANGLETOFINESHIFT) & FINEMASK;
                    fixed_t dist = 96 << FRACBITS;
                    mobj_t* target = P_SpawnMobj(
                        w, mo->x + FixedMul(finecosine[fi], dist),
                        mo->y + FixedMul(finesine[fi], dist), mo->z, MT_BARREL);
                    int hp0 = target->health;
                    w.player.ammo[AM_CLIP] = 50;
                    w.player.readyweapon = WP_PISTOL;
                    P_SetPsprite(w, w.player, 0, S_PISTOL);
                    for (int t = 0; t < 40; ++t) {
                        P_PlayerThink(w, true, -1);
                        P_RunThinkers(w);
                    }
                    check(target->health < hp0, "pistol fire damages a barrel in front");
                }

                // P6b: monster AI on the real map path. Spawn a possessed 96 units in front,
                // facing the player, and let its A_Look/A_Chase state chain run. It must wake
                // and either close distance or land a hit (both go through the live AI path).
                {
                    mobj_t* mo = w.player.mo;
                    int fi = (mo->angle >> ANGLETOFINESHIFT) & FINEMASK;
                    fixed_t dist = 96 << FRACBITS;
                    fixed_t mx = mo->x + FixedMul(finecosine[fi], dist);
                    fixed_t my = mo->y + FixedMul(finesine[fi], dist);
                    mobj_t* mon = P_SpawnMobj(w, mx, my, mo->z, MT_POSSESSED);
                    mon->angle = mo->angle + ANG180;     // face back toward the player (in FOV)
                    mon->health = 100000;                // ignore crossfire from the real map
                    w.player.health = 1000; mo->health = 1000;   // survive the AI loop
                    w.player.dead = false;
                    int hp0 = w.player.health;
                    fixed_t d0 = std::abs(mon->x - mo->x) + std::abs(mon->y - mo->y);
                    bool woke = false, moved = false, hit = false;
                    for (int t = 0; t < 200; ++t) {
                        P_RunThinkers(w);
                        fixed_t d = std::abs(mon->x - mo->x) + std::abs(mon->y - mo->y);
                        if (d != d0) moved = true;
                        if (w.player.health < hp0) hit = true;
                        if (mon->target == mo) woke = true;
                    }
                    check(woke, "monster AI wakes and targets the player");
                    check(moved || hit, "awake monster chases or attacks the player");
                    if (mon->health > 0) P_RemoveMobj(w, *mon);   // keep later checks clean
                    w.player.health = 100; mo->health = 100;
                    w.player.dead = false;
                }

                // P6a: synthetic door next to the player (room | closed door). Activate it and
                // require the door sector's ceiling to rise; this exercises the real mover path.
                {
                    const int roomSec = static_cast<int>(map.sectors.size());
                    sector_t R; R.floorheight = 0; R.ceilingheight = 128;
                    map.sectors.push_back(R);
                    const int doorSec = static_cast<int>(map.sectors.size());
                    sector_t D; D.floorheight = 0; D.ceilingheight = 0;
                    map.sectors.push_back(D);
                    const int sideRoom = static_cast<int>(map.sides.size());
                    side_t sr; sr.sector = roomSec; map.sides.push_back(sr);
                    const int sideDoor = static_cast<int>(map.sides.size());
                    side_t sd; sd.sector = doorSec; map.sides.push_back(sd);

                    const fixed_t lx = w.player.mo->x + (32 << FRACBITS);
                    const fixed_t ly = w.player.mo->y;
                    const int v0 = static_cast<int>(map.vertices.size());
                    map.vertices.push_back({lx, ly - (1000 << FRACBITS)});
                    map.vertices.push_back({lx, ly + (1000 << FRACBITS)});
                    line_t L; L.v1 = v0; L.v2 = v0 + 1; L.flags = ML_TWOSIDED;
                    L.special = 1; L.tag = 0;
                    L.sidenum[0] = sideDoor;    // east (front) = door
                    L.sidenum[1] = sideRoom;    // west = player's room
                    const int li = static_cast<int>(map.lines.size());
                    map.lines.push_back(L);

                    P_ActivateLine(w, map.lines[li], w.player.mo);
                    for (int t = 0; t < 70; ++t) P_RunThinkers(w);
                    check(map.sectors[doorSec].ceilingheight > 0,
                          "use-activating a door raises its ceiling");
                }

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
            if (std::string(argv[i]) == "--aittest" && i + 1 < argc) {
                // Headless AI soak on E1M1: one monster of each family in front of the player
                // (separate runs so they don't block each other). Requires wake, chase and
                // damage/projectile evidence through the real map/collision/render paths.
                WadFile wad(argv[i + 1]);
                MapData map = loadMap(wad, "E1M1");
                const mobjtype_t types[] = { MT_POSSESSED, MT_TROOP, MT_SERGEANT,
                                             MT_HEAD, MT_BRUISER, MT_SKULL };
                const char* names[] = { "possessed", "imp", "demon", "caco", "baron", "lost soul" };
                int failures = 0;
                bool anyDamaged = false, anyMissile = false, anyClosed = false;
                auto check = [&](bool ok, const std::string& what) {
                    std::cout << (ok ? "PASS: " : "FAIL: ") << what << "\n";
                    if (!ok) ++failures;
                };
                for (int k = 0; k < 6; ++k) {
                    World w; w.map = &map; w.bm = &map.blockmap; w.mapName = "E1M1";
                    P_SetupLevel(w, map);
                    mobj_t* pm = w.player.mo;
                    const int fi = (pm->angle >> ANGLETOFINESHIFT) & FINEMASK;
                    const fixed_t d = 96 << FRACBITS;
                    mobj_t* mon = P_SpawnMobj(
                        w, pm->x + FixedMul(finecosine[fi], d),
                           pm->y + FixedMul(finesine[fi], d), pm->z, types[k]);
                    mon->angle = pm->angle + ANG180;
                    mon->health = 100000;
                    w.player.health = 100000; pm->health = 100000;
                    const int hp0 = w.player.health;
                    const fixed_t startDist = std::abs(mon->x - pm->x) + std::abs(mon->y - pm->y);
                    bool woke = false;
                    int peakMissiles = 0;
                    for (int t = 0; t < 200; ++t) {
                        P_RunThinkers(w);
                        if (mon->target == pm) woke = true;
                        int missiles = 0;
                        for (const mobj_t& mo : w.mobjs)
                            if ((mo.flags & MF_MISSILE) && !mo.removePending) ++missiles;
                        if (missiles > peakMissiles) peakMissiles = missiles;
                    }
                    const bool damaged = w.player.health < hp0;
                    const fixed_t endDist = std::abs(mon->x - pm->x) + std::abs(mon->y - pm->y);
                    const bool closed = endDist < startDist;
                    anyDamaged = anyDamaged || damaged;
                    anyMissile = anyMissile || peakMissiles > 0;
                    anyClosed = anyClosed || closed;
                    std::cout << "  " << names[k] << ": woke=" << woke
                              << " damaged=" << damaged << " missiles=" << peakMissiles
                              << " dist=" << (startDist >> FRACBITS) << "->"
                              << (endDist >> FRACBITS) << "\n";
                    check(woke, std::string("AI soak: ") + names[k] + " wakes");
                }
                check(anyDamaged, "AI soak: monster attacks damaged the player");
                check(anyMissile, "AI soak: imp/caco/baron fired projectiles");
                check(anyClosed, "AI soak: monsters closed distance");
                std::cout << (failures ? "AITTEST FAILED\n" : "AITTEST PASSED\n");
                return failures ? 1 : 0;
            }
            if (std::string(argv[i]) == "--soundtest" && i + 1 < argc) {
                // Decode every DS lump through the SDL backend and play a few sounds.
                WadFile wad(argv[i + 1]);
                if (!i_sound::init()) {
                    std::cout << "SOUNDTEST SKIPPED (no audio device)\n";
                    return 0;
                }
                i_sound::loadWad(wad);
                I_SetSoundHook(&i_sound::play);
                const int n = i_sound::loadedCount();
                std::cout << "SOUNDTEST: decoded " << n << " / " << (NUMSFX - 1)
                          << " sound effects\n";
                I_StartSound(SFX_PISTOL);
                SDL_Delay(150);
                I_StartSound(SFX_DOROPN);
                SDL_Delay(300);
                I_StartSound(NUMSFX - 1);     // valid id sanity
                int songs = 0, badSongs = 0;
                for (int l = 0; l < wad.numLumps(); ++l) {
                    const std::string nm = wad.lumpName(l);
                    if (nm.size() < 2 || nm[0] != 'D' || nm[1] != '_') continue;
                    auto raw = wad.readLump(l);
                    MidiSong song;
                    if (parseMidi(raw.data(), raw.size(), song)) ++songs; else ++badSongs;
                }
                std::cout << "SOUNDTEST: parsed " << songs << " MIDI tracks ("
                          << badSongs << " bad)\n";
                const float rms = i_sound::musicRms(wad, "D_E1M1", 2);
                std::cout << "SOUNDTEST: D_E1M1 synth RMS=" << rms << "\n";
                if (songs > 0) i_sound::playMusic(wad, "D_E1M1");
                SDL_Delay(1500);
                i_sound::stopMusic();
                I_SetSoundHook(nullptr);
                i_sound::shutdown();
                const bool pass = n > 50 && songs > 20 && badSongs == 0 && rms > 200.0f;
                std::cout << (pass ? "SOUNDTEST PASSED\n" : "SOUNDTEST FAILED\n");
                return pass ? 0 : 1;
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

        for (int ci = 1; ci < argc; ++ci) {
            if (std::string(argv[ci]) == "--campaigntest" && ci + 1 < argc) {
                // P7 integration: every map in E1-E4 loads, spawns and ticks without
                // crashing; the campaign chain terminates; a save round-trips.
                WadFile wad(argv[ci + 1]);
                int failures = 0;
                auto check = [&](bool ok, const std::string& what) {
                    std::cout << (ok ? "PASS: " : "FAIL: ") << what << "\n";
                    if (!ok) ++failures;
                };
                int mapCount = 0, monsterTotal = 0;
                for (int ep = 1; ep <= 4; ++ep) {
                    for (int mn = 1; mn <= 9; ++mn) {
                        std::string name = "E#M#";
                        name[1] = static_cast<char>('0' + ep);
                        name[3] = static_cast<char>('0' + mn);
                        MapData map = loadMap(wad, name);
                        World w; w.map = &map; w.bm = &map.blockmap; w.mapName = name;
                        P_SetupLevel(w, map);
                        if (!w.player.mo) { check(false, name + " player spawn"); continue; }
                        for (int t = 0; t < 15; ++t) P_RunThinkers(w);
                        // Every map must expose a working exit line (P7 level flow).
                        bool exitFound = false;
                        bool exitSecret = false;
                        for (const line_t& L : map.lines) {
                            if (L.special == 11 || L.special == 52 ||
                                L.special == 51 || L.special == 124) {
                                exitFound = true;
                                exitSecret = (L.special == 51 || L.special == 124);
                                P_ActivateLine(w, L, w.player.mo);
                                break;
                            }
                        }
                        const bool bossMap = (name == "E2M8" || name == "E3M8");
                        check((exitFound && w.levelExitRequested && w.secretExit == exitSecret) ||
                              bossMap,
                              name + " exit line sets the level-exit request");
                        w.levelExitRequested = false;
                        w.secretExit = false;
                        ++mapCount;
                        monsterTotal += w.totalKills;
                        std::cout << "  " << name << ": mobjs=" << w.mobjs.size()
                                  << " monsters=" << w.totalKills
                                  << " items=" << w.totalItems
                                  << " playerHP=" << w.player.health << "\n";
                        check(w.mobjs.size() > 1, name + " spawns things");
                    }
                }
                check(mapCount == 36, "all 36 E1-E4 maps load and tick");

                std::string cur = "E1M1";
                int steps = 0;
                while (!cur.empty() && steps < 64) { cur = G_NextLevelName(cur, false); ++steps; }
                check(cur.empty() && steps == 32, "normal campaign chain reaches the end after 32 maps");

                // Save/load round-trip on E1M1.
                MapData map = loadMap(wad, "E1M1");
                World w; w.map = &map; w.bm = &map.blockmap; w.mapName = "E1M1";
                P_SetupLevel(w, map);
                w.player.ammo[AM_SHELL] = 23;
                w.tic = 1234;
                const std::string path = "doomcpp-campaigntest-save.dat";
                bool okSave = G_SaveGame(w, path);
                w.player.ammo[AM_SHELL] = 0;
                w.tic = 0;
                bool okLoad = okSave && G_LoadGame(w, path);
                check(okSave && okLoad && w.player.ammo[AM_SHELL] == 23 && w.tic == 1234,
                      "save/load round-trips on E1M1");
                std::remove(path.c_str());

                std::cout << (failures ? "CAMPAIGNTEST FAILED (" : "CAMPAIGNTEST PASSED (")
                          << failures << " failures, " << mapCount << " maps, "
                          << monsterTotal << " monsters)\n";
                return failures ? 1 : 0;
            }
        }
        // --- window / first-person 3D mode (P7 campaign loop) ---
        std::cout << "doomcpp 0.1.0  (P7 campaign)  WASD move, arrows turn, Space use, Ctrl fire,"
                     " 1-8 weapons, G toss, F5/F9 save/load, ESC quit\n";
        bool noSound = false;
        std::string startLevel = "E1M1";
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--nosound") noSound = true;
            else if (a == "--warp" && i + 1 < argc) startLevel = argv[++i];
        }
        WadFile wad("assets/freedoom1.wad");
        const bool audio = !noSound && i_sound::init();
        if (audio) {
            i_sound::loadWad(wad);
            I_SetSoundHook(&i_sound::play);
        }
        TextureLookup tex(wad);
        SpriteCache sprites(wad);
        StatusBar stbar(wad);
        Font font(wad);
        Intermission inter(wad);

        MapData map;
        World w;
        std::string levelName;
        auto loadLevel = [&](const std::string& name, const player_t* carry) {
            map = loadMap(wad, name);
            w.map = &map;
            w.bm = &map.blockmap;
            w.mapName = name;
            levelName = name;
            P_SetupLevel(w, map, carry);
            if (audio) i_sound::playMusic(wad, "D_" + name);
        };
        loadLevel(startLevel, nullptr);

        enum class GameState { Playing, Intermission, Dead, Victory };
        GameState state = GameState::Playing;
        std::string nextLevel;
        const std::string SAVE_PATH = "doomcpp-quicksave.dat";

        constexpr int FB_W = 320, FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp - P7")) return 1;
        std::vector<std::uint32_t> fb(static_cast<size_t>(FB_W) * FB_H, 0);

        // Vanilla forwardmove/sidemove (p_user.c) scaled by 2048 for P_Thrust.
        const fixed_t walkThrust = 25 * 2048;
        const fixed_t runThrust  = 50 * 2048;
        const fixed_t walkStrafe = 24 * 2048;
        const fixed_t runStrafe  = 40 * 2048;
        const angle_t turnSpeed = degreesToBam(4);      // ~4 deg/tic
        constexpr uint32_t TIC_MS = 1000 / 35;
        Input in{};
        bool running = true;
        uint32_t prev = SDL_GetTicks(), acc = 0;
        while (running) {
            running = i_video::pollEvents(in);

            if (state == GameState::Intermission || state == GameState::Victory) {
                if (in.enter || in.use) {
                    in.enter = in.use = false;
                    if (state == GameState::Victory) {
                        loadLevel("E1M1", nullptr);
                        state = GameState::Playing;
                    } else if (nextLevel.empty()) {
                        state = GameState::Victory;
                        if (audio) i_sound::playMusic(wad, "D_VICTOR");
                    } else {
                        player_t carry = w.player;
                        loadLevel(nextLevel, &carry);
                        state = GameState::Playing;
                    }
                }
            } else if (state == GameState::Dead) {
                if (in.enter) {
                    in.enter = false;
                    loadLevel(levelName, nullptr);
                    state = GameState::Playing;
                }
            } else {
                // ---- Playing: quick save/load, toss, tics ----
                if (in.save) {
                    in.save = false;
                    I_Printf(G_SaveGame(w, SAVE_PATH) ? "game saved" : "save failed");
                }
                if (in.load) {
                    in.load = false;
                    std::string savedLevel;
                    if (G_SaveGameLevel(SAVE_PATH, savedLevel)) {
                        if (savedLevel != levelName) loadLevel(savedLevel, nullptr);
                        if (G_LoadGame(w, SAVE_PATH)) {
                            state = GameState::Playing;
                            I_Printf("game loaded");
                        } else {
                            I_Printf("load failed");
                        }
                    } else {
                        I_Printf("no save file");
                    }
                }
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
                    if (in.use && !w.player.dead) { in.use = false; P_UseLines(w, w.player); }
                    P_PlayerThink(w, in.fire, in.weaponSwitch);
                    in.weaponSwitch = -1;
                    const fixed_t thrust = in.run ? runThrust : walkThrust;
                    const fixed_t side   = in.run ? runStrafe : walkStrafe;
                    P_MovePlayerWorld(w, w.player,
                                      fwd * thrust, str * side, trn * turnSpeed);
                    if (!w.player.dead) P_TouchSpecialThings(w, *w.player.mo);
                    P_RunThinkers(w);
                    if (!w.player.dead) P_CheckSecrets(w, *w.player.mo);
                    if (w.levelExitRequested) break;
                }
                if (w.player.dead || w.player.health <= 0) {
                    state = GameState::Dead;
                } else if (w.levelExitRequested) {
                    nextLevel = G_NextLevelName(levelName, w.secretExit);
                    state = GameState::Intermission;
                    if (audio) i_sound::playMusic(wad, "D_INTER");
                }
            }

            in.enter = in.save = in.load = false;
            in.use = false;

            if (state == GameState::Intermission || state == GameState::Victory) {
                int kills = w.totalKills > 0 ? (w.player.killcount * 100 / w.totalKills) : 0;
                int items = w.totalItems > 0 ? (w.player.itemcount * 100 / w.totalItems) : 0;
                int secrets = w.totalSecrets > 0
                    ? (w.player.secretsFound * 100 / w.totalSecrets) : 0;
                inter.draw(fb.data(), FB_W, FB_H, levelName, kills, items, secrets,
                           nextLevel, state == GameState::Victory);
            } else {
                P_BuildThingsBySubsec(w);
                constexpr int VIEW_H = FB_H - 32;   // vanilla 320x168 view + status bar
                R_RenderView(fb.data(), FB_W, VIEW_H, map, tex,
                             static_cast<float>(w.player.mo->x >> FRACBITS),
                             static_cast<float>(w.player.mo->y >> FRACBITS),
                             bamToRendererRad(w.player.mo->angle),
                             static_cast<float>(w.player.viewz >> FRACBITS),
                             w.tic, &sprites, &w.thingsBySubsec);
                drawPlayerSprites(w, sprites, fb.data(), FB_W, VIEW_H);
                stbar.draw(fb.data(), FB_W, FB_H, w.player);
                if (state == GameState::Dead) {
                    font.drawCentered(fb.data(), FB_W, FB_H, 76, "YOU DIED", 2);
                    font.drawCentered(fb.data(), FB_W, FB_H, 104, "PRESS ENTER TO RESTART", 1);
                }
            }
            i_video::present(fb.data());
        }

        i_video::shutdown();
        if (audio) i_sound::shutdown();
        return 0;
    } catch (const std::exception& e) {
        I_Printf(std::string("Fatal: ") + e.what());
        return 1;
    }
}
