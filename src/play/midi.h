#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// P7 music: a minimal Standard MIDI File (SMF) parser. Freedoom ships its music as
// MIDI lumps (MThd/MTrk), not MUS, so this is all the parsing the game needs.
// SDL-free: the platform layer turns these notes into audio samples.

struct MidiNote {
    double start = 0.0;   // seconds
    double end = 0.0;
    int    pitch = 0;     // 0..127
    int    velocity = 0;  // 1..127
    int    channel = 0;
    int    program = 0;
};

struct MidiSong {
    std::vector<MidiNote> notes;
    double length = 0.0;  // seconds (last note end)
    bool valid = false;
};

// Parse a complete SMF image. Returns false if the header/chunks are malformed.
bool parseMidi(const std::uint8_t* data, std::size_t size, MidiSong& out);
