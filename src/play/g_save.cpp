#include "play/g_save.h"
#include "play/p_tick.h"
#include "play/p_mobj.h"
#include "play/info.h"
#include "play/p_setup.h"
#include "play/p_spec.h"   // P_InitLights
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr char MAGIC[4] = { 'D', 'C', 'P', 'S' };
constexpr std::int32_t VERSION = 3;   // v3 adds the power-up timers

template <typename T>
bool wr(std::ofstream& f, const T& v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(T));
    return static_cast<bool>(f);
}
template <typename T>
bool rd(std::ifstream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(f);
}

bool writeString(std::ofstream& f, const std::string& s) {
    const std::int32_t n = static_cast<std::int32_t>(s.size());
    if (!wr(f, n)) return false;
    f.write(s.data(), n);
    return static_cast<bool>(f);
}
bool readString(std::ifstream& f, std::string& s) {
    std::int32_t n = 0;
    if (!rd(f, n) || n < 0 || n > 4096) return false;
    s.resize(static_cast<size_t>(n));
    if (n) f.read(s.data(), n);
    return static_cast<bool>(f);
}

int mobjTypeIndex(const mobj_t& mo) {
    if (!mo.info) return -1;
    const auto idx = mo.info - mobjinfo;
    return (idx >= 0 && idx < NUMMOBJTYPES) ? static_cast<int>(idx) : -1;
}

}  // namespace

bool G_SaveGame(const World& w, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(MAGIC, sizeof(MAGIC));
    if (!wr(f, VERSION)) return false;
    if (!writeString(f, w.mapName)) return false;
    if (!wr(f, w.tic) || !wr(f, w.totalKills) || !wr(f, w.totalItems) ||
        !wr(f, w.totalSecrets)) return false;

    // Sector heights (doors/lifts may be mid-move).
    const MapData* map = w.map;
    if (!map) return false;
    const std::int32_t nsec = static_cast<std::int32_t>(map->sectors.size());
    if (!wr(f, nsec)) return false;
    for (size_t i = 0; i < map->sectors.size(); ++i) {
        const sector_t& s = map->sectors[i];
        const char found = i < w.secretFound.size() ? w.secretFound[i] : 0;
        if (!wr(f, s.floorheight) || !wr(f, s.ceilingheight) || !wr(f, found)) return false;
    }

    // Movers.
    const std::int32_t nmv = static_cast<std::int32_t>(w.movers.size());
    if (!wr(f, nmv)) return false;
    for (const Mover& mv : w.movers) {
        const std::int32_t kind = static_cast<int>(mv.kind);
        if (!wr(f, kind) || !wr(f, mv.sector) || !wr(f, mv.top) || !wr(f, mv.bottom) ||
            !wr(f, mv.speed) || !wr(f, mv.waitTics) || !wr(f, mv.state) ||
            !wr(f, mv.stayOpen)) return false;
    }

    // Player scalars.
    const player_t& p = w.player;
    if (!wr(f, p.health) || !wr(f, p.viewz)) return false;
    for (int i = 0; i < 2; ++i) if (!wr(f, p.armorpoints[i])) return false;
    if (!wr(f, p.armortype)) return false;
    for (int i = 0; i < NUMAMMO; ++i)
        if (!wr(f, p.ammo[i]) || !wr(f, p.maxammo[i])) return false;
    const std::uint32_t weapons = p.weapons;
    if (!wr(f, weapons) || !wr(f, p.readyweapon) || !wr(f, p.keys) ||
        !wr(f, p.killcount) || !wr(f, p.itemcount) || !wr(f, p.secretsFound) ||
        !wr(f, p.dead) || !wr(f, p.damagecount) || !wr(f, p.bonuscount)) return false;
    for (int i = 0; i < NUMPOWERS; ++i)
        if (!wr(f, p.powers[i])) return false;      // P8b power-ups
    for (int i = 0; i < 2; ++i) {
        const std::int32_t st = p.psprites[i].state ? static_cast<std::int32_t>(p.psprites[i].state - states)
                                                    : -1;
        if (!wr(f, st) || !wr(f, p.psprites[i].tics) ||
            !wr(f, p.psprites[i].sx) || !wr(f, p.psprites[i].sy)) return false;
    }

    // Mobjs (in list order) with target indices.
    std::vector<const mobj_t*> order;
    const mobj_t* playerMo = w.player.mo;
    std::int32_t playerIdx = -1;
    for (const mobj_t& mo : w.mobjs) {
        if (mo.removePending) continue;
        if (&mo == playerMo) playerIdx = static_cast<std::int32_t>(order.size());
        order.push_back(&mo);
    }
    if (!wr(f, playerIdx)) return false;
    const std::int32_t nmo = static_cast<std::int32_t>(order.size());
    if (!wr(f, nmo)) return false;
    for (const mobj_t* mo : order) {
        auto indexOf = [&](const mobj_t* t) -> std::int32_t {
            if (!t) return -1;
            for (size_t i = 0; i < order.size(); ++i) if (order[i] == t) return static_cast<std::int32_t>(i);
            return -1;
        };
        const std::int32_t type = mobjTypeIndex(*mo);
        const std::int32_t state = mo->state ? static_cast<std::int32_t>(mo->state - states) : 0;
        const std::int32_t target = indexOf(mo->target);
        const std::int32_t flags = mo->flags;
        if (!wr(f, type) || !wr(f, mo->x) || !wr(f, mo->y) || !wr(f, mo->z) ||
            !wr(f, mo->momx) || !wr(f, mo->momy) || !wr(f, mo->momz) ||
            !wr(f, mo->angle) || !wr(f, mo->height) || !wr(f, mo->health) ||
            !wr(f, flags) || !wr(f, mo->tics) || !wr(f, state) ||
            !wr(f, mo->reactiontime) || !wr(f, target) ||
            !wr(f, mo->floorz) || !wr(f, mo->ceilingz) || !wr(f, mo->light) ||
            !wr(f, mo->fullbright)) return false;
    }
    return static_cast<bool>(f);
}

