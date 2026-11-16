#include "ui/f_title.h"
#include "ui/f_font.h"
#include "render/r_data.h"   // loadPalette, decodePatch, Patch
#include "wad/wadfile.h"

void F_DrawPatch(uint32_t* fb, int fbw, int fbh, const Patch& p, int x, int y) {
    if (p.rgba.empty() || p.width <= 0 || p.height <= 0) return;
    for (int py = 0; py < p.height; ++py) {
        const int dy = y + py;
        if (dy < 0 || dy >= fbh) continue;
        for (int px = 0; px < p.width; ++px) {
            const int dx = x + px;
            if (dx < 0 || dx >= fbw) continue;
            const uint32_t t = p.rgba[static_cast<size_t>(py) * p.width + px];
            if (!(t & 0xFFu)) continue;                 // transparent: keep the background
            fb[static_cast<size_t>(dy) * fbw + dx] = t;
        }
    }
}

TitleScreen::TitleScreen(const WadFile& wad) {
    static Patch storage;                                 // one title graphic per process
    const auto pal = loadPalette(wad);
    if (wad.checkNumForName("TITLEPIC") < 0) return;
    auto raw = const_cast<WadFile&>(wad).readLumpByName("TITLEPIC");
    if (raw.empty()) return;
    Patch p = decodePatch(raw.data(), raw.size(), pal.data());
    if (p.width <= 0 || p.height <= 0 || p.rgba.empty()) return;
    storage = std::move(p);
    pic_ = &storage;
    width_ = storage.width;
    height_ = storage.height;
}

void TitleScreen::draw(uint32_t* fb, int w, int h, const Font& font, uint32_t tic) const {
    for (int i = 0; i < w * h; ++i) fb[i] = 0x000000FFu;   // opaque black behind everything
    if (pic_) F_DrawPatch(fb, w, h, *pic_, (w - width_) / 2, (h - height_) / 2);
    // Blinking prompt. It sits just under the artwork's own title block: Freedoom's
    // TITLEPIC has its name and version baked in, and the first attempt landed right on
    // top of the "PHASE 1" lettering.
    if ((tic % 48) < 32)
        font.drawCentered(fb, w, h, h - 24, "PRESS ENTER", 1);
}
