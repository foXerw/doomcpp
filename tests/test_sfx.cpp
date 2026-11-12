#include "doctest.h"
#include "play/sfx.h"
#include "core/i_system.h"
#include <cstring>

namespace {
int g_lastId = -1;
int g_lastVol = -1;
int g_calls = 0;
void capture(int id, int vol) { g_lastId = id; g_lastVol = vol; ++g_calls; }
}  // namespace

TEST_CASE("I_StartSound routes through the platform hook (and survives no hook)") {
    g_calls = 0;
    I_SetSoundHook(nullptr);
    I_StartSound(SFX_PISTOL, 100);      // no hook: no-op, no crash
    CHECK(g_calls == 0);

    I_SetSoundHook(&capture);
    I_StartSound(SFX_DOROPN, 90);
    CHECK(g_calls == 1);
    CHECK(g_lastId == SFX_DOROPN);
    CHECK(g_lastVol == 90);
    I_SetSoundHook(nullptr);
}

TEST_CASE("sfxNames maps every sound id to a Freedoom DS lump name") {
    CHECK(sfxNames[SFX_NONE] == nullptr);
    CHECK(std::strcmp(sfxNames[SFX_PISTOL], "PISTOL") == 0);
    CHECK(std::strcmp(sfxNames[SFX_DOROPN], "DOROPN") == 0);
    CHECK(std::strcmp(sfxNames[SFX_BAREXP], "BAREXP") == 0);
    CHECK(std::strcmp(sfxNames[SFX_SPIDTH], "SPIDTH") == 0);
    for (int i = 1; i < NUMSFX; ++i) CHECK(sfxNames[i] != nullptr);
}