bool G_LoadGame(World& w, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4] = {};
    f.read(magic, sizeof(magic));
    if (!f || magic[0] != MAGIC[0] || magic[1] != MAGIC[1] ||
        magic[2] != MAGIC[2] || magic[3] != MAGIC[3]) return false;
    std::int32_t version = 0;
    if (!rd(f, version) || version != VERSION) return false;
    std::string mapName;
    if (!readString(f, mapName)) return false;
    if (!w.map || mapName != w.mapName) return false;   // main must reload the map first
    std::uint32_t tic = 0;
    std::int32_t totalKills = 0, totalItems = 0, totalSecrets = 0;
    if (!rd(f, tic) || !rd(f, totalKills) || !rd(f, totalItems) ||
        !rd(f, totalSecrets)) return false;
    w.tic = tic;
    w.totalKills = totalKills;
    w.totalItems = totalItems;
    w.totalSecrets = totalSecrets;

    std::int32_t nsec = 0;
    if (!rd(f, nsec) || nsec != static_cast<std::int32_t>(w.map->sectors.size())) return false;
    w.secretFound.assign(w.map->sectors.size(), 0);
    for (size_t i = 0; i < w.map->sectors.size(); ++i) {
        sector_t& s = w.map->sectors[i];
        char found = 0;
        if (!rd(f, s.floorheight) || !rd(f, s.ceilingheight) || !rd(f, found)) return false;
        w.secretFound[i] = found;
    }

    std::int32_t nmv = 0;
    if (!rd(f, nmv) || nmv < 0) return false;
    w.movers.clear();
    for (std::int32_t i = 0; i < nmv; ++i) {
        Mover mv;
        std::int32_t kind = 0;
        if (!rd(f, kind) || !rd(f, mv.sector) || !rd(f, mv.top) || !rd(f, mv.bottom) ||
            !rd(f, mv.speed) || !rd(f, mv.waitTics) || !rd(f, mv.state) ||
            !rd(f, mv.stayOpen)) return false;
        mv.kind = static_cast<MoverKind>(kind);
        w.movers.push_back(mv);
    }

    player_t p{};
    if (!rd(f, p.health) || !rd(f, p.viewz)) return false;
    for (int i = 0; i < 2; ++i) if (!rd(f, p.armorpoints[i])) return false;
    if (!rd(f, p.armortype)) return false;
    for (int i = 0; i < NUMAMMO; ++i)
        if (!rd(f, p.ammo[i]) || !rd(f, p.maxammo[i])) return false;
    std::uint32_t weapons = 0;
    if (!rd(f, weapons) || !rd(f, p.readyweapon) || !rd(f, p.keys) ||
        !rd(f, p.killcount) || !rd(f, p.itemcount) || !rd(f, p.secretsFound) ||
        !rd(f, p.dead) || !rd(f, p.damagecount) || !rd(f, p.bonuscount)) return false;
    for (int i = 0; i < NUMPOWERS; ++i)
        if (!rd(f, p.powers[i])) return false;      // P8b power-ups
    p.weapons = weapons;
    std::int32_t psState[2] = {};
    if (!rd(f, psState[0]) || !rd(f, p.psprites[0].tics) ||
        !rd(f, p.psprites[0].sx) || !rd(f, p.psprites[0].sy) ||
        !rd(f, psState[1]) || !rd(f, p.psprites[1].tics) ||
        !rd(f, p.psprites[1].sx) || !rd(f, p.psprites[1].sy)) return false;

    std::int32_t playerIdx = -1;
    std::int32_t nmo = 0;
    if (!rd(f, playerIdx) || !rd(f, nmo) || nmo < 0 || nmo > 100000) return false;

    w.mobjs.clear();
    w.player.mo = nullptr;
    std::vector<mobj_t*> revived;
    revived.reserve(static_cast<size_t>(nmo));
    std::vector<std::int32_t> targets(static_cast<size_t>(nmo), -1);
    for (std::int32_t i = 0; i < nmo; ++i) {
        std::int32_t type = -1, state = 0, flags = 0, target = -1, reaction = 0;
        fixed_t x = 0, y = 0, z = 0, momx = 0, momy = 0, momz = 0;
        angle_t angle = 0;
        fixed_t height = 0, floorz = 0, ceilingz = 0;
        int health = 0, tics = -1, light = 160;
        bool fullbright = false;
        if (!rd(f, type) || !rd(f, x) || !rd(f, y) || !rd(f, z) ||
            !rd(f, momx) || !rd(f, momy) || !rd(f, momz) || !rd(f, angle) ||
            !rd(f, height) || !rd(f, health) || !rd(f, flags) || !rd(f, tics) ||
            !rd(f, state) || !rd(f, reaction) || !rd(f, target) ||
            !rd(f, floorz) || !rd(f, ceilingz) || !rd(f, light) ||
            !rd(f, fullbright)) return false;
        if (type < 0 || type >= NUMMOBJTYPES) return false;
        mobj_t* mo = P_SpawnMobj(w, x, y, z, static_cast<mobjtype_t>(type));
        mo->x = x; mo->y = y; mo->z = z;
        mo->momx = momx; mo->momy = momy; mo->momz = momz;
        mo->angle = angle;
        mo->height = height;
        mo->health = health;
        mo->flags = flags;
        mo->reactiontime = reaction;
        mo->floorz = floorz;
        mo->ceilingz = ceilingz;
        mo->light = light;
        mo->fullbright = fullbright;
        P_SetMobjState(*mo, static_cast<statenum_t>(state));   // refresh render cache only
        mo->tics = tics;
        revived.push_back(mo);
        targets[static_cast<size_t>(i)] = target;
    }
    for (size_t i = 0; i < revived.size(); ++i) {
        const std::int32_t ti = targets[i];
        revived[i]->target = (ti >= 0 && ti < static_cast<std::int32_t>(revived.size()))
                                 ? revived[static_cast<size_t>(ti)] : nullptr;
    }
    if (playerIdx < 0 || playerIdx >= static_cast<std::int32_t>(revived.size())) return false;
    p.mo = revived[static_cast<size_t>(playerIdx)];
    for (int i = 0; i < 2; ++i) {
        if (psState[i] >= 0 && psState[i] < NUMSTATES) {
            p.psprites[i].state = &states[psState[i]];
        } else {
            p.psprites[i].state = nullptr;
        }
    }
    w.player = p;
    P_InitLights(w);   // P8b: rebuild the animated-light list from the sector types
    return true;
}

bool G_SaveGameLevel(const std::string& path, std::string& mapName) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4] = {};
    f.read(magic, sizeof(magic));
    if (!f || magic[0] != MAGIC[0] || magic[1] != MAGIC[1] ||
        magic[2] != MAGIC[2] || magic[3] != MAGIC[3]) return false;
    std::int32_t version = 0;
    if (!rd(f, version) || version != VERSION) return false;
    return readString(f, mapName);
}
