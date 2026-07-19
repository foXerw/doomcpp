#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "i_video.h"
#include "core/i_system.h"
#include <cstring>
#include <string>

namespace {
SDL_Window*   g_window   = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture*  g_texture  = nullptr;
int g_w = 0;
int g_h = 0;

constexpr Uint32 PIXEL_FORMAT = SDL_PIXELFORMAT_RGBA8888;
}

namespace i_video {

bool init(int width, int height, const std::string& title) {
    g_w = width;
    g_h = height;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        I_Printf(std::string("SDL_Init failed: ") + SDL_GetError());
        return false;
    }
    g_window = SDL_CreateWindow(title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width * 2, height * 2, SDL_WINDOW_SHOWN);
    if (!g_window) {
        I_Printf(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        return false;
    }
    g_renderer = SDL_CreateRenderer(g_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        // Fall back to software rendering (headless / no GPU / locked desktop).
        I_Printf(std::string("no accelerated renderer, trying software: ") + SDL_GetError());
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g_renderer) {
        I_Printf(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        return false;
    }
    g_texture = SDL_CreateTexture(g_renderer, PIXEL_FORMAT,
        SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!g_texture) {
        I_Printf(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
        return false;
    }
    return true;
}

void present(const std::uint32_t* pixels) {
    void* dst   = nullptr;
    int   pitch = 0;
    SDL_LockTexture(g_texture, nullptr, &dst, &pitch);
    std::memcpy(dst, pixels,
        static_cast<size_t>(g_w) * static_cast<size_t>(g_h) * sizeof(std::uint32_t));
    SDL_UnlockTexture(g_texture);

    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
}

bool pollEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) return false;
    }
    return true;
}

void shutdown() {
    if (g_texture)  SDL_DestroyTexture(g_texture);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window)   SDL_DestroyWindow(g_window);
    g_texture  = nullptr;
    g_renderer = nullptr;
    g_window   = nullptr;
    SDL_Quit();
}

} // namespace i_video
