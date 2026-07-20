#include "r_data.h"
#include "core/i_system.h"
#include "wad/wadfile.h"
#include <cctype>
#include <cstring>

namespace {
std::int16_t rd_i16(const byte* p) {
    return static_cast<std::int16_t>(
        static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}
std::int32_t rd_i32(const byte* p) {
    return static_cast<std::int32_t>(
        static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
}
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}

std::vector<std::string> parsePnames(const byte* d, size_t n) {
    if (n < 4) I_Error("parsePnames: too small");
    int count = rd_i32(d);
    if (count < 0) I_Error("parsePnames: negative count");
    std::vector<std::string> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const byte* p = d + 4 + i * 8;
        if (p + 8 > d + n) I_Error("parsePnames: truncated");
        char name[9];
        std::memcpy(name, p, 8);
        name[8] = '\0';
        for (int k = 7; k >= 0 && name[k] == ' '; --k) name[k] = '\0';
        out.emplace_back(name);
    }
    return out;
}

Patch decodePatch(const byte* data, size_t n, const uint32_t* palette) {
    Patch patch;
    if (n < 8) return patch;
    int width  = rd_i16(data);
    int height = rd_i16(data + 2);
    if (width < 0 || height < 0) I_Error("decodePatch: negative dimensions");
    patch.width = width;
    patch.height = height;
    patch.rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0u);  // transparent
    for (int col = 0; col < width; ++col) {
        const byte* base = data + 8;
        if (base + 4 * col + 4 > data + n) break;
        int ofs = rd_i32(base + 4 * col);
        if (ofs <= 0 || ofs >= static_cast<int>(n)) continue;
        const byte* p = data + ofs;
        while (p < data + n) {
            byte topdelta = *p++;
            if (topdelta == 0xFF) break;
            if (p + 1 > data + n) break;
            byte length = *p++;
            if (p < data + n) ++p;            // unused pad
            for (byte i = 0; i < length; ++i) {
                if (p >= data + n) break;
                byte pix = *p++;
                int row = topdelta + i;
                if (row >= 0 && row < height)
                    patch.rgba[static_cast<size_t>(row) * width + col] = palette[pix] | 0xFFu;
            }
            if (p < data + n) ++p;            // unused pad
        }
    }
    return patch;
}

std::vector<TextureDef> parseTextureDefs(const byte* d, size_t n) {
    if (n < 4) I_Error("parseTextureDefs: too small");
    int num = rd_i32(d);
    if (num < 0) I_Error("parseTextureDefs: negative numtextures");
    std::vector<TextureDef> out;
    out.reserve(num);
    for (int i = 0; i < num; ++i) {
        if (d + 4 + (i + 1) * 4 > d + n) I_Error("parseTextureDefs: truncated offsets");
        int ofs = rd_i32(d + 4 + i * 4);
        const byte* p = d + ofs;
        if (p + 22 > d + n) I_Error("parseTextureDefs: truncated maptexture");
        // maptexture_t: name(8)@0, masked(4)@8[ignore], width(2)@12, height(2)@14,
        // columndirectory(4)@16[ignore], patchcount(2)@20, mappatches@22.
        TextureDef def;
        std::memcpy(def.name, p, 8); def.name[8] = '\0';
        for (int k = 7; k >= 0 && def.name[k] == ' '; --k) def.name[k] = '\0';
        def.width  = rd_i16(p + 12);
        def.height = rd_i16(p + 14);
        int patchcount = rd_i16(p + 20);
        const byte* pp = p + 22;
        for (int j = 0; j < patchcount; ++j) {
            if (pp + 6 > d + n) I_Error("parseTextureDefs: truncated mappatch");
            Mappatch mp;
            mp.originx = rd_i16(pp);
            mp.originy = rd_i16(pp + 2);
            mp.patch   = rd_i16(pp + 4);
            def.patches.push_back(mp);
            pp += 6;
        }
        out.push_back(std::move(def));
    }
    return out;
}

