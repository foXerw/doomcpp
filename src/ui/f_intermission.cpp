#include "ui/f_intermission.h"
#include <algorithm>

namespace {
constexpr uint32_t BLACK = 0x000000FFu;
}  // namespace

void Intermission::draw(uint32_t* fb, int w, int h, const std::string& level,
                        int kills, int items, int secrets, const std::string& nextLevel,
                        bool victory) const {
    std::fill(fb, fb + static_cast<size_t>(w) * h, BLACK);
    if (!font_.valid()) return;

    font_.drawCentered(fb, w, h, 10, level + " FINISHED", 2);
    font_.drawCentered(fb, w, h, 48, "KILLS", 1);
    font_.drawCentered(fb, w, h, 62, std::to_string(std::clamp(kills, 0, 100)) + "%", 2);
    font_.drawCentered(fb, w, h, 90, "ITEMS", 1);
    font_.drawCentered(fb, w, h, 104, std::to_string(std::clamp(items, 0, 100)) + "%", 2);
    font_.drawCentered(fb, w, h, 132, "SECRETS", 1);
    font_.drawCentered(fb, w, h, 146, std::to_string(std::clamp(secrets, 0, 100)) + "%", 2);

    if (victory || nextLevel.empty()) {
        font_.drawCentered(fb, w, h, 164, "THE CAMPAIGN IS COMPLETE", 1);
        font_.drawCentered(fb, w, h, 180, "PRESS ENTER", 1);
    } else {
        font_.drawCentered(fb, w, h, 164, "ENTERING " + nextLevel, 1);
        font_.drawCentered(fb, w, h, 180, "PRESS ENTER", 1);
    }
}
