#include "view_policy.h"

#include <math.h>

void dm_view_policy_init(dm_view_policy *policy) {
    if (!policy) return;
    policy->view_width_px = 667;
    policy->view_height_px = 375;
    policy->buffer_width_px = 0;
    policy->buffer_height_px = 0;
    policy->buffer_generation = 0;
    policy->density = 1.0f;
    policy->view_width_pt = 667.0f;
    policy->view_height_pt = 375.0f;
    dm_view_policy_clear_ime_insets(policy);
}

void dm_view_policy_clear_ime_insets(dm_view_policy *policy) {
    if (!policy) return;
    policy->ime_state = DM_IME_INSETS_UNKNOWN;
    policy->ime_left_px = 0;
    policy->ime_top_px = 0;
    policy->ime_right_px = 0;
    policy->ime_bottom_px = 0;
    policy->ime_available_width_px = 0;
    policy->ime_available_height_px = 0;
    policy->ime_session = 0;
    policy->ime_generation = 0;
    policy->ime_sequence = 0;
}

int dm_view_policy_set_view(dm_view_policy *policy, uint32_t width_px,
                            uint32_t height_px, float density) {
    if (!policy || width_px == 0 || height_px == 0 || !isfinite(density) ||
        density <= 0.0f) return 0;
    if (policy->view_width_px != width_px || policy->view_height_px != height_px ||
        policy->density != density) {
        dm_view_policy_clear_ime_insets(policy);
    }
    policy->view_width_px = width_px;
    policy->view_height_px = height_px;
    policy->density = density;
    policy->view_width_pt = (float)width_px / density;
    policy->view_height_pt = (float)height_px / density;
    return 1;
}

int dm_view_policy_set_ime_insets(dm_view_policy *policy, uint64_t session,
                                  uint64_t generation, uint64_t sequence,
                                  int known, int visible,
                                  int32_t left, int32_t top, int32_t right,
                                  int32_t bottom) {
    if (!policy || session == 0 || generation == 0 || sequence == 0) return 0;
    if (left < 0 || top < 0 || right < 0 || bottom < 0) return 0;
    if (known != 0 && known != 1) return 0;
    if (visible != 0 && visible != 1) return 0;
    if (!known && (visible || left || top || right || bottom)) return 0;
    if (known && !visible && (left || top || right || bottom)) return 0;
    if (known && visible) {
        uint64_t width = policy->view_width_px;
        uint64_t height = policy->view_height_px;
        if ((uint64_t)left + (uint64_t)right > width) return 0;
        if ((uint64_t)top + (uint64_t)bottom > height) return 0;
    }
    dm_ime_insets_state next;
    uint32_t available_width = 0, available_height = 0;
    if (!known) {
        next = DM_IME_INSETS_UNKNOWN;
    } else if (!visible) {
        next = DM_IME_INSETS_HIDDEN;
        available_width = policy->view_width_px;
        available_height = policy->view_height_px;
    } else {
        next = DM_IME_INSETS_VISIBLE;
        available_width = policy->view_width_px - ((uint32_t)left + (uint32_t)right);
        available_height = policy->view_height_px - ((uint32_t)top + (uint32_t)bottom);
    }
    policy->ime_state = next;
    policy->ime_left_px = left;
    policy->ime_top_px = top;
    policy->ime_right_px = right;
    policy->ime_bottom_px = bottom;
    policy->ime_available_width_px = available_width;
    policy->ime_available_height_px = available_height;
    policy->ime_session = session;
    policy->ime_generation = generation;
    policy->ime_sequence = sequence;
    return 1;
}

int dm_view_policy_set_buffer(dm_view_policy *policy, uint32_t width_px,
                              uint32_t height_px) {
    if (!policy || width_px == 0 || height_px == 0) return 0;
    policy->buffer_width_px = width_px;
    policy->buffer_height_px = height_px;
    policy->buffer_generation++;
    return 1;
}
