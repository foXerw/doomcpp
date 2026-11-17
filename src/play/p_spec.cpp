#include "p_spec.h"
#include "p_setup.h"
#include "p_maputl.h"
#include "info.h"
#include "core/m_tables.h"
#include "core/i_system.h"
#include "play/sfx.h"
#include "p_map.h"        // VIEWHEIGHT
#include "p_inter.h"   // P_DamageMobj (crush)
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace {

int sideSector(const MapData& m, const line_t& L, int side) {
    if (side < 0 || side > 1 || L.sidenum[side] < 0) return -1;
    int sd = L.sidenum[side];
    if (sd >= static_cast<int>(m.sides.size())) return -1;
    return m.sides[sd].sector;
}

int otherSideSector(const MapData& m, const line_t& L, int sec) {
    int f = sideSector(m, L, 0), b = sideSector(m, L, 1);
    if (f == sec) return b;
    if (b == sec) return f;
    return -1;
}

// Collect the min/max neighbouring floor/ceiling heights around a sector.
struct Neighbors { int floor = 0, ceil = 0; };
Neighbors neighborHeights(const MapData& m, int sec) {
    Neighbors n;
    bool haveFloor = false, haveCeil = false;
    for (const line_t& L : m.lines) {
        int other = otherSideSector(m, L, sec);
        if (other < 0 || other == sec || other >= static_cast<int>(m.sectors.size())) continue;
        int f = m.sectors[other].floorheight, c = m.sectors[other].ceilingheight;
        if (!haveFloor || f < n.floor) { n.floor = f; haveFloor = true; }
        if (!haveCeil  || c < n.ceil)  { n.ceil  = c; haveCeil = true; }
    }
    if (!haveFloor) n.floor = m.sectors[sec].floorheight + 128;
    if (!haveCeil)  n.ceil  = m.sectors[sec].ceilingheight;
    return n;
}

// Lowest neighbouring floor strictly above / highest strictly below the current one.
int nextFloorUp(const MapData& m, int sec) {
    int cur = m.sectors[sec].floorheight;
    int best = 0x7fffffff;
    for (const line_t& L : m.lines) {
        int other = otherSideSector(m, L, sec);
        if (other < 0 || other == sec) continue;
        int f = m.sectors[other].floorheight;
        if (f > cur && f < best) best = f;
    }
    return (best == 0x7fffffff) ? cur + 64 : best;
}
int nextFloorDown(const MapData& m, int sec) {
    int cur = m.sectors[sec].floorheight;
    int best = -0x7fffffff;
    for (const line_t& L : m.lines) {
        int other = otherSideSector(m, L, sec);
        if (other < 0 || other == sec) continue;
        int f = m.sectors[other].floorheight;
        if (f < cur && f > best) best = f;
    }
    return (best == -0x7fffffff) ? cur - 64 : best;
}

// Carry/limit every mobj in a sector to its new heights (the lift/door "carries you").
void syncSectorThings(World& w, int sec) {
    if (!w.map || sec < 0 || sec >= static_cast<int>(w.map->sectors.size())) return;
    const sector_t& s = w.map->sectors[sec];
    for (mobj_t& mo : w.mobjs) {
        if (mo.removePending) continue;
        if (sectorOf(*w.map, R_PointInSubsector(*w.map, mo.x, mo.y)) != sec) continue;
        mo.floorz   = static_cast<fixed_t>(s.floorheight)   << FRACBITS;
        mo.ceilingz = static_cast<fixed_t>(s.ceilingheight) << FRACBITS;
        if (mo.z < mo.floorz) mo.z = mo.floorz;
        fixed_t maxz = mo.ceilingz - mo.height;
        if (maxz < mo.floorz) maxz = mo.floorz;
        if (mo.z > maxz) mo.z = maxz;
        // No room left means the mover is crushing it: vanilla kills what a door/lift/
        // rising floor closes onto. Without this the player just sits inside the ceiling
        // with the camera clamped, which reads as the engine losing an argument with
        // geometry.
        if ((mo.flags & MF_SHOOTABLE) && s.floorheight + (mo.height >> FRACBITS) > s.ceilingheight)
            P_DamageMobj(w, mo, nullptr, 10000);
    }
}

Mover* findMover(World& w, int sec, MoverKind kind) {
    for (Mover& mv : w.movers)
        if (mv.sector == sec && mv.kind == kind) return &mv;
    return nullptr;
}

void startDoor(World& w, int sec, int waitTics, bool stayOpen, int speed) {
    if (!w.map || sec < 0 || sec >= static_cast<int>(w.map->sectors.size())) return;
    const sector_t& s = w.map->sectors[sec];
    const int bottom = s.floorheight;
    int top = P_DoorOpenHeight(*w.map, sec);
    if (top <= bottom + 1) return;                       // no room to open
    Mover* mv = findMover(w, sec, MoverKind::Door);
    if (!mv) { w.movers.push_back(Mover{}); mv = &w.movers.back(); }
    mv->kind = MoverKind::Door; mv->sector = sec;
    mv->top = top; mv->bottom = bottom; mv->speed = speed;
    mv->waitTics = waitTics; mv->stayOpen = stayOpen;
    // Open if currently closed, close if already open (manual doors toggle).
    mv->state = (s.ceilingheight > bottom + 1) ? 2 : 0;
    I_StartSound(mv->state == 0 ? SFX_DOROPN : SFX_DORCLS);
}

void startPlat(World& w, int sec, int speed, int waitTics) {
    if (!w.map || sec < 0 || sec >= static_cast<int>(w.map->sectors.size())) return;
    const sector_t& s = w.map->sectors[sec];
    const int bottom = s.floorheight;
    const int top = nextFloorUp(*w.map, sec);            // lift rises to the next floor level
    if (top <= bottom) return;
    Mover* mv = findMover(w, sec, MoverKind::Plat);
    if (!mv) { w.movers.push_back(Mover{}); mv = &w.movers.back(); }
    mv->kind = MoverKind::Plat; mv->sector = sec;
    mv->top = top; mv->bottom = bottom; mv->speed = speed;
    mv->waitTics = waitTics; mv->stayOpen = false; mv->state = 0;
    I_StartSound(SFX_STNMOV);
}

void startFloorOrCeiling(World& w, int sec, MoverKind kind, int target, int speed) {
    if (!w.map || sec < 0 || sec >= static_cast<int>(w.map->sectors.size())) return;
    // A surface can never travel past the opposite one: a "raise floor to the highest
    // neighbouring floor" line whose neighbour sits above this sector's ceiling would
    // otherwise drive the floor through it and leave anyone standing there inside the
    // ceiling (--clipaudit found exactly that in E1M7 and E3M5).
    if (kind == MoverKind::Floor && target > w.map->sectors[sec].ceilingheight)
        target = w.map->sectors[sec].ceilingheight;
    if (kind == MoverKind::Ceiling && target < w.map->sectors[sec].floorheight)
        target = w.map->sectors[sec].floorheight;
    Mover* mv = findMover(w, sec, kind);
    if (!mv) { w.movers.push_back(Mover{}); mv = &w.movers.back(); }
    mv->kind = kind; mv->sector = sec;
    mv->top = target; mv->bottom = target; mv->speed = speed;
    mv->waitTics = 0; mv->stayOpen = true; mv->state = 0;
    if (target != w.map->sectors[sec].floorheight ||
        kind == MoverKind::Ceiling)
        I_StartSound(SFX_STNMOV);
}

template <typename Fn>
void forTaggedSectors(const MapData& m, int tag, Fn fn) {
    if (tag <= 0) return;
    for (int i = 0; i < static_cast<int>(m.sectors.size()); ++i)
        if (m.sectors[i].tag == tag) fn(i);
}


// Trigger style comes from the special table: 1 = usable (Space within reach),
// 2 = triggered by crossing, 3 = both. A teleport line is authored as a wall button in some
// maps and as a floor pad you walk onto in others, so it needs both.
int triggerFor(int special) {
    const SpecialDef* d = P_LookupSpecial(special);
    return d ? d->trigger : 0;
}

bool swapSwitchTextures(MapData& m, const line_t& L) {
    auto swap = [](char* name) {
        if (name && std::strncmp(name, "SW1", 3) == 0) name[2] = '2';
    };
    for (int side = 0; side < 2; ++side) {
        if (L.sidenum[side] < 0) continue;
        side_t& sd = m.sides[L.sidenum[side]];
        bool hadSwitch = std::strncmp(sd.midtexture, "SW1", 3) == 0 ||
                         std::strncmp(sd.toptexture, "SW1", 3) == 0 ||
                         std::strncmp(sd.bottomtexture, "SW1", 3) == 0;
        if (!hadSwitch) continue;
        swap(sd.toptexture); swap(sd.midtexture); swap(sd.bottomtexture);
        return true;   // only the first matching side
    }
    return false;
}

// The sector on the side of the line the mover is NOT in (a door, or a teleport
// destination, lives there). Falls back to the back sidedef when the mover is null.
int otherSideOfMover(const MapData& m, const line_t& L, const mobj_t* who) {
    int front = sideSector(m, L, 0), back = sideSector(m, L, 1);
    if (front < 0 || back < 0 || !who ||
        L.v1 < 0 || L.v2 < 0 ||
        L.v1 >= static_cast<int>(m.vertices.size()) ||
        L.v2 >= static_cast<int>(m.vertices.size()))
        return back >= 0 ? back : front;
    const double ax = static_cast<double>(m.vertices[L.v1].x) / FRACUNIT;
    const double ay = static_cast<double>(m.vertices[L.v1].y) / FRACUNIT;
    const double bx = static_cast<double>(m.vertices[L.v2].x) / FRACUNIT;
    const double by = static_cast<double>(m.vertices[L.v2].y) / FRACUNIT;
    const double px = static_cast<double>(who->x) / FRACUNIT;
    const double py = static_cast<double>(who->y) / FRACUNIT;
    const bool onFront = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) < 0;
    return onFront ? back : front;
}

