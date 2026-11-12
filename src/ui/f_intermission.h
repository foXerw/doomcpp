#pragma once
#include <cstdint>
#include <string>
#include "ui/f_font.h"

class WadFile;

// P7 intermission ("FINISHED" tally). Drawn over a black 320x200 framebuffer.
class Intermission {
public:
    explicit Intermission(const WadFile& wad) : font_(wad) {}
    explicit Intermission(const Font& font) : font_(font) {}
    // kills/items/secrets in percent (0..100). nextLevel may be empty (campaign end).
    void draw(uint32_t* fb, int w, int h, const std::string& level,
              int kills, int items, int secrets, const std::string& nextLevel,
              bool victory) const;

private:
    Font font_;
};
