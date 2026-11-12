#pragma once
#include <string>

// P7 campaign progression (vanilla g_game.c subset): episode/map sequencing.
// Map names are "E<episode>M<map>" (episode 1..4, map 1..9).

// Next map after `cur`; an empty string means the campaign is complete.
// Normal exits advance M1..M7 -> M8, M8 -> next episode M1 (E4M8 ends the game),
// M9 -> M4. A secret exit goes to M9 (except from M9 itself, which is normal).
std::string G_NextLevelName(const std::string& cur, bool secretExit);

// True for the final map of the final episode (E4M8).
bool G_IsCampaignEnd(const std::string& cur);

// Title string for the intermission screen ("E1M1: ..." -> map name).
std::string G_LevelLabel(const std::string& mapName);
