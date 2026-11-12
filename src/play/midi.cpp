#include "play/midi.h"
#include <algorithm>
#include <map>
#include <utility>

namespace {

struct Reader {
    const std::uint8_t* p = nullptr;
    std::size_t n = 0;
    std::size_t i = 0;
    bool ok = true;

    bool need(std::size_t k) {
        if (i + k > n) { ok = false; return false; }
        return true;
    }
    std::uint8_t u8() { return need(1) ? p[i++] : 0; }
    std::uint16_t u16() {
        if (!need(2)) return 0;
        std::uint16_t v = static_cast<std::uint16_t>((p[i] << 8) | p[i + 1]);
        i += 2;
        return v;
    }
    std::uint32_t u32() {
        if (!need(4)) return 0;
        std::uint32_t v = (static_cast<std::uint32_t>(p[i]) << 24) |
                          (static_cast<std::uint32_t>(p[i + 1]) << 16) |
                          (static_cast<std::uint32_t>(p[i + 2]) << 8) |
                          static_cast<std::uint32_t>(p[i + 3]);
        i += 4;
        return v;
    }
    std::uint32_t vlq() {
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            if (!need(1)) return v;
            const std::uint8_t b = p[i++];
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return v;
    }
    bool skip(std::size_t k) { return need(k) ? (i += k, true) : false; }
};

struct RawNote {
    std::uint32_t startTick = 0, endTick = 0;
    int pitch = 0, velocity = 0, channel = 0, program = 0;
};

struct Tempo { std::uint32_t tick = 0; std::uint32_t usecPerQuarter = 500000; };

bool fourCC(Reader& r, const char* cc) {
    if (!r.need(4)) return false;
    const bool match = r.p[r.i] == static_cast<std::uint8_t>(cc[0]) &&
                       r.p[r.i + 1] == static_cast<std::uint8_t>(cc[1]) &&
                       r.p[r.i + 2] == static_cast<std::uint8_t>(cc[2]) &&
                       r.p[r.i + 3] == static_cast<std::uint8_t>(cc[3]);
    r.i += 4;
    return match;
}

// Parse one MTrk body into raw absolute-tick notes.
void parseTrack(const std::uint8_t* data, std::size_t size,
                std::vector<RawNote>& notes, std::vector<Tempo>& tempos) {
    Reader r{data, size, 0, true};
    std::uint32_t t = 0;
    std::uint8_t running = 0;
    int program[16] = {};
    std::map<std::pair<int, int>, RawNote> open;   // (channel, pitch) -> note

    auto closeNote = [&](int ch, int pitch) {
        auto it = open.find({ch, pitch});
        if (it == open.end()) return;
        it->second.endTick = t;
        notes.push_back(it->second);
        open.erase(it);
    };

    while (r.ok && r.i < size) {
        t += r.vlq();
        if (!r.ok || r.i >= size) break;
        std::uint8_t status = data[r.i];
        if (status & 0x80) { ++r.i; running = status; }
        else status = running;                      // running status
        if (status == 0 || status == 0xFF) {
            if (status == 0xFF) {
                const std::uint8_t meta = r.u8();
                const std::uint32_t len = r.vlq();
                if (meta == 0x51 && len >= 3) {
                    Tempo tp; tp.tick = t;
                    tp.usecPerQuarter = (static_cast<std::uint32_t>(data[r.i]) << 16) |
                                        (static_cast<std::uint32_t>(data[r.i + 1]) << 8) |
                                        static_cast<std::uint32_t>(data[r.i + 2]);
                    tempos.push_back(tp);
                }
                if (meta == 0x2F) { r.skip(len); break; }
                if (!r.skip(len)) break;
            } else {
                break;
            }
            continue;
        }
        const int ch = status & 0x0F;
        const int type = status & 0xF0;
        switch (type) {
        case 0x80: case 0x90: {
            const int pitch = r.u8();
            const int vel = r.u8();
            if (type == 0x80 || vel == 0) closeNote(ch, pitch);
            else {
                closeNote(ch, pitch);               // retrigger
                RawNote n; n.startTick = t; n.pitch = pitch; n.velocity = vel;
                n.channel = ch; n.program = program[ch];
                open[{ch, pitch}] = n;
            }
            break;
        }
        case 0xA0: case 0xB0: case 0xE0: r.skip(2); break;
        case 0xC0: program[ch] = r.u8(); break;
        case 0xD0: r.skip(1); break;
        default: r.ok = false; break;
        }
    }
    for (auto& kv : open) {
        kv.second.endTick = t;
        notes.push_back(kv.second);
    }
}

double tickToSeconds(std::uint32_t tick, const std::vector<Tempo>& tempos,
                     int division) {
    double sec = 0.0;
    std::uint32_t last = 0, tempo = 500000;
    for (const Tempo& tp : tempos) {
        if (tp.tick > tick) break;
        if (tp.tick > last)
            sec += static_cast<double>(tp.tick - last) * tempo / 1.0e6 / division;
        last = tp.tick;
        tempo = tp.usecPerQuarter;
    }
    if (tick > last)
        sec += static_cast<double>(tick - last) * tempo / 1.0e6 / division;
    return sec;
}

}  // namespace

bool parseMidi(const std::uint8_t* data, std::size_t size, MidiSong& out) {
    out = MidiSong{};
    if (!data || size < 14) return false;
    Reader r{data, size, 0, true};
    if (!fourCC(r, "MThd")) return false;
    const std::uint32_t headerLen = r.u32();
    if (!r.ok || headerLen < 6) return false;
    r.u16();                                        // format
    const std::uint16_t ntrks = r.u16();
    int division = r.u16();
    r.skip(headerLen - 6);
    if (!r.ok) return false;
    if (division & 0x8000) division = 96;           // SMPTE timecode: unsupported, use 96 PPQ
    if (division <= 0) return false;

    std::vector<RawNote> raw;
    std::vector<Tempo> tempos;
    for (int tr = 0; tr < ntrks && r.ok && r.i < size; ++tr) {
        while (r.ok && r.i + 8 <= size && !fourCC(r, "MTrk")) {
            const std::uint32_t skipLen = r.u32();
            if (!r.ok || !r.skip(skipLen)) return false;
        }
        if (!r.ok || r.i + 4 > size) break;
        const std::uint32_t len = r.u32();
        if (!r.ok) return false;
        if (!r.need(len)) break;
        parseTrack(data + r.i, len, raw, tempos);
        r.i += len;
    }
    if (!r.ok && raw.empty()) return false;
    std::sort(tempos.begin(), tempos.end(),
              [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });

    out.notes.reserve(raw.size());
    for (const RawNote& n : raw) {
        MidiNote m;
        m.start = tickToSeconds(n.startTick, tempos, division);
        m.end = tickToSeconds(std::max(n.endTick, n.startTick + 1), tempos, division);
        m.pitch = n.pitch;
        m.velocity = n.velocity;
        m.channel = n.channel;
        m.program = n.program;
        out.length = std::max(out.length, m.end);
        out.notes.push_back(m);
    }
    std::sort(out.notes.begin(), out.notes.end(),
              [](const MidiNote& a, const MidiNote& b) { return a.start < b.start; });
    out.valid = !out.notes.empty();
    return out.valid;
}
