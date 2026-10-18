#!/usr/bin/env python3
"""Host callback regression for actual dm/android/app/native.c.

The test extracts named static production functions from native.c (fail closed
if missing/ambiguous), compiles that actual text against owned Android/WSI
boundary shims and the real wsi/lifecycle sources, and runs callback assertions.
No implementation is copied into the fixture. A private mutant is compiled from
a temporary copy and must fail; the repository is never modified.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

FUNCTIONS = ["window_ready", "window_resized", "window_destroyed",
             "pause_app", "resume_app", "draw_diagnostic"]


def extract_function(text: str, name: str) -> str:
    matches = list(re.finditer(rf"(?m)^static\s+[^\n]*\b{re.escape(name)}\s*\(", text))
    if len(matches) != 1:
        raise SystemExit(f"fail closed: expected exactly one static {name}, found {len(matches)}")
    start = matches[0].start()
    brace = text.find("{", matches[0].end())
    if brace < 0:
        raise SystemExit(f"fail closed: no body for {name}")
    depth = 0
    in_string = False
    in_char = False
    escaped = False
    in_line = False
    in_block = False
    i = brace
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if in_line:
            if ch == "\n": in_line = False
        elif in_block:
            if ch == "*" and nxt == "/":
                in_block = False; i += 1
        elif in_string:
            if escaped: escaped = False
            elif ch == "\\": escaped = True
            elif ch == '"': in_string = False
        elif in_char:
            if escaped: escaped = False
            elif ch == "\\": escaped = True
            elif ch == "'": in_char = False
        else:
            if ch == "/" and nxt == "/": in_line = True; i += 1
            elif ch == "/" and nxt == "*": in_block = True; i += 1
            elif ch == '"': in_string = True
            elif ch == "'": in_char = True
            elif ch == "{": depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[start:i + 1]
        i += 1
    raise SystemExit(f"fail closed: unbalanced body for {name}")


PRELUDE = r'''
#include "lifecycle.h"
#include "wsi_diagnostic.h"
#include "wsi_bridge.h"
#include "view_policy.h"
#include "present_policy.h"
#include "diagnostic_scene.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int VkResult;
#define VK_SUCCESS 0
#define DM_DIAG_TARGET_EXTENT 256u
#define DM_VK_INDEXED_TEXTURED_VERTEX_STRIDE 16u
#define WINDOW_FORMAT_RGBA_8888 1
typedef struct { int width, height, refs; } ANativeWindow;
typedef struct { void *instance; } ANativeActivity;
typedef struct { void *bits; int width, height, stride, format; } ANativeWindow_Buffer;
typedef struct { int device; } dm_vk;
typedef struct { uint32_t width, height; uint8_t *bytes; size_t capacity; } dm_vk_pixels;
typedef struct { float position[2], uv[2]; } dm_vk_indexed_textured_vertex;
typedef struct {
    const dm_vk_indexed_textured_vertex *vertices;
    uint32_t vertex_count, vertex_stride;
    const uint16_t *indices; uint32_t index_count;
    const uint8_t *texture_pixels; size_t texture_size;
    uint32_t texture_width, texture_height;
    const char *vertex_spv, *fragment_spv;
} dm_vk_indexed_textured_draw;
typedef struct { int dirty; } dm_touch_hud_frame;

typedef struct {
    ANativeActivity *activity;
    dm_app_lifecycle *lifecycle;
    dm_wsi wsi;
    dm_wsi_bridge wsi_bridge;
    ANativeWindow *window;
    int resumed;
    int frame_fd;
    int wsi_last_logged;
    float density;
    dm_touch_hud_frame hud_frame;
    int hud_obs_valid;
    dm_view_policy view;
    dm_vk vk;
    char vertex[8], fragment[8];
} app_state;

static int touch_resize_calls, bad_touch_reads, update_calls, stop_calls, start_calls;
static int lock_calls, frame_calls, fail_open, fail_close, fail_start, frame_status = DM_WSI_OK;
static int scene_ready = 1;
static ANativeWindow *last_touch;
static dm_diag_scene_vertex scene_vertices[DM_DIAG_SCENE_VERTEX_COUNT];
static dm_diag_scene scene = {.vertices = scene_vertices,
    .vertex_count = DM_DIAG_SCENE_VERTEX_COUNT};
static dm_diag_scene_mismatch mismatch;

static void touch_resize_window(app_state *s, ANativeWindow *w) {
    (void)s; touch_resize_calls++; last_touch = w;
    if (!w || w->refs <= 0) bad_touch_reads++;
}
static void update_touch_state(app_state *s) { (void)s; update_calls++; }
static void stop_frame_timer(app_state *s) { if (s && s->frame_fd >= 0) { s->frame_fd = -1; stop_calls++; } }
static int start_frame_timer(app_state *s) {
    start_calls++;
    if (fail_start) return 0;
    s->frame_fd = 1; return 1;
}
static void touch_cancel(app_state *s, const char *why, int64_t now) { (void)s; (void)why; (void)now; }
static void touch_refresh_hud(app_state *s) { (void)s; }
static void dm_touch_hud_frame_dirty(dm_touch_hud_frame *f) { (void)f; }
static void dm_touch_hud_frame_suppress(dm_touch_hud_frame *f) { (void)f; }
static int dm_touch_hud_frame_begin(dm_touch_hud_frame *f, int a, int b, int c, int d) {
    (void)f; (void)a; (void)b; (void)c; (void)d; return 1;
}
static void dm_touch_hud_frame_composed(dm_touch_hud_frame *f, int ok) { (void)f; (void)ok; }
static int dm_touch_hud_frame_complete(dm_touch_hud_frame *f, int ok) { (void)f; (void)ok; return 1; }
static int draw_hud_overlay(app_state *s, ANativeWindow_Buffer *b) { (void)s; (void)b; return 0; }
#define LOG(...) ((void)0)
static int64_t monotonic_ns(void) { return 0; }
static void draw_diagnostic(ANativeActivity *a, ANativeWindow *w, int external);
int dm_diag_scene_ready(const dm_app_lifecycle *l, const void *w) {
    (void)l; (void)w; return scene_ready;
}
const dm_diag_scene *dm_diag_scene_owned(void) { return &scene; }
static VkResult dm_vk_create(dm_vk *vk) { vk->device = 1; return VK_SUCCESS; }
static VkResult dm_vk_draw_indexed_textured_offscreen(dm_vk *vk,
        const dm_vk_indexed_textured_draw *d, dm_vk_pixels *p) {
    (void)vk; (void)d; (void)p; return VK_SUCCESS;
}
uint32_t dm_diag_scene_check_pixels(const dm_diag_scene *s, const uint8_t *p,
        uint32_t w, uint32_t h, size_t c, dm_diag_scene_mismatch *m) {
    (void)s; (void)p; (void)w; (void)h; (void)c; (void)m; return 1;
}
int dm_view_policy_set_view(dm_view_policy *p, uint32_t w, uint32_t h, float d) {
    p->view_width_px = w; p->view_height_px = h; p->density = d; return 1;
}
int dm_view_policy_set_buffer(dm_view_policy *p, uint32_t w, uint32_t h) {
    (void)p; (void)w; (void)h; return 1;
}
static int ANativeWindow_getWidth(ANativeWindow *w) { return w->width; }
static int ANativeWindow_getHeight(ANativeWindow *w) { return w->height; }
static int ANativeWindow_lock(ANativeWindow *w, ANativeWindow_Buffer *b, void *r) {
    (void)w; (void)b; (void)r; lock_calls++; return 0;
}
static int ANativeWindow_unlockAndPost(ANativeWindow *w) { (void)w; return 0; }
int dm_present_copy(const dm_present_source *s, const dm_present_target *t, dm_present_rect *r) {
    (void)s; (void)t; if (r) *r = (dm_present_rect){0}; return 1;
}

static int acquire(void *ctx, void *w) { (void)ctx; ((ANativeWindow *)w)->refs++; return 0; }
static void release(void *ctx, void *w) {
    (void)ctx; ANativeWindow *win = w; if (win->refs > 0) win->refs--;
}
static int load(void *ctx) { (void)ctx; return 0; }
static int stop(void *ctx) { (void)ctx; return 0; }
static int opened(void *ctx, void *w, uint32_t ww, uint32_t hh) {
    (void)ctx; (void)w; (void)ww; (void)hh; return fail_open ? DM_WSI_ERROR : 0;
}
static int closed(void *ctx) { (void)ctx; return fail_close ? 1 : 0; }
static int framed(void *ctx) { (void)ctx; frame_calls++; return frame_status; }
static int created(void *ctx, void *w) { return dm_app_lifecycle_surface_created(ctx, w); }
static int destroyed(void *ctx, void *w) { return dm_app_lifecycle_surface_destroyed(ctx, w); }

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
'''


MAIN = r'''
int main(void) {
    app_state s;
    memset(&s, 0, sizeof s);
    s.frame_fd = -1;
    ANativeActivity activity = {&s};
    s.activity = &activity;
    dm_app_host_ops host = {.ctx = &s, .load = load, .stop = stop,
        .surface_acquire = acquire, .surface_release = release};
    s.lifecycle = dm_app_lifecycle_create(&host, NULL, NULL);
    CHECK(s.lifecycle != NULL);
    CHECK(dm_app_lifecycle_start(s.lifecycle));
    dm_wsi_ops wops = {.ctx = &s, .open = opened, .close = closed, .frame = framed};
    dm_wsi_init(&s.wsi, &wops, 1);
    dm_wsi_lifecycle_ops lops = {.ctx = s.lifecycle, .surface_created = created,
                                 .surface_destroyed = destroyed, .trace = NULL};
    dm_wsi_bridge_init(&s.wsi_bridge, &s.wsi, &lops);
    s.resumed = 1;
    ANativeWindow A = {640, 480, 0}, B = {800, 600, 0}, C = {720, 540, 0}, D = {700, 500, 0};

    window_ready(&activity, &A);
    CHECK(A.refs == 1);
    CHECK(s.window == dm_app_lifecycle_surface(s.lifecycle));
    CHECK(s.frame_fd == 1);

    fail_open = 1;
    window_ready(&activity, &B);
    CHECK(B.refs == 1);
    CHECK(s.window == dm_app_lifecycle_surface(s.lifecycle));
    CHECK(s.window != &A);
    CHECK(bad_touch_reads == 0);
    CHECK(last_touch == &B);
    fail_open = 0;

    /* Stale A destroy/resize must not stop B's timer or touch geometry. */
    int stops = stop_calls, touches = touch_resize_calls;
    uint32_t b_width = s.wsi.width;
    int frame_fd_before = s.frame_fd;
    window_destroyed(&activity, &A);
    window_resized(&activity, &A);
    CHECK(stop_calls == stops);
    CHECK(touch_resize_calls == touches);
    CHECK(s.wsi.width == b_width);
    CHECK(s.frame_fd == frame_fd_before);
    CHECK(s.window == &B);

    /* Close failure keeps the lifecycle reference and cached live window.
       Use a successfully opened C; B never opened, so it cannot exercise close. */
    window_ready(&activity, &C);
    CHECK(C.refs == 1);
    CHECK(s.window == &C);
    fail_close = 1;
    window_destroyed(&activity, &C);
    CHECK(C.refs == 1);
    CHECK(s.window == &C);
    fail_close = 0;

    /* Pause/pre-resume/windowless/sticky/surface-loss/error paths do not arm. */
    stop_calls = start_calls = frame_calls = 0;
    s.resumed = 0;
    window_ready(&activity, &C);
    CHECK(start_calls == 0);
    CHECK(s.frame_fd == -1);
    pause_app(&activity);
    CHECK(s.frame_fd == -1);
    CHECK(start_calls == 0);
    s.resumed = 1;
    dm_wsi_resume(&s.wsi);
    s.wsi.error = DM_WSI_ERROR;
    window_resized(&activity, &C);
    CHECK(start_calls == 0);
    s.wsi.error = 0;
    frame_status = DM_WSI_SURFACE_LOST;
    window_resized(&activity, &C);
    CHECK(start_calls == 0);
    frame_status = DM_WSI_ERROR;
    window_ready(&activity, &C);
    CHECK(start_calls == 0);
    frame_status = DM_WSI_OK;
    fail_start = 1;
    window_ready(&activity, &C);
    CHECK(s.frame_fd == -1);
    CHECK(start_calls > 0);
    fail_start = 0;

    /* Successfully destroyed window: a later resize is windowless and cannot arm. */
    window_ready(&activity, &D);
    CHECK(D.refs == 1);
    int d_starts = start_calls;
    window_destroyed(&activity, &D);
    CHECK(D.refs == 0);
    window_resized(&activity, &D);
    CHECK(start_calls == d_starts);

    /* WSI render entry never reaches CPU lock; default CPU path does. */
    lock_calls = 0;
    s.wsi.enabled = 1;
    draw_diagnostic(&activity, &C, 1);
    CHECK(lock_calls == 0);
    s.wsi.enabled = 0;
    s.hud_obs_valid = 0;
    s.vertex[0] = 'v'; s.fragment[0] = 'f';
    memset(&s.view, 0, sizeof s.view);
    draw_diagnostic(&activity, &C, 1);
    CHECK(lock_calls == 1);

    fail_close = 0;
    window_destroyed(&activity, &C);
    dm_wsi_stop(&s.wsi);
    dm_app_lifecycle_stop(s.lifecycle);
    dm_app_lifecycle_destroy(s.lifecycle);
    printf("native callbacks: OK (touches=%d frames=%d lock=%d)\n",
           touch_resize_calls, frame_calls, lock_calls);
    return 0;
}
'''


def build_source(native: Path) -> str:
    text = native.read_text(encoding="utf-8")
    return PRELUDE + "\n".join(extract_function(text, name) for name in FUNCTIONS) + MAIN


def compile_and_run(cc: str, app: Path, source: str, work: Path, label: str) -> int:
    c_file = work / f"{label}.c"
    exe = work / label
    c_file.write_text(source, encoding="utf-8")
    compile_command = [cc, "-std=gnu11", "-g", "-fsanitize=address,undefined",
                       f"-I{app}", str(c_file),
                       str(app / "wsi_bridge.c"), str(app / "wsi_diagnostic.c"),
                       str(app / "lifecycle.c"), "-o", str(exe)]
    built = subprocess.run(compile_command, text=True, capture_output=True)
    if built.returncode != 0:
        print(f"{label}: compile failed\n{built.stderr}", file=sys.stderr)
        return 1
    run = subprocess.run([str(exe)], text=True, capture_output=True,
                         env={"ASAN_OPTIONS": "detect_leaks=0"})
    print(f"{label}: exit={run.returncode}")
    if run.stdout: print(run.stdout, end="")
    if run.stderr: print(run.stderr, end="", file=sys.stderr)
    return run.returncode


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--app", required=True)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    native = Path(args.native)
    app = Path(args.app)
    normal = build_source(native)
    mutant_text = native.read_text(encoding="utf-8")
    old = "s->window=dm_app_lifecycle_surface(s->lifecycle);"
    new = "/* mutant: omit cached-window synchronization */"
    if old not in mutant_text:
        print("fail closed: mutant anchor missing", file=sys.stderr)
        return 1
    mutated = mutant_text.replace(old, new, 1)
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        normal_rc = compile_and_run(args.cc, app, normal, work, "native-callbacks")
        if normal_rc != 0:
            return 1
        mutant_rc = compile_and_run(args.cc, app, build_source_from_text(mutated), work,
                                    "native-callbacks-mutant")
        if mutant_rc == 0:
            print("FAIL: private native mutant did not fail", file=sys.stderr)
            return 1
        print("private native mutant failed as required")
    return 0


def build_source_from_text(text: str) -> str:
    return PRELUDE + "\n".join(extract_function(text, name) for name in FUNCTIONS) + MAIN


if __name__ == "__main__":
    raise SystemExit(main())
