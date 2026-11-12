#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "platform/i_sound.h"
#include "wad/wadfile.h"
#include "play/sfx.h"
#include "play/midi.h"
#include "core/i_system.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr int NUM_CHANNELS = 8;

struct SoundData {
    std::vector<std::int16_t> pcm;   // device-rate mono samples
    bool valid = false;
};

struct Channel {
    const SoundData* sound = nullptr;
    int  pos = 0;                    // 16.16 fixed index into sound->pcm
    int  step = 0;                   // 16.16 source->device rate ratio
    int  volume = 0;                 // 0..127
    bool active = false;
};

SDL_AudioDeviceID g_dev = 0;
int g_rate = 22050;

// P7 music playback state (mutated by the audio callback; guarded by SDL_LockAudioDevice
// whenever the main thread replaces the song).
MidiSong g_music{};
double   g_musicTime = 0.0;
size_t   g_nextNote = 0;
std::vector<size_t> g_activeNotes{};
std::array<SoundData, NUMSFX> g_sounds{};
std::array<Channel, NUM_CHANNELS> g_channels{};

void mixMusic(std::int16_t* out, int frames);

void mixCallback(void*, Uint8* stream, int len) {
    auto* out = reinterpret_cast<std::int16_t*>(stream);
    const int frames = len / static_cast<int>(sizeof(std::int16_t));
    std::memset(out, 0, static_cast<size_t>(len));
    for (Channel& ch : g_channels) {
        if (!ch.active || !ch.sound) continue;
        const std::vector<std::int16_t>& pcm = ch.sound->pcm;
        const int n = static_cast<int>(pcm.size());
        for (int i = 0; i < frames; ++i) {
            const int idx = ch.pos >> 16;
            if (idx >= n) { ch.active = false; break; }
            const std::int32_t s = pcm[static_cast<size_t>(idx)];
            out[i] = static_cast<std::int16_t>(
                std::clamp<std::int32_t>(out[i] + (s * ch.volume) / 127, -32768, 32767));
            ch.pos += ch.step;
        }
    }
    mixMusic(out, frames);
}

std::vector<std::int16_t> decodeDmx(const byte* data, size_t n, int& rateOut) {
    std::vector<std::int16_t> out;
    rateOut = 11025;
    if (!data || n < 8) return out;
    const std::uint16_t format = static_cast<std::uint16_t>(data[0] | (data[1] << 8));
    const std::uint16_t rate   = static_cast<std::uint16_t>(data[2] | (data[3] << 8));
    const std::uint32_t count  = static_cast<std::uint32_t>(data[4]) |
                                 (static_cast<std::uint32_t>(data[5]) << 8) |
                                 (static_cast<std::uint32_t>(data[6]) << 16) |
                                 (static_cast<std::uint32_t>(data[7]) << 24);
    // Doom/DMX formats: 0 = 8-bit unsigned PCM; 3 = Freedoom's 8-bit unsigned PCM;
    // 1 = 16-bit signed little-endian. Anything else is skipped.
    if (rate) rateOut = rate;
    if (format == 0 || format == 3) {
        const size_t avail = std::min<size_t>(count, n - 8);
        out.reserve(avail);
        for (size_t i = 0; i < avail; ++i)
            out.push_back(static_cast<std::int16_t>((static_cast<int>(data[8 + i]) - 128) << 8));
    } else if (format == 1) {
        const size_t avail = std::min<size_t>(count, (n - 8) / 2);
        out.reserve(avail);
        for (size_t i = 0; i < avail; ++i)
            out.push_back(static_cast<std::int16_t>(data[8 + i * 2] |
                                                   (data[8 + i * 2 + 1] << 8)));
    }
    return out;
}

// Simple sine+octave synth: recognizable melodies, deliberately chiptune-flavoured.
void mixSongSamples(const MidiSong& song, double& time, size_t& next,
                    std::vector<size_t>& active, std::int16_t* out, int frames,
                    int rate, bool loop) {
    if (!song.valid) return;
    const double dt = 1.0 / (rate > 0 ? rate : 22050);
    constexpr double TWO_PI = 6.283185307179586;
    for (int i = 0; i < frames; ++i) {
        while (next < song.notes.size() && song.notes[next].start <= time)
            active.push_back(next++);
        double mix = 0.0;
        for (size_t k = 0; k < active.size(); ) {
            const MidiNote& n = song.notes[active[k]];
            if (n.end <= time) {
                active[k] = active.back();
                active.pop_back();
                continue;
            }
            const double local = time - n.start;
            const double freq = 440.0 * std::pow(2.0, (n.pitch - 69) / 12.0);
            const double attack = std::min(1.0, local / 0.006);
            const double decay = 0.35 + 0.65 * std::exp(-2.2 * local);
            const double phase = TWO_PI * freq * local;
            const double wave = std::sin(phase) + 0.25 * std::sin(2.0 * phase);
            mix += wave * attack * decay * (n.velocity / 127.0) * 0.055;
            ++k;
        }
        if (loop && song.length > 0.0 && time > song.length + 1.0) {
            time = 0.0;
            next = 0;
            active.clear();
        }
        const std::int32_t s = static_cast<std::int32_t>(mix * 32767.0);
        out[i] = static_cast<std::int16_t>(
            std::clamp<std::int32_t>(out[i] + s, -32768, 32767));
        time += dt;
    }
}

