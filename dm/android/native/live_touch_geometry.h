// Actual rendered-picture geometry for Android touch forwarding.
// Front-end pages are the original centered 16:9 UI; in-run output follows the
// phone-aspect surface. Invalid surfaces must not produce a fake picture.
#pragma once
#include <stdint.h>

typedef struct {
    int valid;
    float left, top, width, height;
    float logical_width, logical_height;
} dm_live_touch_geometry;

static inline dm_live_touch_geometry dm_live_touch_geometry_for(int surface_w, int surface_h, int ingame) {
    dm_live_touch_geometry g = {0};
    if (surface_w < 320 || surface_h < 240 || surface_w <= 0 || surface_h <= 0 ||
        surface_w > 16384 || surface_h > 16384) return g;
    float sw = (float)surface_w, sh = (float)surface_h;
    if (ingame) {
        g.width = sw;
        g.height = sh;
    } else {
        // Original front-end pages: centered 16:9 within the surface.
        g.width = sw;
        g.height = sw * 9.0f / 16.0f;
        if (g.height > sh) {
            g.height = sh;
            g.width = sh * 16.0f / 9.0f;
        }
        g.left = (sw - g.width) * 0.5f;
        g.top = (sh - g.height) * 0.5f;
    }
    g.logical_width = 667.0f;
    g.logical_height = g.logical_width * g.height / g.width;
    g.valid = g.width > 0 && g.height > 0 && g.logical_height > 0;
    return g;
}
