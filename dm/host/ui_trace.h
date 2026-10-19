// Opt-in, bounded front-end touch tracing. This is diagnostic-only: it never
// changes touch phases, coordinates or host mouse delivery.
#pragma once
#include <stdatomic.h>
#include <stdint.h>

typedef struct {
    atomic_int enabled;
    atomic_uint emitted;
} dm_ui_trace_state;

#define DM_UI_TRACE_LIMIT 32u

static inline void dm_ui_trace_reset(dm_ui_trace_state *s, int on) {
    atomic_store(&s->enabled, on ? 1 : 0);
    atomic_store(&s->emitted, 0);
}

static inline int dm_ui_trace_take(dm_ui_trace_state *s) {
    if (!atomic_load(&s->enabled)) return 0;
    unsigned n = atomic_fetch_add(&s->emitted, 1);
    return n < DM_UI_TRACE_LIMIT;
}
