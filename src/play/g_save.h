#pragma once
#include <string>

struct World;

// P7 quick save/load. The saved Level must already be loaded into w.map (main reloads
// by the saved map name before calling G_LoadGame). Returns false on I/O or version error.
bool G_SaveGame(const World& w, const std::string& path);
bool G_LoadGame(World& w, const std::string& path);

// Read just the map name from a save header (for main's reload-then-load path).
bool G_SaveGameLevel(const std::string& path, std::string& mapName);