// Teleport to the other side of this line: vanilla picks the nearest teleport landing
// thing inside the sector behind the line (not a tag-matched one -- verified against
// the IWAD, where every such line has its landing thing behind it and none in a sector
// carrying its tag). Anything standing on the pad is telefragged.
bool teleportOtherSide(World& w, const line_t& L, mobj_t& mo) {
    if (!w.map) return false;
    // Two authoring shapes exist in the IWAD: a button set in a wall (the landing pad is
    // the sector on the far side) and a floor pad you walk onto (by the time the crossing
    // fires, the mover is already standing in the landing sector). Try the far side first,
    // then the mover's own side.
    int cand[2] = { otherSideOfMover(*w.map, L, &mo), sideSector(*w.map, L, 0) };
    const int here = sectorOf(*w.map, R_PointInSubsector(*w.map, mo.x, mo.y));
    cand[1] = (cand[1] == cand[0]) ? here : cand[1];
    const thing_t* best = nullptr;
    int sec = -1;
    fixed_t bestDist = 0x7fffffff;
    for (int pass = 0; pass < 2 && !best; ++pass) {
        const int trySec = cand[pass];
        if (trySec < 0) continue;
        for (const thing_t& th : w.map->things) {
            if (th.type != 14) continue;
            const fixed_t x = static_cast<fixed_t>(th.x) << FRACBITS;
            const fixed_t y = static_cast<fixed_t>(th.y) << FRACBITS;
            if (sectorOf(*w.map, R_PointInSubsector(*w.map, x, y)) != trySec) continue;
            const fixed_t dd = std::abs(x - mo.x) + std::abs(y - mo.y);
            if (dd < bestDist) { bestDist = dd; best = &th; sec = trySec; }
        }
    }
    if (!best || sec < 0) return false;
    const fixed_t dx = static_cast<fixed_t>(best->x) << FRACBITS;
    const fixed_t dy = static_cast<fixed_t>(best->y) << FRACBITS;
    // Telefrag: whatever occupies the landing spot dies (vanilla P_TeleportMove).
    for (mobj_t& th : w.mobjs) {
        if (&th == &mo || th.removePending || !(th.flags & MF_SHOOTABLE)) continue;
        if (std::abs(th.x - dx) > (16 << FRACBITS) || std::abs(th.y - dy) > (16 << FRACBITS)) continue;
        P_DamageMobj(w, th, nullptr, 10000);
    }
    mo.x = dx; mo.y = dy;
    mo.momx = mo.momy = mo.momz = 0;
    mo.angle = degreesToBam(best->angleDeg);
    mo.subsector = R_PointInSubsector(*w.map, dx, dy);
    mo.floorz   = static_cast<fixed_t>(w.map->sectors[sec].floorheight)   << FRACBITS;
    mo.ceilingz = static_cast<fixed_t>(w.map->sectors[sec].ceilingheight) << FRACBITS;
    mo.z = mo.floorz;
    if (w.player.mo == &mo) w.player.viewz = mo.floorz + VIEWHEIGHT;
    I_StartSound(SFX_TELEPT);
    return true;
}

