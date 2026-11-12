#include "ui/st_stuff.h"
#include "play/p_tick.h"
#include "play/info.h"
#include "wad/wadfile.h"
#include <algorithm>

namespace {
constexpr int ST_AMMOX   = 44,  ST_AMMOY   = 171;
constexpr int ST_HEALTHX = 90,  ST_HEALTHY = 171;
constexpr int ST_ARMSX   = 111, ST_ARMSY   = 172;
constexpr int ST_ARMORX  = 221, ST_ARMORY  = 171;
constexpr int ST_FACEX   = 143, ST_FACEY   = 168;
constexpr int ST_KEYSX   = 239, ST_KEYSY   = 171;
constexpr int SBARHEIGHT = 32;
}  // namespace

StatusBar::StatusBar(const WadFile& wad) {
    std::array<uint32_t, 256> pal = loadPalette(wad);
    load(wad, pal, "STBAR", bar_);
    load(wad, pal, "STFST00", face_);
    load(wad, pal, "STFDEAD0", deadFace_);
    load(wad, pal, "STTPRCNT", percent_);
    load(wad, pal, "STTMINUS", minus_);
    for (int i = 0; i < 10; ++i) {
        char name[9] = "STTNUM0";
        name[6] = static_cast<char>('0' + i);
        load(wad, pal, name, num_[i]);
    }
    for (int i = 0; i < 8; ++i) {
        char name[9] = "STKEYS0";
        name[6] = static_cast<char>('0' + i);
        load(wad, pal, name, keys_[i]);
    }
}

bool StatusBar::load(const WadFile& wad, const std::array<uint32_t, 256>& pal,
                     const char* name, Patch& out) {
    if (wad.checkNumForName(name) < 0) return false;
    auto raw = const_cast<WadFile&>(wad).readLumpByName(name);
    if (raw.empty()) return false;
    out = decodePatch(raw.data(), raw.size(), pal.data());
    return true;
}

void StatusBar::blit(uint32_t* fb, int w, int h, const Patch& p, int x0, int y0) {
    if (p.rgba.empty()) return;
    for (int py = 0; py < p.height; ++py) {
        int y = y0 + py;
        if (y < 0 || y >= h) continue;
        for (int px = 0; px < p.width; ++px) {
            int x = x0 + px;
            if (x < 0 || x >= w) continue;
            uint32_t texel = p.rgba[static_cast<size_t>(py) * p.width + px];
            if (!(texel & 0xFFu)) continue;
            fb[static_cast<size_t>(y) * w + x] = texel;
        }
    }
}

// Right-aligned number ending at xRight (vanilla STlib_drawNum). percent appends STTPRCNT.
void StatusBar::drawNum(uint32_t* fb, int w, int h, int xRight, int y, int value,
                        int maxDigits, bool percent) const {
    int x = xRight;
    int v = value < 0 ? 0 : value;
    for (int d = 0; d < maxDigits; ++d) {
        int digit = v % 10;
        const Patch& np = num_[digit];
        if (np.rgba.empty()) break;
        x -= np.width;
        blit(fb, w, h, np, x, y);
        v /= 10;
        if (v == 0) break;
    }
    if (percent && !percent_.rgba.empty())
        blit(fb, w, h, percent_, xRight + 1, y);
}

void StatusBar::draw(uint32_t* fb, int w, int h, const player_t& p) const {
    const int barY = h - SBARHEIGHT;
    if (bar_.rgba.empty()) return;
    blit(fb, w, h, bar_, 0, barY);

    const int dy = barY - ST_FACEY;   // convert 200-high screen coords to this framebuffer
    const weaponinfo_t& wi = weaponinfo[std::clamp(p.readyweapon, 0, NUMWEAPONS - 1)];
    int ammo = (wi.ammo == AM_NOAMMO) ? 0 : p.ammo[wi.ammo];
    drawNum(fb, w, h, ST_AMMOX,   ST_AMMOY + dy,   ammo, 3, false);
    drawNum(fb, w, h, ST_HEALTHX, ST_HEALTHY + dy, p.health, 3, true);
    int armor = p.armorpoints[0] + p.armorpoints[1];
    drawNum(fb, w, h, ST_ARMORX,  ST_ARMORY + dy,  armor, 3, true);

    // Weapon slot in the arms box (vanilla: chainsaw shares slot 1).
    int slot = p.readyweapon + 1;
    if (p.readyweapon == WP_CHAINSAW) slot = 1;
    if (!num_[slot % 10].rgba.empty())
        blit(fb, w, h, num_[slot % 10], ST_ARMSX, ST_ARMSY + dy);

    // Face: dead or a health tier (static expression for P5b).
    const Patch& face = (p.dead || p.health <= 0) ? deadFace_ : face_;
    blit(fb, w, h, face, ST_FACEX, ST_FACEY + dy);

    // Keys: one combined STKEYS<n> patch; bit0 blue, bit1 yellow, bit2 red.
    int keyCombo = (p.keys & KEY_BLUE ? 1 : 0) | (p.keys & KEY_YELLOW ? 2 : 0) |
                   (p.keys & KEY_RED ? 4 : 0);
    if (keyCombo >= 0 && keyCombo < 8)
        blit(fb, w, h, keys_[keyCombo], ST_KEYSX, ST_KEYSY + dy);
}