Texture compositeTexture(const TextureDef& def, const std::vector<Patch>& patches) {
    Texture t;
    std::memcpy(t.name, def.name, 9);
    t.width = def.width;
    t.height = def.height;
    if (def.width <= 0 || def.height <= 0) return t;
    t.rgba.assign(static_cast<size_t>(def.width) * static_cast<size_t>(def.height), 0u);
    for (const Mappatch& mp : def.patches) {
        if (mp.patch < 0 || mp.patch >= static_cast<int>(patches.size())) continue;
        const Patch& pat = patches[mp.patch];
        for (int y = 0; y < pat.height; ++y) {
            for (int x = 0; x < pat.width; ++x) {
                int tx = mp.originx + x;
                int ty = mp.originy + y;
                if (tx < 0 || tx >= def.width || ty < 0 || ty >= def.height) continue;
                uint32_t src = pat.rgba[static_cast<size_t>(y) * pat.width + x];
                if (!(src & 0xFFu)) continue;                 // transparent -> skip
                t.rgba[static_cast<size_t>(ty) * def.width + tx] = src;
            }
        }
    }
    return t;
}

TextureLookup::TextureLookup(const WadFile& wad) {
    // PLAYPAL -> palette_ (first 256 entries; RGB -> RGBA opaque)
    auto pal = const_cast<WadFile&>(wad).readLumpByName("PLAYPAL");
    for (int i = 0; i < 256; ++i) {
        if (static_cast<size_t>(i * 3 + 2) < pal.size())
            palette_[i] = (static_cast<uint32_t>(pal[i * 3]) << 24) |
                          (static_cast<uint32_t>(pal[i * 3 + 1]) << 16) |
                          (static_cast<uint32_t>(pal[i * 3 + 2]) << 8) | 0xFFu;
        else
            palette_[i] = 0u;
    }

    // PNAMES
    auto pn = const_cast<WadFile&>(wad).readLumpByName("PNAMES");
    std::vector<std::string> pnames = pn.empty() ? std::vector<std::string>{} : parsePnames(pn.data(), pn.size());

    // decode every patch referenced by TEXTURE1 (+TEXTURE2), one decode each
    std::vector<Patch> patches(pnames.size());
    auto ensurePatch = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(patches.size())) return;
        if (patches[idx].width != 0 || patches[idx].height != 0) return;   // decoded
        int lump = const_cast<WadFile&>(wad).checkNumForName(pnames[idx]);
        if (lump < 0) { patches[idx] = Patch{0, 0, {}}; return; }
        auto raw = const_cast<WadFile&>(wad).readLump(lump);
        patches[idx] = raw.empty() ? Patch{0, 0, {}} : decodePatch(raw.data(), raw.size(), palette_.data());
    };

    auto loadLumpDefs = [&](const char* lumpName) {
        int lump = const_cast<WadFile&>(wad).checkNumForName(lumpName);
        if (lump < 0) return std::vector<TextureDef>{};
        auto raw = const_cast<WadFile&>(wad).readLump(lump);
        return raw.empty() ? std::vector<TextureDef>{} : parseTextureDefs(raw.data(), raw.size());
    };

    for (TextureDef def : loadLumpDefs("TEXTURE1")) {
        for (const Mappatch& mp : def.patches) ensurePatch(mp.patch);
        walls_.push_back(compositeTexture(def, patches));
    }
    for (TextureDef def : loadLumpDefs("TEXTURE2")) {
        for (const Mappatch& mp : def.patches) ensurePatch(mp.patch);
        walls_.push_back(compositeTexture(def, patches));
    }
    for (int i = 0; i < static_cast<int>(walls_.size()); ++i) wallIndex_[upper(walls_[i].name)] = i;
}

const Texture* TextureLookup::wall(const std::string& name) const {
    auto it = wallIndex_.find(upper(name));
    return it == wallIndex_.end() ? nullptr : &walls_[it->second];
}
