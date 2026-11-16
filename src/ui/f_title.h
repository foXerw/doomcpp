#pragma once
#include <cstdint>

class WadFile;
class Font;
struct Patch;

// Masked blit of a patch at (x,y): transparent texels leave the background alone.
// Split out from TitleScreen so it can be tested without an IWAD.
void F_DrawPatch(uint32_t* fb, int fbw, int fbh, const Patch& p, int x, int y);

// P8c: the title screen. Vanilla cycles TITLEPIC / CREDIT; doomcpp shows TITLEPIC with a
// prompt and waits for Enter, which is the shortest honest version — the game otherwise
// drops the player straight into E1M1 with no way to pick a level.
class TitleScreen {
public:
    explicit TitleScreen(const WadFile& wad);
    bool valid() const { return width_ > 0; }
    int width() const { return width_; }
    int height() const { return height_; }
    // `tic` drives the blinking prompt (32 tics on, 16 off).
    void draw(uint32_t* fb, int w, int h, const Font& font, uint32_t tic) const;

private:
    const Patch* pic_ = nullptr;
    int width_ = 0, height_ = 0;
};
