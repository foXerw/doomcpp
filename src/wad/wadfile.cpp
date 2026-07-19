#include "wadfile.h"
#include "core/i_system.h"
#include <cstring>

namespace {
// Read a little-endian int32 (WAD format is LE).
std::int32_t read_le_i32(const unsigned char* p) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24));
}
// Uppercase + NUL-pad/truncate a lump name into exactly 8 bytes.
void normalize_name(const std::string& in, char out[8]) {
    std::memset(out, 0, 8);
    for (int i = 0; i < 8 && i < static_cast<int>(in.size()); ++i) {
        char c = in[i];
        out[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
    }
}
}

WadFile::WadFile(const std::string& path) {
    file_.open(path, std::ios::binary);
    if (!file_) {
        I_Error(std::string("WadFile: cannot open ") + path);
    }
    unsigned char hdr[12];
    file_.read(reinterpret_cast<char*>(hdr), 12);
    if (!file_) {
        I_Error("WadFile: truncated header");
    }
    magic_.assign(reinterpret_cast<const char*>(hdr), 4);
    if (magic_ != "IWAD" && magic_ != "PWAD") {
        I_Error(std::string("WadFile: not IWAD/PWAD: ") + magic_);
    }
    int numlumps     = read_le_i32(hdr + 4);
    int infotableofs = read_le_i32(hdr + 8);

    std::vector<unsigned char> dir(static_cast<size_t>(numlumps) * 16);
    file_.seekg(infotableofs, std::ios::beg);
    file_.read(reinterpret_cast<char*>(dir.data()),
               static_cast<std::streamsize>(dir.size()));
    if (!file_) {
        I_Error("WadFile: truncated directory");
    }
    lumps_.reserve(static_cast<size_t>(numlumps));
    for (int i = 0; i < numlumps; ++i) {
        const unsigned char* e = dir.data() + static_cast<size_t>(i) * 16;
        LumpInfo li;
        li.filepos = read_le_i32(e);
        li.size    = read_le_i32(e + 4);
        std::memcpy(li.name, e + 8, 8);
        li.name[8] = '\0';
        lumps_.push_back(li);
    }
}

std::string WadFile::lumpName(int i) const {
    if (i < 0 || i >= numLumps()) I_Error("WadFile::lumpName: out of range");
    return std::string(lumps_[i].name);
}

int WadFile::lumpSize(int i) const {
    if (i < 0 || i >= numLumps()) I_Error("WadFile::lumpSize: out of range");
    return lumps_[i].size;
}

int WadFile::checkNumForName(const std::string& name) const {
    char want[8];
    normalize_name(name, want);
    for (int i = numLumps() - 1; i >= 0; --i) {   // backward scan (last wins)
        if (std::memcmp(lumps_[i].name, want, 8) == 0) return i;
    }
    return -1;
}

int WadFile::getNumForName(const std::string& name) const {
    int i = checkNumForName(name);
    if (i < 0) I_Error(std::string("WadFile::getNumForName: not found: ") + name);
    return i;
}
