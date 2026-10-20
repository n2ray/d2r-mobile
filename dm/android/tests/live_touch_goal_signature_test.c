#include "live_touch.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    dm_map_goal_view g; memset(&g, 0, sizeof g);
    g.n = 2; g.px = 10; g.py = 20; g.sx = .4f; g.sy = .6f;
    g.x[0] = 11; g.y[0] = 22; g.art[0] = 5;
    float mm[4] = {.65f, .02f, .90f, .35f}, cell = 2.f;
    uint64_t base = dm_live_touch_goal_signature(1469598103934665603ull, &g, 1, 1, cell, mm, 1, 8, 0);
    assert(base == dm_live_touch_goal_signature(1469598103934665603ull, &g, 1, 1, cell, mm, 1, 8, 0));
    g.sx += .01f;
    assert(base != dm_live_touch_goal_signature(1469598103934665603ull, &g, 1, 1, cell, mm, 1, 8, 0));
    g.sx = .4f; mm[2] += .01f;
    assert(base != dm_live_touch_goal_signature(1469598103934665603ull, &g, 1, 1, cell, mm, 1, 8, 0));
    mm[2] = .90f; g.x[0] += 1;
    assert(base != dm_live_touch_goal_signature(1469598103934665603ull, &g, 1, 1, cell, mm, 1, 8, 0));
    puts("PASS: goal signature includes sx/sy, minimap rect and goal IDs; unchanged is stable");
}
