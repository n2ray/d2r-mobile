#!/usr/bin/env python3
"""Extract and exercise the real frontend shop_ghost_sync boundary.

The function is compiled from frontend_activity.c, not copied, so coordinate
units and the live-contact token are tested on the production text.
"""
import pathlib
import re
import subprocess
import sys

android = pathlib.Path(__file__).resolve().parents[1]
src = (android / "native/frontend_activity.c").read_text()
start = src.index("static void shop_ghost_sync(")
depth = 0
end = None
for i in range(start, len(src)):
    if src[i] == "{":
        depth += 1
    elif src[i] == "}":
        depth -= 1
        if depth == 0:
            end = i + 1
            break
assert end, "shop_ghost_sync body not found"
body = src[start:end]

prelude = r'''
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define DM_TC_POINTER_LIFT_PT 24.0f
typedef struct { struct { int width, height; } layout; int navigation_mode; } dm_tc_state;
typedef struct { int active, shop_drag; float aim_origin_x, aim_origin_y, x, y; } dm_tc_finger;
typedef struct { int ui_drag_active; } game_app;
static _Atomic uint64_t ghost_contact;
static int off_calls, on_calls, last_dim;
static float last_px, last_py, last_cx, last_cy;
static const _Atomic uint64_t *last_live;
static uint64_t last_token;
static float dm_tc_scale_for_size(float w, float h) { (void)w; (void)h; return 1.0f; }
static int dm_ui_shelf_grid(float g[4]) { g[0]=0; g[1]=0; g[2]=.5f; g[3]=.5f; return 1; }
static int dm_ui_stack_left(void) { return 0; }
static void mtl_ui_ghost(int on, float px, float py, const float g[4], float cx, float cy) {
    (void)px; (void)py; (void)g; (void)cx; (void)cy;
    if (!on) off_calls++; else on_calls++;
}
static void dm_vk_ui_ghost_request(int on, float px, float py, const float g[4],
                                   float cx, float cy, int dim,
                                   const _Atomic uint64_t *live, uint64_t token) {
    (void)g; if (!on) { off_calls++; return; }
    on_calls++; last_px=px; last_py=py; last_cx=cx; last_cy=cy;
    last_dim=dim; last_live=live; last_token=token;
}
'''

main = r'''
int main(void) {
    game_app s = {.ui_drag_active = 1};
    dm_tc_state c = {.layout = {640, 360}, .navigation_mode = 0};
    dm_tc_finger f = {.active = 1, .shop_drag = 1, .aim_origin_x = 100, .aim_origin_y = 100, .x = 320, .y = 180};
    atomic_store(&ghost_contact, 7);
    shop_ghost_sync(&s, &c, &f);
    if (on_calls != 1) return 1;
    if (fabsf(last_px - 100.f/640.f) > 1e-5f || fabsf(last_py - 100.f/360.f) > 1e-5f) return 2;
    if (fabsf(last_cx - .5f) > 1e-5f || fabsf(last_cy - (.5f - 24.f/360.f)) > 1e-5f) return 3;
    if (last_live != &ghost_contact || last_token != 7) return 4;
    // Physical release invalidates the live contact; the request token is now stale.
    atomic_store(&ghost_contact, 0);
    if (atomic_load(last_live) != 0) return 5;
    int before = off_calls;
    shop_ghost_sync(&s, &c, &f);
    if (off_calls != before + 1) return 6;

    // Second viewport/layout with the same lift in CSS/physical pixels.
    atomic_store(&ghost_contact, 9);
    c.layout.width = 1280; c.layout.height = 720; f.x = 640; f.y = 360;
    shop_ghost_sync(&s, &c, &f);
    if (on_calls != 2) return 7;
    if (fabsf(last_cx - .5f) > 1e-5f || fabsf(last_cy - (.5f - 24.f/720.f)) > 1e-5f) return 8;
    if (last_token != 9) return 9;

    // Unknown state fails closed.
    s.ui_drag_active = 0; before = off_calls;
    shop_ghost_sync(&s, &c, &f);
    if (off_calls != before + 1) return 10;
    puts("PASS shop ghost sync: lift units at 640x360/1280x720, live token, release, unknown");
    return 0;
}
'''

out = pathlib.Path("/Volumes/T2/Projects/d2r-work/build/shop-ghost-sync-test.c")
out.write_text(prelude + "\n" + body + "\n" + main)
exe = out.with_suffix("")
subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                str(out), "-o", str(exe), "-lm"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
