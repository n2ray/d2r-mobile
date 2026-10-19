// Read-only visual snapshot fixture for the draw-only Android overlay.
#include "../native/live_touch.c"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

static int event(dm_live_touch *t, int action, int id, float x, float y, int64_t at) {
    int32_t pointer = id;
    return dm_live_touch_event(t, action, 0, 1, &pointer, &x, &y,
                               667, 600, 0, 0, 667, 600, at);
}

int main(void) {
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, 667, 600);
    dm_live_touch *t = dm_live_touch_create(667, 600);
    CHECK(t != NULL);
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);

    dm_live_touch_visual v;
    CHECK(dm_live_touch_visual_snapshot(t, &v) == 1 && v.valid);
    CHECK(v.width == 667 && v.height == 600);
    CHECK(v.joy_radius > 0 && v.swap_radius > 0 && v.diff_radius > 0);
    CHECK(v.enabled_targets == (1u << DM_TC_COUNT) - 1);
    CHECK(v.target_x[DM_TC_MAIN] == layout.target[DM_TC_MAIN].x);
    CHECK(v.target_y[DM_TC_MAIN] == layout.target[DM_TC_MAIN].y);
    CHECK(dm_tc_hit(&layout, v.target_x[DM_TC_MAIN], v.target_y[DM_TC_MAIN]) == DM_TC_MAIN);
    CHECK(dm_tc_hit(&layout, v.target_x[DM_TC_SKILL1], v.target_y[DM_TC_SKILL1]) == DM_TC_SKILL1);

    // Empty/destroyed snapshot is never a fake visible layout.
    CHECK(dm_live_touch_visual_snapshot(NULL, &v) == 0);

    // Actual selector open publishes the pick cells; no fake static menu.
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 10, layout.diff.x, layout.diff.y, 1000000000));
    CHECK(dm_live_touch_visual_snapshot(t, &v) == 1 && v.diff_open == 1);
    CHECK(v.pick_x[0] == layout.pick[0].x && v.pick_r[7] == layout.pick[7].r);
    (void)event(t, DM_LIVE_ACTION_UP, 10, layout.diff.x, layout.diff.y, 1010000000);

    // Resize and cancel boundaries: snapshot tracks the actual core, with no buttons.
    dm_live_touch_resize(t, 800, 400);
    CHECK(dm_live_touch_visual_snapshot(t, &v) == 1 && v.width == 800 && v.height == 400);
    CHECK(dm_live_touch_cancel_all(t, 1020000000));
    CHECK(dm_live_touch_visual_snapshot(t, &v) == 1 && v.buttons == 0);

    // Destroyed owner cannot expose a snapshot (caller must drop the pointer).
    dm_live_touch_destroy(t);
    CHECK(dm_live_touch_visual_snapshot(NULL, &v) == 0);
    puts("PASS: live visual layout snapshot, target centers, difficulty pick, resize, cancel, empty");
    return 0;
}
