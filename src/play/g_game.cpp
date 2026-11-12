#include "g_game.h"
#include <cstdio>

namespace {
// Parse "E<ep>M<map>"; returns false for anything else.
bool parseLevel(const std::string& s, int& ep, int& map) {
    if (s.size() < 4 || s[0] != 'E' || s[2] != 'M') return false;
    if (s[1] < '1' || s[1] > '4') return false;
    if (s[3] < '1' || s[3] > '9') return false;
    ep = s[1] - '0';
    map = s[3] - '0';
    return true;
}

std::string makeLevel(int ep, int map) {
    std::string s = "E#M#";
    s[1] = static_cast<char>('0' + ep);
    s[3] = static_cast<char>('0' + map);
    return s;
}
}  // namespace

std::string G_NextLevelName(const std::string& cur, bool secretExit) {
    int ep = 0, map = 0;
    if (!parseLevel(cur, ep, map)) return "";
    if (secretExit && map != 9 && map != 8) return makeLevel(ep, 9);
    if (map == 9) return makeLevel(ep, 4);            // secret level returns to M4
    if (map < 8) return makeLevel(ep, map + 1);
    if (ep < 4) return makeLevel(ep + 1, 1);          // E1M8 -> E2M1
    return "";                                        // E4M8 -> victory
}

bool G_IsCampaignEnd(const std::string& cur) {
    return cur == "E4M8";
}

std::string G_LevelLabel(const std::string& mapName) {
    return mapName;
}
