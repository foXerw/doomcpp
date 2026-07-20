#include "r_data.h"
#include "core/i_system.h"
#include "wad/wadfile.h"
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
}

std::vector<std::string> parsePnames(const byte* d, size_t n) {
    if (n < 4) I_Error("parsePnames: too small");
    int count = rd_i32(d);
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