bool teleportPlayer(World& w, mobj_t& mo, int tag) {
    if (!w.map || tag <= 0) return false;
    for (const thing_t& th : w.map->things) {
        if (th.type != 14) continue;   // teleport landing
        fixed_t x = static_cast<fixed_t>(th.x) << FRACBITS;
        fixed_t y = static_cast<fixed_t>(th.y) << FRACBITS;
        int sec = sectorOf(*w.map, R_PointInSubsector(*w.map, x, y));
        if (sec < 0 || w.map->sectors[sec].tag != tag) continue;
        mo.x = x; mo.y = y;
        mo.angle = degreesToBam(th.angleDeg);
        mo.momx = mo.momy = mo.momz = 0;
        mo.subsector = R_PointInSubsector(*w.map, x, y);
        mo.floorz   = static_cast<fixed_t>(w.map->sectors[sec].floorheight)   << FRACBITS;
        mo.ceilingz = static_cast<fixed_t>(w.map->sectors[sec].ceilingheight) << FRACBITS;
        mo.z = mo.floorz;
        I_StartSound(SFX_TELEPT);
        return true;
    }
    return false;
}

}  // namespace

int P_DoorOpenHeight(const MapData& m, int sec) {
    Neighbors n = neighborHeights(m, sec);
    return std::max(m.sectors[sec].floorheight + 1, n.ceil - 4);
}

