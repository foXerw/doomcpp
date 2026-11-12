#include "ui/f_font.h"
#include "wad/wadfile.h"
#include <cctype>

namespace {
constexpr int SPACE_ADVANCE = 4;

void blitScaled(uint32_t* fb, int w, int h, const Patch& p, int x0, int y0, int scale) {
    if (p.rgba.empty()) return;
    for (int py = 0; py < p.height; ++py) {
        for (int px = 0; px < p.width; ++px) {
            const uint32_t texel = p.rgba[static_cast<size_t>(py) * p.width + px];
            if (!(texel & 0xFFu)) continue;
            for (int sy = 0; sy < scale; ++sy) {
                const int y = y0 + py * scale + sy;
                if (y < 0 || y >= h) continue;
                for (int sx = 0; sx < scale; ++sx) {
                    const int x = x0 + px * scale + sx;
                    if (x < 0 || x >= w) continue;
                    fb[static_cast<size_t>(y) * w + x] = texel;
                }
            }
        }
    }
}
}  // namespace

Font::Font(const WadFile& wad) {
    std::array<uint32_t, 256> pal = loadPalette(wad);
    for (int c = 33; c <= 95; ++c) {
        char name[9] = "STCFN000";
        name[5] = static_cast<char>('0' + (c / 100) % 10);
        name[6] = static_cast<char>('0' + (c / 10) % 10);
        name[7] = static_cast<char>('0' + c % 10);
        if (wad.checkNumForName(name) < 0) continue;
        auto raw = const_cast<WadFile&>(wad).readLumpByName(name);
        if (raw.empty()) continue;
        glyphs_[static_cast<size_t>(c - 33)] = decodePatch(raw.data(), raw.size(), pal.data());
        if (glyphs_[static_cast<size_t>(c - 33)].width > 0) any_ = true;
    }
}

void Font::install(char c, const Patch& p) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c < 33 || c > 95) return;
    glyphs_[static_cast<size_t>(c - 33)] = p;
    if (p.width > 0) any_ = true;
}

const Patch* Font::glyph(char c) const {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c < 33 || c > 95) return nullptr;
    const Patch& p = glyphs_[static_cast<size_t>(c - 33)];
    return p.width > 0 ? &p : nullptr;
}

int Font::textWidth(const std::string& text, int scale) const {
    int wsum = 0;
    for (char c : text) {
        if (c == ' ') { wsum += SPACE_ADVANCE * scale; continue; }
        const Patch* p = glyph(c);
        wsum += p ? p->width * scale : SPACE_ADVANCE * scale;
    }
    return wsum;
}

void Font::drawText(uint32_t* fb, int w, int h, int x, int y,
                    const std::string& text, int scale) const {
    int cx = x;
    for (char c : text) {
        if (c == ' ') { cx += SPACE_ADVANCE * scale; continue; }
        const Patch* p = glyph(c);
        if (!p) { cx += SPACE_ADVANCE * scale; continue; }
        blitScaled(fb, w, h, *p, cx, y, scale);
        cx += p->width * scale;
    }
}

void Font::drawCentered(uint32_t* fb, int w, int h, int y,
                        const std::string& text, int scale) const {
    drawText(fb, w, h, (w - textWidth(text, scale)) / 2, y, text, scale);
}
