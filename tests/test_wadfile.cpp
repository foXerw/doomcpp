#include "doctest.h"
#include "wad/wadfile.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
void put_le32(std::vector<unsigned char>& v, std::int32_t x) {
    auto u = static_cast<std::uint32_t>(x);
    v.push_back(static_cast<unsigned char>(u));
    v.push_back(static_cast<unsigned char>(u >> 8));
    v.push_back(static_cast<unsigned char>(u >> 16));
    v.push_back(static_cast<unsigned char>(u >> 24));
}
void put_name(std::vector<unsigned char>& v, const std::string& n) {
    char buf[8] = {0};
    for (int i = 0; i < 8 && i < static_cast<int>(n.size()); ++i) buf[i] = n[i];
    v.insert(v.end(), buf, buf + 8);
}
// Minimal valid WAD: header(12) + "hello"(5) + "worlds!"(7) + dir(2*16).
std::string make_test_wad() {
    std::vector<unsigned char> w;
    w.insert(w.end(), {'I', 'W', 'A', 'D'});
    put_le32(w, 2);            // numlumps
    put_le32(w, 24);           // infotableofs = 12 + 5 + 7
    const std::string d0 = "hello", d1 = "worlds!";
    w.insert(w.end(), d0.begin(), d0.end());
    w.insert(w.end(), d1.begin(), d1.end());
    put_le32(w, 12); put_le32(w, 5); put_name(w, "LUMPONE");
    put_le32(w, 17); put_le32(w, 7); put_name(w, "LUMPTWO");
    std::string path = "test_wad_tmp.wad";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size()));
    return path;
}
}

TEST_CASE("WadFile parses header and directory") {
    std::string p = make_test_wad();
    WadFile wad(p);
    CHECK(wad.isIWAD());
    CHECK(wad.numLumps() == 2);
    CHECK(wad.lumpName(0) == "LUMPONE");
    CHECK(wad.lumpName(1) == "LUMPTWO");
    CHECK(wad.lumpSize(0) == 5);
    CHECK(wad.lumpSize(1) == 7);
    std::remove(p.c_str());
}
