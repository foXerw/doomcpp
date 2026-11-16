#include "doctest.h"
#include "ui/f_title.h"
#include "render/r_data.h"   // Patch
#include <vector>

TEST_CASE("F_DrawPatch blits opaque texels and leaves transparent ones alone") {
    constexpr int W = 6, H = 4;
    std::vector<uint32_t> fb(W * H, 0x001122FFu);          // a distinguishable background
    Patch p;
    p.width = 2; p.height = 2;
    // doomcpp pixels are (R<<24)|(G<<16)|(B<<8)|A, so alpha 0xFF means opaque.
    p.rgba = { 0xFF0000FFu, 0x00000000u,              // red, transparent
               0x0000FFFFu, 0x00FF00FFu };            // blue, green
    F_DrawPatch(fb.data(), W, H, p, 1, 1);
    CHECK(fb[1 * W + 1] == 0xFF0000FFu);             // (0,0) -> (1,1)
    CHECK(fb[1 * W + 2] == 0x001122FFu);             // transparent texel kept the background
    CHECK(fb[2 * W + 1] == 0x0000FFFFu);
    CHECK(fb[2 * W + 2] == 0x00FF00FFu);
}

TEST_CASE("F_DrawPatch clips to the framebuffer instead of writing out of bounds") {
    constexpr int W = 8, H = 8;
    std::vector<uint32_t> fb(W * H, 0u);
    Patch p;
    p.width = 6; p.height = 6;
    p.rgba.assign(36, 0x00FF00FFu);
    F_DrawPatch(fb.data(), W, H, p, -2, -2);         // hangs off the top-left
    CHECK(fb[0] == 0x00FF00FFu);                     // the visible part was drawn
    CHECK(fb[3 * W + 3] == 0x00FF00FFu);             // last covered cell
    CHECK(fb[4 * W + 4] == 0u);                      // past the patch's own extent
    std::vector<uint32_t> fb2(W * H, 0u);
    F_DrawPatch(fb2.data(), W, H, p, 10, 10);        // entirely off-screen
    CHECK(fb2[0] == 0u);
    std::vector<uint32_t> fb3(W * H, 0x000000FFu);
    Patch empty;
    F_DrawPatch(fb3.data(), W, H, empty, 0, 0);      // degenerate patch is a no-op
    CHECK(fb3[0] == 0x000000FFu);
}
