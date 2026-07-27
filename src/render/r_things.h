#pragma once
#include "core/doomtype.h"
#include "render/r_data.h"   // Patch
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
class WadFile;   // fwd

// ---- Sprite lump-name parse (Task 3) ----
struct LumpInstall { char frame = 0; int rotation = 0; bool flip = false; };  // rotation 0 == all 8
struct SpriteLumpName { char sprname[5] = {0}; LumpInstall inst[2]; int nInst = 0; };
SpriteLumpName parseSpriteLumpName(const std::string& name);

// ---- Sprite frame cache (Task 4) ----
// Indexes decoded sprite patches by sprname -> frame('A'..'Z') -> rotation(1..8),
// mirroring vanilla spriteframe_t. rotation 0 installs a single lump for all angles.
struct SpriteFrame {
    bool rotate = false;            // false: single (rot0) lump[0]; true: 8-way
    const Patch* lump[8] = {};      // index 0..7 == rotation 1..8
    bool flip[8] = {};
    bool present = false;
};

class SpriteCache {
public:
    SpriteCache() = default;                    // empty cache (unit-test path)
    explicit SpriteCache(const WadFile& wad);   // decode S_START..S_END into owned_
    // Index an externally-owned patch (storage managed by caller/ctor).
    void install(const std::string& lumpName, const Patch* patch);
    struct View { const Patch* patch = nullptr; bool flip = false; };
    // rotIndex 0..7. For non-rotate frames rotIndex is ignored.
    View resolve(const std::string& sprname, char frame, int rotIndex) const;
private:
    std::unordered_map<std::string, std::array<SpriteFrame, 29>> frames_;  // sprname -> [frame-'A']
    std::vector<Patch> owned_;   // decoded patches (ctor path); stable after ctor returns
};
