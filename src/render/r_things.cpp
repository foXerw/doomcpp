#include "r_things.h"
#include "r_data.h"        // loadPalette, decodePatch
#include "wad/wadfile.h"   // WadFile (ctor)
#include <cctype>
#include <cmath>

SpriteLumpName parseSpriteLumpName(const std::string& name) {
    SpriteLumpName out;
    // sprname = first 4 chars, uppercased, NUL-terminated
    for (int i = 0; i < 4 && i < static_cast<int>(name.size()); ++i)
        out.sprname[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
    out.sprname[4] = 0;
    // primary install: frame = name[4], rotation = name[5]-'0'
    out.inst[0].frame = name.size() > 4 ? name[4] : 'A';
    out.inst[0].rotation = name.size() > 5 ? (name[5] - '0') : 0;
    out.inst[0].flip = false;
    out.nInst = 1;
    // mirrored second install: name[6] = frame, name[7] = rotation, flip = true
    if (name.size() > 7 && name[6] != ' ' && name[6] != '\0') {
        out.inst[1].frame = name[6];
        out.inst[1].rotation = name[7] - '0';
        out.inst[1].flip = true;
        out.nInst = 2;
    }
    return out;
}

// ---- SpriteCache (Task 4) ----
namespace {
std::string upperS(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

void SpriteCache::install(const std::string& lumpName, const Patch* patch) {
    SpriteLumpName pn = parseSpriteLumpName(lumpName);
    auto& arr = frames_[upperS(pn.sprname)];
    for (int i = 0; i < pn.nInst; ++i) {
        const LumpInstall& ins = pn.inst[i];
        int fi = ins.frame - 'A';
        if (fi < 0 || fi >= 29) continue;
        SpriteFrame& f = arr[fi];
        f.present = true;
        if (ins.rotation == 0) {                       // all angles, no flip
            f.rotate = false;
            f.lump[0] = patch;
            f.flip[0] = false;
        } else if (ins.rotation >= 1 && ins.rotation <= 8) {
            f.rotate = true;
            f.lump[ins.rotation - 1] = patch;
            f.flip[ins.rotation - 1] = ins.flip;
        }
    }
}

SpriteCache::View SpriteCache::resolve(const std::string& sprname, char frame, int rotIndex) const {
    auto it = frames_.find(upperS(sprname));
    if (it == frames_.end()) return {};                // absent sprite
    int fi = frame - 'A';
    if (fi < 0 || fi >= 29) return {};                 // out-of-range frame
    const SpriteFrame& f = it->second[fi];
    if (!f.present) return {};                         // absent frame
    int idx = f.rotate ? (rotIndex & 7) : 0;
    return {f.lump[idx], f.flip[idx]};
}

SpriteCache::SpriteCache(const WadFile& wad) {
    WadFile& w = const_cast<WadFile&>(wad);
    const auto pal = loadPalette(wad);

    // First pass: locate S_START / S_END lump indices (case-insensitive).
    int start = -1, end = -1;
    for (int i = 0; i < w.numLumps(); ++i) {
        std::string nm = upperS(w.lumpName(i));
        if (nm == "S_START") start = i;
        else if (nm == "S_END") { end = i; break; }
    }
    if (start < 0 || end < 0) return;                  // no sprite block

    // Decode every non-empty sprite lump into owned_, remembering each lump's
    // directory index so empty lumps don't desync the name->patch mapping.
    std::vector<int> lumpIdx;
    for (int i = start + 1; i < end; ++i) {
        auto raw = w.readLump(i);
        if (raw.empty()) continue;
        owned_.push_back(decodePatch(raw.data(), raw.size(), pal.data()));
        lumpIdx.push_back(i);
    }
    // Install pointers ONLY after owned_ is fully populated: push_back may
    // realloc, so installing mid-loop would dangle the stored const Patch*.
    for (size_t i = 0; i < owned_.size(); ++i) {
        install(w.lumpName(lumpIdx[i]), &owned_[i]);
    }
}

// ---- spriteRotationIndex (Task 5) ----
// Float equivalent of vanilla `rot = (ang - thing->angle + (ANG45/2)*9) >> 29`.
// (ANG45/2)*9 = 202.5 deg = 9*pi/8 shifts the bin grid so that relAng == 0
// (player directly behind the thing's facing) lands in the centre of bin 4.
int spriteRotationIndex(float relAngRad) {
    constexpr float PI = 3.141592653589793f;
    constexpr float TWO_PI = 2.0f * PI;
    constexpr float OFFSET = 9.0f * PI / 8.0f;   // (ANG45/2)*9 = 202.5 deg
    float a = std::fmod(relAngRad + OFFSET, TWO_PI);
    if (a < 0.0f) a += TWO_PI;                   // fmod may be negative
    int r = static_cast<int>(a / (TWO_PI / 8.0f));
    if (r < 0) r = 0;                            // clamp at boundaries
    if (r > 7) r = 7;
    return r;
}