bool P_SectorHasMover(const World& w, int sector) {
    for (const Mover& mv : w.movers) if (mv.sector == sector && mv.state != 3) return true;
    return false;
}

void P_LowerFloorByTag(World& w, int tag) {
    if (!w.map || tag <= 0) return;
    forTaggedSectors(*w.map, tag, [&](int s) {
        startFloorOrCeiling(w, s, MoverKind::Floor, nextFloorDown(*w.map, s), 4);
    });
}

// ---- P8b sector light specials ----
//
// Vanilla drives these from three thinker shapes (glow / flash / strobe). The sector's
// authored lightlevel is the bright end; the dark end is the same distance below it that
// vanilla's tables use (84 units), clamped so a dark sector never goes black. Doomcpp's
// own periods, chosen to read as "breathing", "flashing" and "strobing" at 35 tics/s.
namespace {
bool isGlow(int sp)    { return sp == 13 || sp == 14; }
bool isFlash(int sp)   { return sp == 5 || sp == 6 || sp == 7 || sp == 8; }
bool isStrobe(int sp)  { return sp == 10 || sp == 11 || sp == 12 || sp == 17; }
// Vanilla's light tables are absolute, not offsets from the sector's own light: a sector
// authored at lightlevel 0 with a flash type still flashes bright. doomcpp's own version of
// that rule -- the bright end is never dimmer than what the map authored, the dark end is
// 84 below it (or 0), and the two ends must differ or there is nothing to animate.
void lightEnds(int special, int base, int& bright, int& dark) {
    const int hi = isStrobe(special) ? 200 : 176;
    bright = std::max(base, hi);
    dark   = base > 84 ? base - 84 : 0;
    if (bright <= dark) bright = dark + 64;       // guarantee a visible swing
    if (bright > 255) { bright = 255; dark = 191; }
}
}  // namespace

void P_InitLights(World& w) {
    w.lights.clear();
    if (!w.map) return;
    for (int i = 0; i < static_cast<int>(w.map->sectors.size()); ++i) {
        const sector_t& s = w.map->sectors[i];
        if (!isGlow(s.special) && !isFlash(s.special) && !isStrobe(s.special)) continue;
        LightSpecial ls;
        ls.sector = i; ls.base = s.lightlevel; ls.special = s.special;
        lightEnds(s.special, s.lightlevel, ls.bright, ls.dark);
        ls.tics = isGlow(s.special) ? 8 : (isFlash(s.special) ? (s.special <= 6 ? 12 : 8) : 8);
        ls.step = 0;
        w.lights.push_back(ls);
    }
}

