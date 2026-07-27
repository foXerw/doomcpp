#include "r_things.h"
#include <cctype>

SpriteLumpName parseSpriteLumpName(const std::string& name) {
    SpriteLumpName out;
    // sprname = first 4 chars, uppercased, NUL-terminated
    for (int i = 0; i < 4 && i < static_cast<int>(name.size()); ++i)
        out.sprname[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
    out.sprname[4] = 0;
    // primary install: frame = name[4], rotation = name[5]-'0'
    out.inst[0].frame = name.size() > 4 ? name[4] : 'A';
    out.inst[0].rotation = name.size() > 5 ? (name[5] - '0') : 0;
    out.inst[0].flip = false;
    out.nInst = 1;
    // mirrored second install: name[6] = frame, name[7] = rotation, flip = true
    if (name.size() > 7 && name[6] != ' ' && name[6] != '\0') {
        out.inst[1].frame = name[6];
        out.inst[1].rotation = name[7] - '0';
        out.inst[1].flip = true;
        out.nInst = 2;
    }
    return out;
}
