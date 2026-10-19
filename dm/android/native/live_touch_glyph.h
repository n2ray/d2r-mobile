// Read-only skill glyph presentation seam. The original iOS mapping
// (dm_ta_current_set) is authoritative; Android only copies the resulting
// status/atlas/frame indices for the draw-only overlay.
#pragma once
#include <stdint.h>
#include "live_touch.h"
#include "../../ios/Sources/dm_touch_native_art.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { DM_LIVE_GLYPH_UNKNOWN = 0, DM_LIVE_GLYPH_EMPTY = 1, DM_LIVE_GLYPH_GLYPH = 2 };
typedef struct {
    int status, atlas, normal_frame, pressed_frame;
    uint32_t raw_skill_id;
} dm_live_touch_glyph;

// Five entries in core target order: main, F1..F4. Stale snapshots follow the
// original visual-only policy (draw the last captured glyph); input freshness
// is not changed here. No allocation and no input mutation.
int dm_live_touch_skill_glyphs(const dm_live_touch *touch,
                               const dm_tg_snapshot *snapshot,
                               uint64_t host_now_ns,
                               dm_live_touch_glyph out[5]);
uint64_t dm_live_touch_glyph_signature(const dm_live_touch *touch,
                                       const dm_tg_snapshot *snapshot,
                                       uint64_t host_now_ns);

#ifdef __cplusplus
}
#endif