void mixMusic(std::int16_t* out, int frames) {
    mixSongSamples(g_music, g_musicTime, g_nextNote, g_activeNotes, out, frames,
                   g_rate, /*loop*/ true);
}

std::vector<std::int16_t> resample(const std::vector<std::int16_t>& in, int srcRate, int dstRate) {
    if (in.empty() || srcRate <= 0 || dstRate <= 0 || srcRate == dstRate) return in;
    const double ratio = static_cast<double>(srcRate) / dstRate;
    const size_t outN = static_cast<size_t>(in.size() / ratio);
    std::vector<std::int16_t> out(outN);
    for (size_t i = 0; i < outN; ++i) {
        const double pos = i * ratio;
        const size_t i0 = static_cast<size_t>(pos);
        const size_t i1 = std::min(i0 + 1, in.size() - 1);
        const double f = pos - static_cast<double>(i0);
        out[i] = static_cast<std::int16_t>(in[i0] * (1.0 - f) + in[i1] * f);
    }
    return out;
}

}  // namespace

namespace i_sound {

bool init() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        I_Printf(std::string("audio init failed: ") + SDL_GetError());
        return false;
    }
    SDL_AudioSpec want{};
    want.freq = 22050;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = mixCallback;
    SDL_AudioSpec have{};
    g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_dev == 0) {
        I_Printf(std::string("audio device failed: ") + SDL_GetError());
        return false;
    }
    g_rate = have.freq > 0 ? have.freq : want.freq;
    SDL_PauseAudioDevice(g_dev, 0);
    return true;
}

void loadWad(const WadFile& wad) {
    for (auto& s : g_sounds) { s.pcm.clear(); s.valid = false; }
    g_channels = {};
    if (g_dev == 0) return;
    for (int id = 1; id < NUMSFX; ++id) {
        const char* name = sfxNames[id];
        if (!name) continue;
        const std::string lump = std::string("DS") + name;
        if (wad.checkNumForName(lump) < 0) continue;
        auto raw = const_cast<WadFile&>(wad).readLumpByName(lump);
        int rate = 11025;
        std::vector<std::int16_t> pcm = decodeDmx(raw.data(), raw.size(), rate);
        if (pcm.empty()) continue;
        SoundData& s = g_sounds[static_cast<size_t>(id)];
        s.pcm = resample(pcm, rate, g_rate);
        s.valid = !s.pcm.empty();
    }
}

void shutdown() {
    if (g_dev) SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
    for (auto& s : g_sounds) { s.pcm.clear(); s.valid = false; }
    g_channels = {};
}

bool playMusic(const WadFile& wad, const std::string& lumpName) {
    if (g_dev == 0) return false;
    if (wad.checkNumForName(lumpName) < 0) return false;
    auto raw = const_cast<WadFile&>(wad).readLumpByName(lumpName);
    MidiSong song;
    if (!parseMidi(raw.data(), raw.size(), song)) return false;
    SDL_LockAudioDevice(g_dev);
    g_music = std::move(song);
    g_musicTime = 0.0;
    g_nextNote = 0;
    g_activeNotes.clear();
    SDL_UnlockAudioDevice(g_dev);
    return true;
}

void stopMusic() {
    if (g_dev == 0) return;
    SDL_LockAudioDevice(g_dev);
    g_music = MidiSong{};
    g_musicTime = 0.0;
    g_nextNote = 0;
    g_activeNotes.clear();
    SDL_UnlockAudioDevice(g_dev);
}

float musicRms(const WadFile& wad, const std::string& lumpName, int seconds) {
    if (wad.checkNumForName(lumpName) < 0) return 0.0f;
    auto raw = const_cast<WadFile&>(wad).readLumpByName(lumpName);
    MidiSong song;
    if (!parseMidi(raw.data(), raw.size(), song)) return 0.0f;
    const int rate = 22050;
    std::vector<std::int16_t> buf(static_cast<size_t>(rate) * std::max(1, seconds), 0);
    double t = 0.0;
    size_t next = 0;
    std::vector<size_t> active;
    mixSongSamples(song, t, next, active, buf.data(), static_cast<int>(buf.size()),
                   rate, /*loop*/ false);
    double sum = 0.0;
    for (std::int16_t v : buf) sum += static_cast<double>(v) * v;
    return static_cast<float>(std::sqrt(sum / buf.size()));
}

int loadedCount() {
    int n = 0;
    for (const auto& s : g_sounds) if (s.valid) ++n;
    return n;
}

void play(int id, int volume) {
    if (g_dev == 0 || id <= 0 || id >= NUMSFX) return;
    const SoundData& snd = g_sounds[static_cast<size_t>(id)];
    if (!snd.valid || snd.pcm.empty()) return;
    const int vol = std::clamp(volume, 0, 127);
    SDL_LockAudioDevice(g_dev);
    Channel* slot = nullptr;
    for (Channel& ch : g_channels) if (!ch.active) { slot = &ch; break; }
    if (!slot) slot = &g_channels[0];        // steal the first channel
    slot->sound = &snd;
    slot->pos = 0;
    slot->step = 1 << 16;                    // loadWad already resampled to g_rate
    slot->volume = vol;
    slot->active = true;
    SDL_UnlockAudioDevice(g_dev);
}

}  // namespace i_sound
