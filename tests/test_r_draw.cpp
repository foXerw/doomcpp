#include "doctest.h"
#include "render/r_draw.h"
#include <vector>

TEST_CASE("R_DrawPixel sets the pixel, clips off-screen") {
    std::vector<uint32_t> fb(10 * 10, 0);
    R_DrawPixel(fb.data(), 10, 10, 3, 4, 0xFFFFFFFFu);
    CHECK(fb[4 * 10 + 3] == 0xFFFFFFFFu);
    R_DrawPixel(fb.data(), 10, 10, -1, 0, 0xFFFFFFFFu);   // off-screen: ignored
    R_DrawPixel(fb.data(), 10, 10, 10, 0, 0xFFFFFFFFu);
    CHECK(fb[0] == 0);
}

TEST_CASE("R_DrawLine draws a horizontal row") {
    std::vector<uint32_t> fb(10 * 10, 0);
    R_DrawLine(fb.data(), 10, 10, 2, 5, 6, 5, 0xFFFFFFFFu);
    for (int x = 2; x <= 6; ++x) CHECK(fb[5 * 10 + x] == 0xFFFFFFFFu);
}
