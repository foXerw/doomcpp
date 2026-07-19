#pragma once
#include <fstream>
#include <string>
#include <vector>
#include "core/doomtype.h"

// Faithful WAD reader (header + lump directory), mirroring w_wad.c.
class WadFile {
public:
    explicit WadFile(const std::string& path);  // opens + parses; I_Error on failure

    const std::string& magic() const { return magic_; }  // "IWAD" or "PWAD"
    bool isIWAD() const { return magic_ == "IWAD"; }

    int numLumps() const { return static_cast<int>(lumps_.size()); }
    std::string lumpName(int i) const;
    int lumpSize(int i) const;

    // -1 if not found. Scans backward (later lumps override), case-insensitive.
    int checkNumForName(const std::string& name) const;
    // Calls I_Error if not found.
    int getNumForName(const std::string& name) const;

private:
    struct LumpInfo {
        int  filepos;
        int  size;
        char name[9];   // 8 chars + NUL
    };

    std::ifstream         file_;
    std::string           magic_;
    std::vector<LumpInfo> lumps_;
};