void P_RunLights(World& w) {
    if (!w.map) return;
    for (LightSpecial& ls : w.lights) {
        if (ls.sector < 0 || ls.sector >= static_cast<int>(w.map->sectors.size())) continue;
        sector_t& s = w.map->sectors[ls.sector];
        const int dark = ls.dark, bright = ls.bright;
        if (isGlow(ls.special)) {
            // ease between the bright and dark end, 3 units per period
            if (--ls.tics <= 0) {
                ls.tics = 8;
                const int next = s.lightlevel + 3 * (ls.step ? -ls.step : 1);
                if (next >= bright) { s.lightlevel = bright; ls.step = -1; }
                else if (next <= dark) { s.lightlevel = dark; ls.step = 1; }
                else { s.lightlevel = next; ls.step = next > s.lightlevel ? 1 : -1; }
            }
        } else if (isFlash(ls.special)) {
            // Alternate on a phase, never on the current lightlevel: comparing to `base`
            // latches a sector whose authored light already equals the dark end (8 sectors
            // in this IWAD have lightlevel 0 or 32 with a flash type) and it stops flashing.
            if (--ls.tics <= 0) {
                ls.tics = (ls.special <= 6) ? 12 : 8;
                ls.step ^= 1;
                s.lightlevel = ls.step ? dark : bright;
            }
        } else {   // strobe: two bright flashes with long dark holds
            if (--ls.tics <= 0) {
                const bool on = (ls.step & 1) == 0;
                ls.tics = on ? 8 : 100;
                ls.step = (ls.step + 1) & 1;
                s.lightlevel = on ? bright : dark;
            }
        }
    }
}

void P_RunMovers(World& w) {
    if (!w.map) return;
    for (Mover& mv : w.movers) {
        if (mv.state == 3) continue;
        if (mv.state == 1) {                     // waiting
            if (mv.waitTics > 0) --mv.waitTics;
            if (mv.waitTics <= 0) mv.state = 2;
            continue;
        }
        sector_t& s = w.map->sectors[mv.sector];
        int& h = (mv.kind == MoverKind::Door || mv.kind == MoverKind::Ceiling)
                     ? s.ceilingheight : s.floorheight;
        // Clamp against the opposite surface every tic, not only when the mover starts:
        // a switch line can start a floor and a ceiling moving at the same time, and the
        // start-time clamp alone let the floor walk past a ceiling that was dropping.
        int target = (mv.state == 0) ? mv.top : mv.bottom;
        if (mv.kind == MoverKind::Floor && target > s.ceilingheight) target = s.ceilingheight;
        if (mv.kind == MoverKind::Ceiling && target < s.floorheight) target = s.floorheight;
        int newH = h;
        if (h < target)      newH = std::min(h + mv.speed, target);
        else if (h > target) newH = std::max(h - mv.speed, target);

        // A closing door reverses if a standing thing would be crushed (vanilla).
        if (mv.kind == MoverKind::Door && mv.state == 2 && newH < h) {
            const fixed_t nc = static_cast<fixed_t>(newH) << FRACBITS;
            bool blocked = false;
            for (const mobj_t& mo : w.mobjs) {
                if (mo.removePending) continue;
                if (sectorOf(*w.map, R_PointInSubsector(*w.map, mo.x, mo.y)) != mv.sector) continue;
                if (mo.z + mo.height > nc) { blocked = true; break; }
            }
            if (blocked) { mv.state = 0; mv.waitTics = 0; continue; }
        }
        h = newH;
        syncSectorThings(w, mv.sector);

        if (h == target) {
            if (mv.state == 0) {
                const bool stationary = mv.stayOpen ||
                    mv.kind == MoverKind::Floor || mv.kind == MoverKind::Ceiling;
                if (stationary) mv.state = 3;
                else { mv.state = 1; if (mv.waitTics <= 0) mv.waitTics = 150; }
            } else {
                mv.state = 3;
            }
        }
    }
}

