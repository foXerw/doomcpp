#pragma once
#include <array>
#include "render/r_data.h"

class WadFile;
struct player_t;

// P5b status bar (vanilla st_stuff/st_lib subset): decodes the STBAR/STTNUM*/STTPRCNT/
// STTMINUS/STFST*/STKEYS* patch lumps directly from the WAD and blits them into the
// bottom 32 rows of a 320x200 framebuffer. Numbers are drawn right-aligned like the
// vanilla STlib_drawNum; missing patches are skipped.
class StatusBar {
public:
    explicit StatusBar(const WadFile& wad);
    void draw(uint32_t* fb, int w, int h, const player_t& p) const;
    bool valid() const { return bar_.width > 0; }

private:
    static bool load(const WadFile& wad, const std::array<uint32_t, 256>& pal,
                     const char* name, Patch& out);
    static void blit(uint32_t* fb, int w, int h, const Patch& p, int x, int y);
    void drawNum(uint32_t* fb, int w, int h, int xRight, int y, int value, int maxDigits,
                 bool percent) const;

    Patch bar_;
    Patch face_;                       // healthy face; easy to extend to pain tiers
    Patch deadFace_;
    Patch percent_;
    Patch minus_;
    std::array<Patch, 10> num_{};
    std::array<Patch, 8> keys_{};
};
