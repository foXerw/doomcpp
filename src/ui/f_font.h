#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "render/r_data.h"

class WadFile;

// P7: the WAD's status font (STCFN033..STCFN095 = printable ASCII 33..95).
// Lowercase input is folded to uppercase; unknown glyphs get a blank advance.
class Font {
public:
    explicit Font(const WadFile& wad);
    Font() = default;                       // empty (test path; install() glyphs manually)
    bool valid() const { return any_; }
    void install(char c, const Patch& p);   // test/embedding hook
    int  textWidth(const std::string& text, int scale = 1) const;
    void drawText(uint32_t* fb, int w, int h, int x, int y,
                  const std::string& text, int scale = 1) const;
    void drawCentered(uint32_t* fb, int w, int h, int y,
                      const std::string& text, int scale = 1) const;

private:
    const Patch* glyph(char c) const;
    std::array<Patch, 63> glyphs_{};   // index = c - 33
    bool any_ = false;
};
