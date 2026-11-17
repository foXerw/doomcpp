#pragma once
#include "core/doomtype.h"
#include <map>
#include <utility>
#include <vector>

struct MapData;

// P9: is a level actually finishable with the specials this engine implements?
//
// The answer has to come from the engine's own rules, not from a hand-written list of
// special numbers (that list is what p_spec's table already owns). So this walks the map's
// sectors the way the player could: geometrically walkable openings, openings a *supported*
// special can open, sectors a supported lift/plat can reach, teleport destinations a
// supported teleport can jump to, and locked doors once the key is somewhere reachable.
//
// What it reports back is the useful part: which lines block the way, and what special
// number they carry. A blocked line with an unsupported special is a feature to implement;
// a blocked line with no special at all is the map's own design (or a monster to kill).
struct ReachResult {
    int sectors = 0;
    int reached = 0;
    bool exitReachable = false;
    int exitLines = 0;
    std::vector<char> reachable;                 // per sector
    std::map<int, int> gating;                   // unsupported special -> blocking edges
    std::vector<std::pair<int, int>> gateSpots;  // example line midpoints that gate progress
    std::vector<std::pair<int, int>> unreachableExits;
};

ReachResult P_Reachability(const MapData& m);