// ---- P9: the special table (see p_spec.h) ----
//
// One row per linedef special this engine can act on. P_ActivateLine dispatches on the
// kind, and the reachability audit reads the same rows to decide "can the player get
// through this line", so the list of supported specials cannot drift from the code.
// Parameters are what the previous switch used, unchanged.
static const SpecialDef kSpecials[] = {
    // doors that open, wait and close again
    {1,   SpecialKind::DoorOpenClose,            1, 0,            2},
    {4,   SpecialKind::DoorOpenClose,            2, 0,            2},
    {26,  SpecialKind::DoorOpenClose,            1, KEY_BLUE,     2},
    {27,  SpecialKind::DoorOpenClose,            1, KEY_YELLOW,   2},
    {28,  SpecialKind::DoorOpenClose,            1, KEY_RED,      2},
    {90,  SpecialKind::DoorOpenClose,            1, 0,            2},
    {117, SpecialKind::DoorOpenClose,            1, 0,            4},
    // doors that open and stay open
    {2,   SpecialKind::DoorOpenStay,             2, 0,            2},
    {31,  SpecialKind::DoorOpenStay,             1, 0,            2},
    {32,  SpecialKind::DoorOpenStay,             1, KEY_BLUE,     2},
    {33,  SpecialKind::DoorOpenStay,             1, KEY_YELLOW,   2},
    {34,  SpecialKind::DoorOpenStay,             1, KEY_RED,      2},
    {63,  SpecialKind::DoorOpenStay,             1, 0,            2},
    {64,  SpecialKind::DoorOpenStay,             1, 0,            2},
    {91,  SpecialKind::DoorOpenStay,             1, 0,            2},
    {103, SpecialKind::DoorOpenStay,             1, 0,            2},
    {118, SpecialKind::DoorOpenStay,             1, 0,            4},
    {133, SpecialKind::DoorOpenStay,             1, KEY_BLUE,     2},
    {135, SpecialKind::DoorOpenStay,             1, KEY_RED,      2},
    {137, SpecialKind::DoorOpenStay,             1, KEY_YELLOW,   2},
    {3,   SpecialKind::DoorClose,                2, 0,            2},
    // plats and floors
    {62,  SpecialKind::Plat,                     1, 0,            4},
    {88,  SpecialKind::Plat,                     2, 0,            4},
    {120, SpecialKind::Plat,                     2, 0,            4},
    {58,  SpecialKind::FloorRaise,               2, 0,            4},
    {92,  SpecialKind::FloorRaise,               2, 0,            4},
    {119, SpecialKind::FloorRaise,               2, 0,            8},
    {130, SpecialKind::FloorRaise,               2, 0,            8},
    {131, SpecialKind::FloorRaise,               2, 0,            8},
    {66,  SpecialKind::FloorRaise,               1, 0,            4},
    {71,  SpecialKind::FloorRaise,               1, 0,            4},
    {55,  SpecialKind::FloorRaise,               1, 0,            4},
    {101, SpecialKind::FloorRaise,               1, 0,            4},
    {102, SpecialKind::FloorRaise,               1, 0,            4},
    {23,  SpecialKind::FloorLower,               1, 0,            4},
    {82,  SpecialKind::FloorLower,               2, 0,            4},
    {38,  SpecialKind::FloorLower,               2, 0,            4},
    {19,  SpecialKind::FloorLower,               2, 0,            4},
    {67,  SpecialKind::FloorLower,               1, 0,            4},
    {7,   SpecialKind::Stairs,                   2, 0,            4},
    {8,   SpecialKind::Stairs,                   2, 0,            4},
    {100, SpecialKind::Stairs,                   2, 0,            4},
    {127, SpecialKind::Stairs,                   2, 0,            4},
    {40,  SpecialKind::CeilingRaiseToNeighbours, 2, 0,            4},
    {69,  SpecialKind::CeilingRaiseToNeighbours, 1, 0,            4},
    {72,  SpecialKind::CeilingRaiseToNeighbours, 1, 0,            4},
    {41,  SpecialKind::CeilingLowerToFloor,      1, 0,            4},
    {43,  SpecialKind::CeilingLowerToFloor,      2, 0,            4},
    {70,  SpecialKind::CeilingLowerToFloor,      1, 0,            4},
    {44,  SpecialKind::CeilingLowerToFloor,      2, 0,            4},
    // teleports
    {39,  SpecialKind::TeleportTag,              2, 0,            0},
    {97,  SpecialKind::TeleportTag,              2, 0,            0},
    {125, SpecialKind::TeleportOther,            3, 0,            0},
    {126, SpecialKind::TeleportOther,            3, 0,            0},
    // level exits
    {11,  SpecialKind::ExitNormal,               1, 0,            0},
    {52,  SpecialKind::ExitNormal,               2, 0,            0},
    {51,  SpecialKind::ExitSecret,               1, 0,            0},
    {124, SpecialKind::ExitSecret,               2, 0,            0},
};

const SpecialDef* P_LookupSpecial(int special) {
    for (const SpecialDef& s : kSpecials) if (s.special == special) return &s;
    return nullptr;
}
bool P_LineSpecialSupported(int special) { return P_LookupSpecial(special) != nullptr; }
bool P_IsExitSpecial(int special) {
    const SpecialDef* s = P_LookupSpecial(special);
    return s && (s->kind == SpecialKind::ExitNormal || s->kind == SpecialKind::ExitSecret);
}
bool P_IsSecretExitSpecial(int special) {
    const SpecialDef* s = P_LookupSpecial(special);
    return s && s->kind == SpecialKind::ExitSecret;
}

