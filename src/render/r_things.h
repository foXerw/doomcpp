#pragma once
#include "core/doomtype.h"
#include "render/r_data.h"   // Patch
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
class WadFile;   // fwd

// ---- Sprite lump-name parse (Task 3) ----
struct LumpInstall { char frame = 0; int rotation = 0; bool flip = false; };  // rotation 0 == all 8
struct SpriteLumpName { char sprname[5] = {0}; LumpInstall inst[2]; int nInst = 0; };
SpriteLumpName parseSpriteLumpName(const std::string& name);
