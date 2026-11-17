#include "p_reach.h"
#include "p_setup.h"
#include "p_maputl.h"
#include "p_spec.h"
#include "info.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr int PLAYER_HEIGHT = 56;      // mobjinfo[MT_PLAYER].height in map units
constexpr int STEP_LIMIT_UNITS = 24;   // p_map.h STEP_LIMIT

int sectorOfSide(const MapData& m, int side) {
    if (side < 0 || side >= static_cast<int>(m.sides.size())) return -1;
    const int s = m.sides[side].sector;
    return (s >= 0 && s < static_cast<int>(m.sectors.size())) ? s : -1;
}

// Does the player fit through this line as the map was authored?
bool walkable(const MapData& m, int a, int b) {
    const sector_t& A = m.sectors[a];
    const sector_t& B = m.sectors[b];
    const int top = std::min(A.ceilingheight, B.ceilingheight);
    const int bottom = std::max(A.floorheight, B.floorheight);
    if (top - bottom < PLAYER_HEIGHT) return false;
    if (std::abs(A.floorheight - B.floorheight) > STEP_LIMIT_UNITS) return false;
    return true;
}

// A sector's floor is not one number: if some supported mover can act on it (a lift, a
// plat, a rising floor), the floor can travel anywhere between where it is and just under
// the ceiling. Modelling that is what lets the walk ride a lift between two areas whose
// static floors are too far apart to step -- without it, every lift in the game looks like
// a wall and the audit reports maps as unfinishable that the engine can finish fine.
struct FloorRange { int lo = 0, hi = 0; };

FloorRange rangeOf(const MapData& m, int s, const std::vector<char>& operable) {
    const sector_t& S = m.sectors[s];
    if (!operable[s]) return {S.floorheight, S.floorheight};
    const int top = S.ceilingheight - PLAYER_HEIGHT;
    return {S.floorheight, std::max(S.floorheight, top)};
}

// Is there a pair of reachable floor heights, one per sector, that the player can step
// between and still fit through?
bool crossable(const MapData& m, int a, int b, const std::vector<char>& operable) {
    const FloorRange A = rangeOf(m, a, operable), B = rangeOf(m, b, operable);
    const int top = std::min(m.sectors[a].ceilingheight, m.sectors[b].ceilingheight);
    const int cand[4] = {A.lo, A.hi, B.lo, B.hi};
    for (int ha : cand) {
        if (ha < A.lo || ha > A.hi) continue;
        for (int hb : cand) {
            if (hb < B.lo || hb > B.hi) continue;
            if (std::abs(ha - hb) > STEP_LIMIT_UNITS) continue;
            if (top - std::max(ha, hb) < PLAYER_HEIGHT) continue;
            return true;
        }
    }
    return false;
}

// Can a *supported* special on this line, or on some line tagged to operate one of these
// two sectors, make the gap passable?
bool specialOpens(const MapData& m, int a, int b, int lineSpecial) {
    if (lineSpecial) {
        const SpecialDef* d = P_LookupSpecial(lineSpecial);
        if (d) {
            switch (d->kind) {
            case SpecialKind::DoorOpenClose:
            case SpecialKind::DoorOpenStay:
            case SpecialKind::Plat:
            case SpecialKind::FloorRaise:
            case SpecialKind::FloorLower:
            case SpecialKind::Stairs:
            case SpecialKind::CeilingRaiseToNeighbours:
            case SpecialKind::CeilingLowerToFloor:
                return true;
            default:
                break;
            }
        }
    }
    // Remote operation: any supported mover line carrying the tag of one of these sectors.
    const int tags[2] = { m.sectors[a].tag, m.sectors[b].tag };
    for (int t = 0; t < 2; ++t) {
        if (tags[t] <= 0) continue;
        for (const line_t& L : m.lines) {
            if (L.tag != tags[t] || L.special == 0) continue;
            const SpecialDef* d = P_LookupSpecial(L.special);
            if (!d) continue;
            switch (d->kind) {
            case SpecialKind::DoorOpenClose:
            case SpecialKind::DoorOpenStay:
            case SpecialKind::Plat:
            case SpecialKind::FloorRaise:
            case SpecialKind::FloorLower:
            case SpecialKind::Stairs:
            case SpecialKind::CeilingRaiseToNeighbours:
            case SpecialKind::CeilingLowerToFloor:
                return true;
            default:
                break;
            }
        }
    }
    return false;
}