void P_ActivateLine(World& w, const line_t& L, mobj_t* who) {
    if (!w.map) return;
    const SpecialDef* def = P_LookupSpecial(L.special);
    if (!def) return;
    if (def->key && who == w.player.mo && !(w.player.keys & def->key)) {
        I_StartSound(SFX_NOWAY);
        return;   // locked
    }

    if ((def->trigger & 1) && swapSwitchTextures(*w.map, const_cast<line_t&>(L)))
        I_StartSound(SFX_SWTCHN);

    const int front = sideSector(*w.map, L, 0);
    const int back  = sideSector(*w.map, L, 1);
    // Manual doors: the door sector is the side OPPOSITE the activating player (maps may
    // put the door on either sidedef). Remote/switch lines fall back to the tag.
    int doorSec = back;
    if (front >= 0 && back >= 0 && who &&
        L.v1 >= 0 && L.v2 >= 0 &&
        L.v1 < static_cast<int>(w.map->vertices.size()) &&
        L.v2 < static_cast<int>(w.map->vertices.size())) {
        const double ax = static_cast<double>(w.map->vertices[L.v1].x) / FRACUNIT;
        const double ay = static_cast<double>(w.map->vertices[L.v1].y) / FRACUNIT;
        const double bx = static_cast<double>(w.map->vertices[L.v2].x) / FRACUNIT;
        const double by = static_cast<double>(w.map->vertices[L.v2].y) / FRACUNIT;
        const double px = static_cast<double>(who->x) / FRACUNIT;
        const double py = static_cast<double>(who->y) / FRACUNIT;
        const double cross = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
        const bool onFront = cross < 0;          // front side = right of v1->v2
        doorSec = onFront ? back : front;
    }

    // Door lines act on the sector on the far side of the line when that is known, and on
    // every sector carrying the tag otherwise. Everything else is tag-driven.
    auto onSectors = [&](std::function<void(int)> fn) {
        const bool door = def->kind == SpecialKind::DoorOpenClose ||
                          def->kind == SpecialKind::DoorOpenStay  ||
                          def->kind == SpecialKind::DoorClose;
        if (door && doorSec >= 0) fn(doorSec);
        else forTaggedSectors(*w.map, L.tag, fn);
    };

    switch (def->kind) {
    case SpecialKind::DoorOpenClose:
        onSectors([&](int s){ startDoor(w, s, 150, false, def->speed); });
        break;
    case SpecialKind::DoorOpenStay:
        onSectors([&](int s){
            const sector_t& sec = w.map->sectors[s];
            if (sec.ceilingheight <= sec.floorheight + 1) startDoor(w, s, 0, true, def->speed);
        });
        break;
    case SpecialKind::DoorClose:
        onSectors([&](int s){
            if (w.map->sectors[s].ceilingheight > w.map->sectors[s].floorheight + 1)
                startDoor(w, s, 0, false, def->speed);
        });
        break;
    case SpecialKind::Plat:
        onSectors([&](int s){ startPlat(w, s, def->speed, 105); });
        break;
    case SpecialKind::FloorRaise:
        onSectors([&](int s){
            startFloorOrCeiling(w, s, MoverKind::Floor, nextFloorUp(*w.map, s), def->speed);
        });
        break;
    case SpecialKind::FloorLower:
        onSectors([&](int s){
            startFloorOrCeiling(w, s, MoverKind::Floor, nextFloorDown(*w.map, s), def->speed);
        });
        break;
    case SpecialKind::Stairs:
        // Simplified (documented): one 8-unit step per tagged sector instead of vanilla's
        // iterative stair builder that keeps claiming neighbouring sectors.
        onSectors([&](int s){
            startFloorOrCeiling(w, s, MoverKind::Floor,
                                w.map->sectors[s].floorheight + 8, def->speed);
        });
        break;
    case SpecialKind::CeilingRaiseToNeighbours:
        onSectors([&](int s){
            Neighbors n = neighborHeights(*w.map, s);
            startFloorOrCeiling(w, s, MoverKind::Ceiling, n.ceil - 4, def->speed);
        });
        break;
    case SpecialKind::CeilingLowerToFloor:
        onSectors([&](int s){
            startFloorOrCeiling(w, s, MoverKind::Ceiling,
                                w.map->sectors[s].floorheight, def->speed);
        });
        break;
    case SpecialKind::TeleportTag:
        if (who) teleportPlayer(w, *who, L.tag);
        break;
    case SpecialKind::TeleportOther:
        if (who) teleportOtherSide(w, L, *who);
        break;
    case SpecialKind::ExitNormal:
        w.levelExitRequested = true; w.secretExit = false;
        break;
    case SpecialKind::ExitSecret:
        w.levelExitRequested = true; w.secretExit = true;
        break;
    case SpecialKind::None:
        break;
    }
}
void P_UseLines(World& w, player_t& p) {
    if (!w.map || !p.mo || p.dead) return;
    P_UseLines(w, *p.mo);
}

