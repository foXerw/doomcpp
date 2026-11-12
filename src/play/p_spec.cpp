#include "p_spec.h"
#include "p_setup.h"
#include "p_maputl.h"
#include "info.h"
#include "core/m_tables.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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
}

void startFloorOrCeiling(World& w, int sec, MoverKind kind, int target, int speed) {
    if (!w.map || sec < 0 || sec >= static_cast<int>(w.map->sectors.size())) return;
    Mover* mv = findMover(w, sec, kind);
    if (!mv) { w.movers.push_back(Mover{}); mv = &w.movers.back(); }
    mv->kind = kind; mv->sector = sec;
    mv->top = target; mv->bottom = target; mv->speed = speed;
    mv->waitTics = 0; mv->stayOpen = true; mv->state = 0;
}

template <typename Fn>
void forTaggedSectors(const MapData& m, int tag, Fn fn) {
    if (tag <= 0) return;
    for (int i = 0; i < static_cast<int>(m.sectors.size()); ++i)
        if (m.sectors[i].tag == tag) fn(i);
}

int keyForSpecial(int special) {
    switch (special) {
    case 26: case 32: case 133: return KEY_BLUE;
    case 27: case 33: case 137: return KEY_YELLOW;
    case 28: case 34: case 135: return KEY_RED;
    default: return 0;
    }
}

// 1 = use (Space), 2 = walk-over trigger.
int triggerFor(int special) {
    switch (special) {
    case 1: case 26: case 27: case 28: case 31: case 32: case 33: case 34:
    case 62: case 63: case 64: case 90: case 91: case 103:
    case 117: case 118: case 133: case 135: case 137:
    case 11: case 51:
    case 23: case 55: case 66: case 67: case 69: case 70: case 71: case 72:
    case 101: case 102: case 41:
        return 1;
    case 2: case 3: case 4: case 7: case 8: case 19: case 38:
    case 39: case 40: case 43: case 44: case 52:
    case 58: case 82: case 88: case 92: case 97:
    case 100: case 119: case 120: case 127: case 130: case 131:
        return 2;
    default:
        return 0;
    }
}

void swapSwitchTextures(MapData& m, const line_t& L) {
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
        return;   // only the first matching side
    }
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
        const int target = (mv.state == 0) ? mv.top : mv.bottom;
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

void P_ActivateLine(World& w, const line_t& L, mobj_t* who) {
    if (!w.map) return;
    const int key = keyForSpecial(L.special);
    if (key && who == w.player.mo && !(w.player.keys & key)) return;   // locked

    if (triggerFor(L.special) == 1) swapSwitchTextures(*w.map, const_cast<line_t&>(L));

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
    auto doorAt = [&](int sec, bool stay, int speed) {
        startDoor(w, sec, 150, stay, speed);
    };

    switch (L.special) {
    // ---- doors ----
    case 1: case 4: case 26: case 27: case 28: case 117: {
        if (doorSec >= 0) doorAt(doorSec, false, L.special == 117 ? 4 : 2);
        else forTaggedSectors(*w.map, L.tag, [&](int s){ doorAt(s, false, 2); });
        break;
    }
    case 2: {   // W1 door open (stay; already-open doors stay open)
        auto openStay = [&](int sec) {
            const sector_t& s = w.map->sectors[sec];
            if (s.ceilingheight <= s.floorheight + 1) startDoor(w, sec, 0, true, 2);
        };
        if (doorSec >= 0) openStay(doorSec);
        else forTaggedSectors(*w.map, L.tag, openStay);
        break;
    }
    case 3: {   // W1 door close
        auto closeAt = [&](int sec) {
            if (w.map->sectors[sec].ceilingheight > w.map->sectors[sec].floorheight + 1)
                startDoor(w, sec, 0, false, 2);
        };
        if (doorSec >= 0) closeAt(doorSec);
        else forTaggedSectors(*w.map, L.tag, closeAt);
        break;
    }
    case 31: case 32: case 33: case 34: case 118: case 63: case 64: case 91: case 103: {
        auto openStay = [&](int sec) {
            const sector_t& s = w.map->sectors[sec];
            if (s.ceilingheight <= s.floorheight + 1)
                startDoor(w, sec, 0, true, L.special == 118 ? 4 : 2);
        };
        if (doorSec >= 0) openStay(doorSec);
        else forTaggedSectors(*w.map, L.tag, openStay);
        break;
    }
    case 90: {
        if (doorSec >= 0) doorAt(doorSec, false, 2);
        else forTaggedSectors(*w.map, L.tag, [&](int s){ doorAt(s, false, 2); });
        break;
    }
    // ---- lifts / platforms ----
    case 62: case 88: case 120: {
        forTaggedSectors(*w.map, L.tag, [&](int s){ startPlat(w, s, 4, 105); });
        break;
    }
    // ---- floors (raise to lowest/highest neighbouring floor) ----
    case 58: case 92: case 119: case 130: case 131: case 66: case 71:
    case 55: case 101: case 102: {
        forTaggedSectors(*w.map, L.tag, [&](int s) {
            int speed = (L.special == 119 || L.special == 130 || L.special == 131) ? 8 : 4;
            startFloorOrCeiling(w, s, MoverKind::Floor, nextFloorUp(*w.map, s), speed);
        });
        break;
    }
    // ---- floors (lower to the next floor below) ----
    case 23: case 82: case 38: case 19: case 67: {
        forTaggedSectors(*w.map, L.tag, [&](int s) {
            startFloorOrCeiling(w, s, MoverKind::Floor, nextFloorDown(*w.map, s), 4);
        });
        break;
    }
    // ---- stairs (simplified: raise each tagged sector by 8) ----
    case 7: case 8: case 100: case 127: {
        forTaggedSectors(*w.map, L.tag, [&](int s) {
            startFloorOrCeiling(w, s, MoverKind::Floor, w.map->sectors[s].floorheight + 8, 4);
        });
        break;
    }
    // ---- ceilings ----
    case 40: case 69: case 72: {
        forTaggedSectors(*w.map, L.tag, [&](int s) {
            Neighbors n = neighborHeights(*w.map, s);
            startFloorOrCeiling(w, s, MoverKind::Ceiling, n.ceil - 4, 4);
        });
        break;
    }
    case 41: case 43: case 70: {
        forTaggedSectors(*w.map, L.tag, [&](int s) {
            startFloorOrCeiling(w, s, MoverKind::Ceiling, w.map->sectors[s].floorheight, 4);
        });
        break;
    }
    case 44: {
        forTaggedSectors(*w.map, L.tag, [&](int s) {
            startFloorOrCeiling(w, s, MoverKind::Ceiling, w.map->sectors[s].floorheight, 4);
        });
        break;
    }
    // ---- teleport / exits ----
    case 39: case 97:
        if (who) teleportPlayer(w, *who, L.tag);
        break;
    case 11: case 51: case 52:
        break;   // level exit: P7
    default:
        break;
    }
}

void P_UseLines(World& w, player_t& p) {
    if (!w.map || !p.mo || p.dead) return;
    mobj_t& mo = *p.mo;
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
        if (triggerFor(L.special) != 1) continue;
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
        if (triggerFor(L.special) != 2) continue;
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
