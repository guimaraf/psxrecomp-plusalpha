#ifndef PSX_VIDEO_BEZEL_H
#define PSX_VIDEO_BEZEL_H

#include <SDL.h>
#include <stdint.h>

/* Allow at most one horizontal pixel of rounding (e.g. 1366x768). */
static inline int video_bezel_resolution_supported(int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    int64_t delta = (int64_t)w * 9 - (int64_t)h * 16;
    return delta >= -9 && delta <= 9;
}

/* Launcher availability follows the current resolution on its own monitor. */
static inline int video_bezel_display_supported(SDL_Window *window) {
    SDL_DisplayMode mode;
    int display = SDL_GetWindowDisplayIndex(window);
    return display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0 &&
           video_bezel_resolution_supported(mode.w, mode.h);
}

/* Presentation uses the actual output size, including exclusive fullscreen. */
static inline int video_bezel_visible(SDL_Window *window, int w, int h) {
    return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) &&
           video_bezel_resolution_supported(w, h);
}

#endif
