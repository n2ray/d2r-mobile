#include "../../host/ui_trace.h"
#include <stdio.h>

int main(void) {
    dm_ui_trace_state s = {0};
    if (dm_ui_trace_take(&s)) return 1; // opt-out is silent
    dm_ui_trace_reset(&s, 1);
    for (int i = 0; i < 32; i++) if (!dm_ui_trace_take(&s)) return 2;
    if (dm_ui_trace_take(&s)) return 3; // cap
    dm_ui_trace_reset(&s, 1); // phase-3 cancel/reset starts a fresh bounded window
    if (!dm_ui_trace_take(&s)) return 4;
    dm_ui_trace_reset(&s, 0);
    if (dm_ui_trace_take(&s)) return 5;
    puts("PASS: ui trace opt-out, 32-event cap, reset/cancel");
    return 0;
}
