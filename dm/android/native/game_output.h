// 手机出图几何：保持 16:9 下限/兼容项，按窗口宽高比在 720 级高度上给出更宽的游戏画面。
// Phone output geometry: keep the 16:9 floor/compatibility path; for wider surfaces return a wider
// game image at 720-level height, bounded so fullscreen never means native 3200x1440.
#pragma once

#define DM_GAME_OUTPUT_BASE_HEIGHT 720u
#define DM_GAME_OUTPUT_MAX_WIDTH 1920u

// Returns 1 and fills w/h for a landscape surface wider than 16:9 (within a small tolerance);
// returns 0 when the caller should keep its existing 16:9 constants (invalid/portrait/16:9).
static inline int dm_game_output_phone(unsigned surface_w, unsigned surface_h, unsigned *w, unsigned *h) {
    if (surface_w < 320 || surface_h < 240 || surface_w <= surface_h) return 0;
    double aspect = (double)surface_w / (double)surface_h;
    if (aspect <= (16.0 / 9.0) * 1.01) return 0;
    unsigned width = (unsigned)(aspect * DM_GAME_OUTPUT_BASE_HEIGHT + 2.5);
    width &= ~3u;   // 4-pixel alignment, matching the host's existing width rounding
    if (width < 1280) width = 1280;
    if (width > DM_GAME_OUTPUT_MAX_WIDTH) width = DM_GAME_OUTPUT_MAX_WIDTH;
    *w = width;
    *h = DM_GAME_OUTPUT_BASE_HEIGHT;
    return 1;
}
