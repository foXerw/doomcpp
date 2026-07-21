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

// A decoded flat: 64x64 full-bright RGBA (alpha 0xFF).
struct Flat { char name[9] = {0}; int width = 64, height = 64; std::vector<uint32_t> rgba; };

// PNAMES: count + 8-byte names.
std::vector<std::string> parsePnames(const byte* d, size_t n);

// Decode a patch lump to RGBA using a 256-entry palette (each = (R<<24)|(G<<16)|(B<<8)|A).
Patch decodePatch(const byte* data, size_t n, const uint32_t* palette);

struct Mappatch   { int originx, originy, patch; };          // patch = PNAMES index
struct TextureDef { char name[9] = {0}; int width = 0, height = 0; std::vector<Mappatch> patches; };

std::vector<TextureDef> parseTextureDefs(const byte* d, size_t n);  // one TEXTURE1/2 lump
Texture compositeTexture(const TextureDef& def, const std::vector<Patch>& patches);  // patches[i] = PNAMES idx i

// Decode a 4096-byte (64x64, row-major) flat lump to RGBA using a 256-entry palette.
Flat decodeFlat(const byte* data, size_t n, const uint32_t* palette, const std::string& name);

// Resolve an (possibly animated) flat name to its frame name at `tick`.
// Known anim groups cycle; everything else returns the input unchanged.
std::string resolveFlatFrame(const std::string& name, uint32_t tick);

class TextureLookup {
public:
    explicit TextureLookup(const WadFile& wad);
    // Case-insensitive; nullptr if no such wall texture.
    const Texture* wall(const std::string& name) const;
    // Case-insensitive; nullptr if no such flat.
    const Flat* flat(const std::string& name) const;
    // True if `name` is the F_SKY1 sky marker (not a renderable flat).
    static bool isSky(const std::string& name);
    // Resolve `name` to its animated frame at `tick`, then look up the flat.
    const Flat* flatForFrame(const std::string& name, uint32_t tick) const;
private:
    std::array<uint32_t, 256> palette_{};
    std::unordered_map<std::string, int> wallIndex_;
    std::vector<Texture> walls_;
    std::unordered_map<std::string, int> flatIndex_;
    std::vector<Flat> flats_;
};
