// NativeActivity view-bound policy: input/render geometry is the real view,
// never the diagnostic Vulkan readback buffer. Host-testable and bounded.
#pragma once

#include <stdint.h>

typedef enum dm_ime_insets_state {
    DM_IME_INSETS_UNKNOWN = 0,
    DM_IME_INSETS_HIDDEN = 1,
    DM_IME_INSETS_VISIBLE = 2
} dm_ime_insets_state;

typedef struct dm_view_policy {
    uint32_t view_width_px;
    uint32_t view_height_px;
    uint32_t buffer_width_px;
    uint32_t buffer_height_px;
    uint32_t buffer_generation;
    float density;
    float view_width_pt;
    float view_height_pt;
    /* Diagnostic IME consumer, separate from the real view/input geometry. */
    dm_ime_insets_state ime_state;
    int32_t ime_left_px;
    int32_t ime_top_px;
    int32_t ime_right_px;
    int32_t ime_bottom_px;
    uint32_t ime_available_width_px;
    uint32_t ime_available_height_px;
    uint64_t ime_session;
    uint64_t ime_generation;
    uint64_t ime_sequence;
} dm_view_policy;

void dm_view_policy_init(dm_view_policy *policy);
// Sets the physical view bounds and density. Returns 0 for zero/non-finite
// values; on success recomputes logical point bounds. A changed view clears the
// diagnostic IME rectangle because old insets are not valid for new bounds.
int dm_view_policy_set_view(dm_view_policy *policy, uint32_t width_px,
                            uint32_t height_px, float density);
// Records the actual render buffer for diagnostics. It never changes the view
// bounds or logical input domain.
int dm_view_policy_set_buffer(dm_view_policy *policy, uint32_t width_px,
                              uint32_t height_px);
// Applies one validated IME observation to the independent diagnostic consumer.
// Unknown/hidden observations keep explicit states; invalid geometry returns 0
// without changing the previous state. It never changes view/input bounds.
int dm_view_policy_set_ime_insets(dm_view_policy *policy, uint64_t session,
                                  uint64_t generation, uint64_t sequence,
                                  int known, int visible,
                                  int32_t left, int32_t top, int32_t right,
                                  int32_t bottom);
void dm_view_policy_clear_ime_insets(dm_view_policy *policy);