void P_UseLines(World& w, mobj_t& mo) {
    if (!w.map) return;
    const int fi = static_cast<int>((mo.angle >> ANGLETOFINESHIFT) & FINEMASK);
    const double ox = static_cast<double>(mo.x) / FRACUNIT;
    const double oy = static_cast<double>(mo.y) / FRACUNIT;
    const double dx = static_cast<double>(finecosine[fi]) / FRACUNIT;
    const double dy = static_cast<double>(finesine[fi]) / FRACUNIT;
    constexpr double USE_RANGE = 64.0;

    int best = -1;
    double bestT = USE_RANGE;
    const MapData& m = *w.map;
    for (int i = 0; i < static_cast<int>(m.lines.size()); ++i) {
        const line_t& L = m.lines[i];
        if (!(triggerFor(L.special) & 1)) continue;
        if (L.v1 < 0 || L.v2 < 0 || L.v1 >= static_cast<int>(m.vertices.size()) ||
            L.v2 >= static_cast<int>(m.vertices.size())) continue;
        const double ax = static_cast<double>(m.vertices[L.v1].x) / FRACUNIT;
        const double ay = static_cast<double>(m.vertices[L.v1].y) / FRACUNIT;
        const double bx = static_cast<double>(m.vertices[L.v2].x) / FRACUNIT;
        const double by = static_cast<double>(m.vertices[L.v2].y) / FRACUNIT;
        const double sx = bx - ax, sy = by - ay;
        const double denom = dx * sy - dy * sx;
        if (std::fabs(denom) < 1e-9) continue;
        const double t = ((ax - ox) * sy - (ay - oy) * sx) / denom;
        const double u = ((ax - ox) * dy - (ay - oy) * dx) / denom;
        if (t <= 0.0 || t > bestT || u < 0.0 || u > 1.0) continue;
        bestT = t;
        best = i;
    }
    if (best >= 0) P_ActivateLine(w, m.lines[best], &mo);
}

void P_CrossSpecialLines(World& w, mobj_t& mo, fixed_t oldx, fixed_t oldy) {
    if (!w.map || &mo != w.player.mo) return;
    const double ox = static_cast<double>(oldx) / FRACUNIT;
    const double oy = static_cast<double>(oldy) / FRACUNIT;
    const double nx = static_cast<double>(mo.x) / FRACUNIT;
    const double ny = static_cast<double>(mo.y) / FRACUNIT;
    if (ox == nx && oy == ny) return;
    const MapData& m = *w.map;
    for (int i = 0; i < static_cast<int>(m.lines.size()); ++i) {
        const line_t& L = m.lines[i];
        if (!(triggerFor(L.special) & 2)) continue;
        if (L.v1 < 0 || L.v2 < 0 || L.v1 >= static_cast<int>(m.vertices.size()) ||
            L.v2 >= static_cast<int>(m.vertices.size())) continue;
        const double ax = static_cast<double>(m.vertices[L.v1].x) / FRACUNIT;
        const double ay = static_cast<double>(m.vertices[L.v1].y) / FRACUNIT;
        const double bx = static_cast<double>(m.vertices[L.v2].x) / FRACUNIT;
        const double by = static_cast<double>(m.vertices[L.v2].y) / FRACUNIT;
        const double rx = nx - ox, ry = ny - oy;
        const double sx = bx - ax, sy = by - ay;
        const double denom = rx * sy - ry * sx;
        if (std::fabs(denom) < 1e-9) continue;
        const double t = ((ax - ox) * sy - (ay - oy) * sx) / denom;
        const double u = ((ax - ox) * ry - (ay - oy) * rx) / denom;
        if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) continue;
        P_ActivateLine(w, L, &mo);
    }
}
