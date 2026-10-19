#include "../app/view_policy.h"
#include "../app/touch_bridge.h"
#include "dm_input.h"
#include "dm_touch_controls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)

static void tap_main(dm_touch_bridge *bridge, float density) {
    dm_tc_layout layout;
    dm_touch_event e;
    dm_tc_layout_make(&layout, 480, 300);
    memset(&e, 0, sizeof(e));
    e.action = DM_TOUCH_ACTION_DOWN;
    e.action_index = 0;
    e.pointer_count = 1;
    e.time_ns = 1000000000;
    e.points[0].id = 88;
    e.points[0].x_px = layout.target[DM_TC_MAIN].x * density;
    e.points[0].y_px = layout.target[DM_TC_MAIN].y * density;
    CHECK(dm_touch_bridge_event(bridge, &e));
    CHECK(dm_touch_bridge_read(bridge).buttons == DM_BTN_A);
}

int main(void) {
    dm_view_policy policy;
    dm_view_policy_init(&policy);
    CHECK(policy.view_width_px == 667 && policy.view_height_px == 375);

    CHECK(dm_view_policy_set_view(&policy, 1440, 900, 3.0f));
    CHECK(policy.view_width_pt == 480.0f && policy.view_height_pt == 300.0f);
    /* The diagnostic readback buffer must not become the input view. */
    CHECK(dm_view_policy_set_buffer(&policy, 256, 256));
    CHECK(policy.view_width_pt == 480.0f && policy.view_height_pt == 300.0f);
    CHECK(policy.buffer_width_px == 256 && policy.buffer_height_px == 256);
    CHECK(dm_view_policy_set_buffer(&policy, 256, 256));
    CHECK(policy.buffer_generation == 2);
    CHECK(policy.view_width_pt == 480.0f && policy.view_height_pt == 300.0f);
    CHECK(policy.ime_state == DM_IME_INSETS_UNKNOWN);
    CHECK(dm_view_policy_set_ime_insets(&policy, 7, 3, 4, 1, 1, 10, 20, 30, 40));
    CHECK(policy.ime_state == DM_IME_INSETS_VISIBLE);
    CHECK(policy.ime_left_px == 10 && policy.ime_right_px == 30
          && policy.ime_available_width_px == 1400
          && policy.ime_available_height_px == 840);
    CHECK(policy.view_width_px == 1440 && policy.view_height_px == 900);
    CHECK(policy.ime_session == 7 && policy.ime_generation == 3 && policy.ime_sequence == 4);
    CHECK(dm_view_policy_set_ime_insets(&policy, 7, 3, 5, 1, 0, 0, 0, 0, 0));
    CHECK(policy.ime_state == DM_IME_INSETS_HIDDEN);
    CHECK(policy.ime_available_width_px == 1440 && policy.ime_available_height_px == 900);
    CHECK(dm_view_policy_set_ime_insets(&policy, 7, 3, 6, 0, 0, 0, 0, 0, 0));
    CHECK(policy.ime_state == DM_IME_INSETS_UNKNOWN && policy.ime_available_width_px == 0);
    CHECK(!dm_view_policy_set_ime_insets(&policy, 7, 3, 7, 1, 1, -1, 0, 0, 0));
    CHECK(!dm_view_policy_set_ime_insets(&policy, 7, 3, 7, 1, 1, 1000, 0, 1000, 0));
    CHECK(!dm_view_policy_set_ime_insets(&policy, 7, 3, 7, 0, 0, 1, 0, 0, 0));
    CHECK(policy.ime_state == DM_IME_INSETS_UNKNOWN && policy.ime_sequence == 6);

    dm_touch_bridge *bridge = dm_touch_bridge_create(policy.view_width_pt,
                                                     policy.view_height_pt,
                                                     policy.density);
    CHECK(bridge != NULL);
    tap_main(bridge, policy.density);
    dm_touch_bridge_destroy(bridge, NULL);

    /* Orientation changes the real view and must update the point domain. */
    CHECK(dm_view_policy_set_view(&policy, 900, 1440, 3.0f));
    CHECK(policy.view_width_pt == 300.0f && policy.view_height_pt == 480.0f);
    CHECK(policy.ime_state == DM_IME_INSETS_UNKNOWN && policy.ime_available_width_px == 0);
    CHECK(!dm_view_policy_set_view(&policy, 0, 1440, 3.0f));
    CHECK(!dm_view_policy_set_view(&policy, 900, 1440, 0.0f));

    printf("PASS view policy: %d checks\n", checks);
    return 0;
}
