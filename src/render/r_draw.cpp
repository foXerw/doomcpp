#include "r_draw.h"
#include <cstdio>
#include <vector>

void R_DrawPixel(uint32_t* fb, int w, int h, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    fb[y * w + x] = color;
}

void R_DrawLine(uint32_t* fb, int w, int h,
                int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = (dx < 0) ? -1 : 1, sy = (dy < 0) ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int err = dx - dy;
    for (;;) {
        R_DrawPixel(fb, w, h, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

bool writeBMP(const std::string& path, const uint32_t* fb, int w, int h) {
    int row = ((24 * w + 31) / 32) * 4;            // bytes per row, 4-aligned
    int sz = 54 + row * h;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = sz; hdr[3] = sz >> 8; hdr[4] = sz >> 16; hdr[5] = sz >> 24;
    hdr[10] = 54; hdr[14] = 40;
    hdr[18] = w; hdr[19] = w >> 8; hdr[22] = h; hdr[23] = h >> 8;
    hdr[26] = 1; hdr[28] = 24;                    // planes=1, bpp=24
    std::fwrite(hdr, 1, 54, f);
    std::vector<unsigned char> rowbuf(row, 0);
    for (int y = h - 1; y >= 0; --y) {            // BMP is bottom-up
        for (int x = 0; x < w; ++x) {
            uint32_t p = fb[y * w + x];            // RGBA8888: R high, A low
            rowbuf[x * 3 + 0] = (p >> 8) & 0xff;   // B
            rowbuf[x * 3 + 1] = (p >> 16) & 0xff;  // G
            rowbuf[x * 3 + 2] = (p >> 24) & 0xff;  // R
        }
        std::fwrite(rowbuf.data(), 1, row, f);
    }
    std::fclose(f);
    return true;
}
