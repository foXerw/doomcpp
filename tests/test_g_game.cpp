#include "doctest.h"
#include "play/g_game.h"
#include "play/p_tick.h"
#include "play/p_setup.h"
#include "play/info.h"
#include "ui/f_font.h"
#include "ui/f_intermission.h"
#include <string>
#include <vector>

namespace {
MapData levelMap() {
    MapData m;
    sector_t s; s.floorheight = 0; s.ceilingheight = 128; m.sectors.push_back(s);
    seg_t g; g.linedef = -1; g.side = 0; g.frontsector = 0; g.backsector = -1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);
    m.things.push_back({0, 0, 0, 1});   // player 1 start
    return m;
}

Patch solidGlyph(int w, int h) {
    Patch p; p.width = w; p.height = h;
    p.rgba.assign(static_cast<size_t>(w) * h, 0xFFFFFFFFu);
    return p;
}
}  // namespace

TEST_CASE("G_NextLevelName: normal progression and episode boundaries") {
    CHECK(G_NextLevelName("E1M1", false) == "E1M2");
    CHECK(G_NextLevelName("E1M7", false) == "E1M8");
    CHECK(G_NextLevelName("E1M8", false) == "E2M1");
    CHECK(G_NextLevelName("E3M8", false) == "E4M1");
    CHECK(G_NextLevelName("E4M8", false) == "");
    CHECK(G_IsCampaignEnd("E4M8"));
    CHECK(!G_IsCampaignEnd("E1M8"));
}

TEST_CASE("G_NextLevelName: secret exits go to M9, M9 returns to M4") {
    CHECK(G_NextLevelName("E1M3", true) == "E1M9");
    CHECK(G_NextLevelName("E1M9", false) == "E1M4");
    CHECK(G_NextLevelName("E1M9", true) == "E1M4");
    CHECK(G_NextLevelName("E2M9", false) == "E2M4");
}

TEST_CASE("G_NextLevelName: malformed names are rejected") {
    CHECK(G_NextLevelName("E1M0", false) == "");
    CHECK(G_NextLevelName("MAP01", false) == "");
    CHECK(G_NextLevelName("", false) == "");
}

TEST_CASE("P_SetupLevel(carry): inventory survives a level change") {
    MapData m = levelMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    P_SetupLevel(w, m);
    REQUIRE(w.player.mo != nullptr);
    w.player.health = 77;
    w.player.ammo[AM_SHELL] = 12;
    w.player.weapons |= (1u << WP_SHOTGUN);
    w.player.keys = KEY_BLUE | KEY_RED;
    w.player.armorpoints[0] = 50;
    w.player.armortype = 1;
    player_t carry = w.player;             // snapshot (old mobj pointer is fine)

    P_SetupLevel(w, m, &carry);
    CHECK(w.player.health == 77);
    CHECK(w.player.mo->health == 77);
    CHECK(w.player.ammo[AM_SHELL] == 12);
    CHECK((w.player.weapons & (1u << WP_SHOTGUN)) != 0);
    CHECK(w.player.keys == (KEY_BLUE | KEY_RED));
    CHECK(w.player.armorpoints[0] == 50);
    CHECK(w.player.killcount == 0);        // per-level tally resets

    // A fresh (pistol) start resets inventory.
    P_SetupLevel(w, m);
    CHECK(w.player.health == 100);
    CHECK(w.player.ammo[AM_SHELL] == 0);
    CHECK(w.player.keys == 0);
}

TEST_CASE("Font: glyphs, lowercase folding, spaces and drawing") {
    Font f;
    CHECK(!f.valid());
    f.install('A', solidGlyph(5, 7));
    f.install('B', solidGlyph(6, 7));
    CHECK(f.valid());
    CHECK(f.textWidth("AB") == 11);
    CHECK(f.textWidth("a b") == 5 + 4 + 6);   // lowercase folds to uppercase

    std::vector<uint32_t> fb(40 * 20, 0);
    f.drawText(fb.data(), 40, 20, 2, 3, "AB");
    CHECK(fb[3 * 40 + 2] == 0xFFFFFFFFu);     // first glyph pixel
    CHECK(fb[3 * 40 + 6] == 0xFFFFFFFFu);     // inside A (5 wide)
    CHECK(fb[3 * 40 + 8] == 0xFFFFFFFFu);     // inside B (starts at x=7)
    CHECK(fb[10 * 40 + 2] == 0u);             // below the glyph
}

TEST_CASE("Intermission: draws a tally over black") {
    Font f;
    for (char c = 'A'; c <= 'Z'; ++c) f.install(c, solidGlyph(3, 5));
    for (char c = '0'; c <= '9'; ++c) f.install(c, solidGlyph(3, 5));
    f.install('%', solidGlyph(4, 5));
    Intermission inter(f);
    std::vector<uint32_t> fb(320 * 200, 0xDEADBEEFu);
    inter.draw(fb.data(), 320, 200, "E1M1", 50, 25, 0, "E1M2", false);
    int nonBlack = 0;
    for (uint32_t p : fb) if (p != 0x000000FFu) ++nonBlack;
    CHECK(nonBlack > 100);
    CHECK(fb[0] == 0x000000FFu);              // background cleared
}
