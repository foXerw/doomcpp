#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "core/doomtype.h"

class WadFile;   // fwd

// Decoded picture (column-major source format flattened to w*h RGBA).
struct Patch { int width = 0, height = 0; std::vector<uint32_t> rgba; };

// A composited wall texture (full-brightness RGBA; alpha 0 = no patch there).
struct Texture { char name[9] = {0}; int width = 0, height = 0; std::vector<uint32_t> rgba; };

// PNAMES: count + 8-byte names.
std::vector<std::string> parsePnames(const byte* d, size_t n);

// Decode a patch lump to RGBA using a 256-entry palette (each = (R<<24)|(G<<16)|(B<<8)|A).
Patch decodePatch(const byte* data, size_t n, const uint32_t* palette);

class TextureLookup {
public:
    explicit TextureLookup(const WadFile& wad);
    // Case-insensitive; nullptr if no such wall texture.
    const Texture* wall(const std::string& name) const;
private:
    std::array<uint32_t, 256> palette_{};
    std::unordered_map<std::string, int> wallIndex_;
    std::vector<Texture> walls_;
};
