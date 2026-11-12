#pragma once
#include <string>

class WadFile;

// P7 SDL2 audio backend: 8-channel mixer for the WAD's DS* lumps.
// doomcore reaches this through I_SetSoundHook(i_sound::play).
namespace i_sound {

bool init();                       // open the device (silent failure -> false)
void loadWad(const WadFile& wad);  // decode DS* lumps (8-bit unsigned PCM, Doom header)
void shutdown();
void play(int id, int volume);     // id = sfxnum_t, volume 0..127
int  loadedCount();                // decoded DS lumps (diagnostics)

// P7 music: stream a MIDI lump (e.g. "D_E1M1") through the built-in synth.
bool playMusic(const WadFile& wad, const std::string& lumpName);
void stopMusic();
// Diagnostics: synthesize a MIDI lump offline (no device) and return its RMS.
float musicRms(const WadFile& wad, const std::string& lumpName, int seconds = 2);

}  // namespace i_sound
