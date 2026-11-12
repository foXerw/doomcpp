#include "doctest.h"
#include "play/midi.h"
#include <cstdint>
#include <vector>

TEST_CASE("parseMidi reads a minimal SMF track into timed notes") {
    const std::vector<std::uint8_t> smf = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,0x60,      // format 0, 1 track, 96 PPQ
        'M','T','r','k', 0,0,0,19,
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,        // tempo 500000 us/quarter
        0x00, 0x90, 0x3C, 0x64,                          // note on C4 vel 100
        0x60, 0x80, 0x3C, 0x00,                          // note off after 96 ticks
        0x00, 0xFF, 0x2F, 0x00,                          // end of track
    };
    MidiSong song;
    REQUIRE(parseMidi(smf.data(), smf.size(), song));
    REQUIRE(song.notes.size() == 1);
    CHECK(song.notes[0].pitch == 60);
    CHECK(song.notes[0].velocity == 100);
    CHECK(song.notes[0].start == doctest::Approx(0.0));
    CHECK(song.notes[0].end == doctest::Approx(0.5));
    CHECK(song.length == doctest::Approx(0.5));
}

TEST_CASE("parseMidi handles running status and program changes") {
    const std::vector<std::uint8_t> smf = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,0x60,
        'M','T','r','k', 0,0,0,14,
        0x00, 0xC0, 0x30,                                // program 48 on channel 0
        0x00, 0x90, 0x40, 0x50,                          // note on E4
        0x30, 0x40, 0x00,                                // running status: note on vel 0 = off
        0x00, 0xFF, 0x2F, 0x00,
    };
    MidiSong song;
    REQUIRE(parseMidi(smf.data(), smf.size(), song));
    REQUIRE(song.notes.size() == 1);
    CHECK(song.notes[0].program == 48);
    CHECK(song.notes[0].pitch == 64);
    CHECK(song.notes[0].end > song.notes[0].start);
}

TEST_CASE("parseMidi rejects malformed data") {
    MidiSong song;
    CHECK(!parseMidi(nullptr, 0, song));
    const std::uint8_t junk[] = { 'N','O','P','E', 0,0,0,6, 0,0,0,1,0,0x60 };
    CHECK(!parseMidi(junk, sizeof(junk), song));
}
