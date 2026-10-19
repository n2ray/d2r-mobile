// #255 caller geometry fixture: 3200x1440 phone surface, centered front-end
// 16:9, exact 16:9 and invalid surfaces.
#include "../native/live_touch_geometry.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    dm_live_touch_geometry g = dm_live_touch_geometry_for(3200, 1440, 1);
    CHECK(g.valid);
    CHECK(g.left == 0 && g.top == 0 && g.width == 3200 && g.height == 1440);
    CHECK(g.logical_width == 667.0f);
    CHECK(g.logical_height > 299.0f && g.logical_height < 301.0f);   // 667 * 1440 / 3200 = 300.15

    // A finger on the right/bottom edge reaches the logical edge; it is not
    // clamped into the old centered 16:9 picture.
    float nx = 3199.0f / g.width, ny = 1439.0f / g.height;
    CHECK(nx * g.logical_width > 666.0f && ny * g.logical_height > 299.0f);

    g = dm_live_touch_geometry_for(3200, 1440, 0);
    CHECK(g.valid);
    CHECK(g.left == 320 && g.top == 0 && g.width == 2560 && g.height == 1440);
    CHECK(g.logical_height > 374.0f && g.logical_height < 376.0f);

    g = dm_live_touch_geometry_for(1920, 1080, 0);
    CHECK(g.valid && g.left == 0 && g.top == 0 && g.width == 1920 && g.height == 1080);
    g = dm_live_touch_geometry_for(1920, 1080, 1);
    CHECK(g.valid && g.left == 0 && g.top == 0 && g.width == 1920 && g.height == 1080);

    CHECK(!dm_live_touch_geometry_for(0, 0, 1).valid);
    CHECK(!dm_live_touch_geometry_for(319, 240, 0).valid);
    CHECK(!dm_live_touch_geometry_for(320, 239, 0).valid);
    CHECK(!dm_live_touch_geometry_for(16385, 720, 1).valid);
    puts("PASS: caller geometry preserves in-run phone aspect and centered front-end 16:9; invalid surfaces stay neutral");
    return 0;
}
