#include "sfx.h"

const char* const sfxNames[NUMSFX] = {
    nullptr,
    "PISTOL", "SHOTGN", "SGCOCK", "SAWUP", "SAWIDL", "SAWFUL",
    "SAWHIT", "RLAUNC", "RXPLOD", "FIRSHT", "FIRXPL", "PSTART",
    "PSTOP", "DOROPN", "DORCLS", "STNMOV", "SWTCHN", "SWTCHX",
    "PLPAIN", "DMPAIN", "POPAIN", "SLOP", "ITEMUP", "WPNUP",
    "OOF", "TELEPT", "POSIT1", "POSIT2", "POSIT3", "BGSIT1",
    "BGSIT2", "SGTSIT", "BRSSIT", "SGTATK", "CLAW", "PLDETH",
    "PDIEHI", "PODTH1", "PODTH2", "PODTH3", "BGDTH1", "BGDTH2",
    "SGTDTH", "BRSDTH", "POSACT", "BGACT", "DMACT", "NOWAY",
    "BAREXP", "PUNCH", "TINK", "BDOPN", "BDCLS", "ITMBK",
    "GETPOW", "PLASMA", "BFG", "CACSIT", "CYBSIT", "SPISIT",
    "SKLATK", "CACDTH", "SKLDTH", "CYBDTH", "SPIDTH", "HOOF",
    "METAL",
};

static_assert(sizeof(sfxNames) / sizeof(sfxNames[0]) == NUMSFX, "sfxNames out of sync");
