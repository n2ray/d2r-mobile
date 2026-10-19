#include "live_touch_glyph.h"
#include "../../host/dm_input.h"

static dm_ta_glyph shown(const dm_tg_snapshot *s, uint64_t button,
                         uint64_t now, int set) {
    dm_ta_glyph g = dm_ta_current_set(s, button, now, set);
    // Visual-only stale policy from DmTouchOverlay.m: keep drawing the last
    // captured glyph when a live snapshot merely aged out. Scene changes clear
    // captured_ns in the bridge and still produce the unknown placeholder.
    if (g.status == DM_TA_UNKNOWN && s && s->captured_ns && now > s->captured_ns &&
        now - s->captured_ns > DM_TG_MAX_AGE_NS)
        g = dm_ta_current_set(s, button, s->captured_ns, set);
    return g;
}

int dm_live_touch_skill_glyphs(const dm_live_touch *touch,
                               const dm_tg_snapshot *snapshot,
                               uint64_t host_now_ns,
                               dm_live_touch_glyph out[5]) {
    if (!out) return 0;
    for (int i = 0; i < 5; ++i)
        out[i] = (dm_live_touch_glyph){DM_LIVE_GLYPH_UNKNOWN, -1, -1, -1, UINT32_MAX};
    if (!touch || !snapshot) return 0;
    int set = dm_live_touch_skill_set(touch);
    for (int i = 0; i < 5; ++i) {
        uint64_t button = dm_live_touch_target_button(touch, DM_TC_MAIN + i);
        if (!button) continue;
        dm_ta_glyph g = shown(snapshot, button, host_now_ns, set);
        out[i].status = g.status;
        out[i].atlas = g.atlas;
        out[i].normal_frame = g.normal_frame;
        out[i].pressed_frame = g.pressed_frame;
        out[i].raw_skill_id = g.raw_skill_id;
    }
    return 1;
}

uint64_t dm_live_touch_glyph_signature(const dm_live_touch *touch,
                                       const dm_tg_snapshot *snapshot,
                                       uint64_t host_now_ns) {
    dm_live_touch_glyph g[5];
    uint64_t h = 1469598103934665603ull;
    dm_live_touch_skill_glyphs(touch, snapshot, host_now_ns, g);
    for (int i = 0; i < 5; ++i) {
        const uint64_t parts[] = {(uint64_t)(uint32_t)g[i].status,
            (uint64_t)(uint32_t)g[i].atlas, (uint64_t)(uint32_t)g[i].normal_frame,
            (uint64_t)(uint32_t)g[i].pressed_frame, (uint64_t)g[i].raw_skill_id};
        for (unsigned j = 0; j < sizeof parts / sizeof parts[0]; ++j)
            h = (h ^ parts[j]) * 1099511628211ull;
    }
    return h;
}