bool isDoorKind(SpecialKind k) {
    return k == SpecialKind::DoorOpenClose || k == SpecialKind::DoorOpenStay;
}

}  // namespace

ReachResult P_Reachability(const MapData& m) {
    ReachResult r;
    const int n = static_cast<int>(m.sectors.size());
    r.sectors = n;
    r.reachable.assign(n, 0);

    fixed_t sx, sy; angle_t sang;
    if (!playerStart(m, sx, sy, sang)) return r;
    const int start = sectorOf(m, R_PointInSubsector(m, sx, sy));
    if (start < 0 || start >= n) return r;
    r.reachable[start] = 1;
    r.reached = 1;

    // Which keys are available: a key thing that sits in a reachable sector. Recomputed on
    // every pass, because reaching a key is what unlocks the next batch of doors.
    std::vector<int> keyTypes = {5, 6, 13};
    std::vector<int> keyTypeOfThing, keySectorOfThing;   // parallel, one entry per key thing
    for (const thing_t& th : m.things) {
        if (th.type != 5 && th.type != 6 && th.type != 13) continue;
        keyTypeOfThing.push_back(th.type);
        keySectorOfThing.push_back(sectorOf(m, R_PointInSubsector(
            m, static_cast<fixed_t>(th.x) << FRACBITS,
               static_cast<fixed_t>(th.y) << FRACBITS)));
    }

    // Teleport destinations by tag, for the supported tag teleports.
    auto taggedSectors = [&](int tag) {
        std::vector<int> out;
        if (tag <= 0) return out;
        for (const thing_t& th : m.things) {
            if (th.type != 14) continue;
            const int s = sectorOf(m, R_PointInSubsector(
                m, static_cast<fixed_t>(th.x) << FRACBITS,
                   static_cast<fixed_t>(th.y) << FRACBITS));
            if (s >= 0 && s < n && m.sectors[s].tag == tag) out.push_back(s);
        }
        return out;
    };

    // Which sectors the engine could ever move, from any line in the map: a tag-matched
    // mover line, or a tag-0 door/plat/floor line that has the sector on one of its sides
    // (the classic "switch right next to the door" and "the door line itself" shapes).
    // Without this the walk stops at every closed door whose opener carries no tag, which
    // is most of them.
    std::vector<char> operable(n, 0);
    for (const line_t& L : m.lines) {
        const SpecialDef* d = P_LookupSpecial(L.special);
        if (!d) continue;
        switch (d->kind) {
        case SpecialKind::DoorOpenClose:
        case SpecialKind::DoorOpenStay:
        case SpecialKind::DoorClose:
        case SpecialKind::Plat:
        case SpecialKind::FloorRaise:
        case SpecialKind::FloorLower:
        case SpecialKind::Stairs:
        case SpecialKind::CeilingRaiseToNeighbours:
        case SpecialKind::CeilingLowerToFloor:
            break;
        default:
            continue;
        }
        if (L.tag > 0)
            for (int sIdx = 0; sIdx < n; ++sIdx)
                if (m.sectors[sIdx].tag == L.tag) operable[sIdx] = 1;
        const int a = sectorOfSide(m, L.sidenum[0]);
        const int b = sectorOfSide(m, L.sidenum[1]);
        if (a >= 0) operable[a] = 1;
        if (b >= 0) operable[b] = 1;
    }

    bool changed = true;
    int guard = 0;
    while (changed && guard++ < 64) {
        changed = false;
        int keys = 0;
        for (size_t i = 0; i < keySectorOfThing.size(); ++i) {
            const int s = keySectorOfThing[i];
            if (s >= 0 && s < n && r.reachable[s])
                keys |= (keyTypeOfThing[i] == 5 ? KEY_BLUE
                          : keyTypeOfThing[i] == 6 ? KEY_YELLOW : KEY_RED);
        }

        for (size_t li = 0; li < m.lines.size(); ++li) {
            const line_t& L = m.lines[li];
            const int a = sectorOfSide(m, L.sidenum[0]);
            const int b = sectorOfSide(m, L.sidenum[1]);
            if (a < 0 || b < 0 || a == b) continue;              // one-sided: no crossing
            const int from = r.reachable[a] ? a : (r.reachable[b] ? b : -1);
            if (from < 0) continue;
            const int to = (from == a) ? b : a;
            if (r.reachable[to]) continue;

            const SpecialDef* d = P_LookupSpecial(L.special);
            const bool locked = d && d->key && !(keys & d->key);

            bool pass = false;
            if (!locked) {
                if (walkable(m, a, b)) pass = true;
                else if (crossable(m, a, b, operable)) pass = true;     // ride a lift across
                else if (operable[to] || operable[from]) pass = true;   // a door/lift can open it
                else if (d && d->kind == SpecialKind::TeleportOther) pass = true;
                else if (specialOpens(m, a, b, L.special)) pass = true;
            }

            // A supported teleport jumps regardless of the geometry in between.
            bool teleported = false;
            if (!locked && d && d->kind == SpecialKind::TeleportTag) {
                for (int s : taggedSectors(L.tag)) if (!r.reachable[s]) {
                    r.reachable[s] = 1; ++r.reached; changed = true; teleported = true;
                }
            }

            if (pass && !r.reachable[to]) {
                r.reachable[to] = 1;
                ++r.reached;
                changed = true;
            } else if (!pass && !r.reachable[to] && L.special && !d) {
                // This line is the frontier, and its special is something the engine cannot
                // do yet: exactly the list worth implementing.
                ++r.gating[L.special];
                if (r.gateSpots.size() < 40 && L.v1 >= 0 && L.v2 >= 0 &&
                    L.v1 < static_cast<int>(m.vertices.size()) &&
                    L.v2 < static_cast<int>(m.vertices.size()))
                    r.gateSpots.emplace_back(
                        (m.vertices[L.v1].x + m.vertices[L.v2].x) / 2 / FRACUNIT,
                        (m.vertices[L.v1].y + m.vertices[L.v2].y) / 2 / FRACUNIT);
            }
            (void)teleported;
        }
    }

    r.exitLines = 0;
    for (const line_t& L : m.lines) {
        if (!P_IsExitSpecial(L.special)) continue;
        ++r.exitLines;
        const int a = sectorOfSide(m, L.sidenum[0]);
        const int b = sectorOfSide(m, L.sidenum[1]);
        const bool ok = (a >= 0 && r.reachable[a]) || (b >= 0 && r.reachable[b]);
        if (ok) r.exitReachable = true;
        else if (a >= 0 && b >= 0 && L.v1 >= 0 && L.v2 >= 0 &&
                 L.v1 < static_cast<int>(m.vertices.size()) &&
                 L.v2 < static_cast<int>(m.vertices.size()))
            r.unreachableExits.emplace_back(
                (m.vertices[L.v1].x + m.vertices[L.v2].x) / 2 / FRACUNIT,
                (m.vertices[L.v1].y + m.vertices[L.v2].y) / 2 / FRACUNIT);
    }
    // Boss maps end by killing the boss, not by an exit line.
    for (const thing_t& th : m.things)
        if (th.type == 7 || th.type == 16) {          // spider / cyberdemon
            const int s = sectorOf(m, R_PointInSubsector(
                m, static_cast<fixed_t>(th.x) << FRACBITS,
                   static_cast<fixed_t>(th.y) << FRACBITS));
            if (s >= 0 && r.reachable[s]) r.exitReachable = true;
        }
    return r;
}
