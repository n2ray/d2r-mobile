// PR #113 repair regressions: shader/begin error propagation, output
// preservation, owned-object balance and numeric boundary preflight.
#include "touch_hud.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); return 1; } checks++; } while (0)

// New source provides a strong definition under DM_TOUCH_HUD_TEST_ACCOUNTING;
// the weak fallback lets this same test link against the old source.
__attribute__((weak)) int dm_touch_hud_test_live_objects(void) { return 0; }

// Source is compiled with -Ddm_vk_begin=dm_touch_hud_begin_test.
#undef dm_vk_begin
VkResult dm_vk_begin(dm_vk *vk, VkCommandBuffer *out);
static int fail_begin;
VkResult dm_touch_hud_begin_test(dm_vk *vk, VkCommandBuffer *out)
{
    if (!fail_begin) return dm_vk_begin(vk, out);
    (void)vk; (void)out;
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

static unsigned char *alloc(dm_vk_pixels *p)
{
    p->width = 1334; p->height = 750; p->capacity = 1334u * 750u * 4u;
    p->bytes = malloc(p->capacity);
    if (p->bytes) memset(p->bytes, 0x5a, p->capacity);
    return p->bytes;
}

static int unchanged(const dm_vk_pixels *p, const unsigned char *snapshot)
{
    return memcmp(p->bytes, snapshot, p->capacity) == 0;
}

static int bad_case(const char *name, dm_touch_hud_snapshot s, dm_vk *vk,
                    dm_vk_pixels *p, const unsigned char *snapshot,
                    const char *vert, const char *frag)
{
    if (dm_touch_hud_validate(&s)) {
        fprintf(stderr, "FAIL %s: validate accepted\n", name);
        return 0;
    }
    dm_touch_hud_geometry g[DM_TC_COUNT];
    dm_touch_hud_joy_geometry joy;
    if (dm_touch_hud_build_geometry(&s, g, &joy)) {
        fprintf(stderr, "FAIL %s: geometry accepted\n", name);
        return 0;
    }
    if (dm_touch_hud_render(vk, &s, p, 0, vert, frag)) {
        fprintf(stderr, "FAIL %s: render accepted\n", name);
        return 0;
    }
    if (!unchanged(p, snapshot)) {
        fprintf(stderr, "FAIL %s: output changed\n", name);
        return 0;
    }
    checks++;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s hud.vert.spv hud.frag.spv\n", argv[0]); return 2; }
    dm_touch_hud_snapshot s = {
        .width_pt = 667, .height_pt = 375, .density = 2,
        .pixel_width = 1334, .pixel_height = 750,
        .available_mask = (1u << DM_TC_COUNT) - 1u,
        .enabled_mask = (1u << DM_TC_COUNT) - 1u,
        .pressed_mask = 1u << DM_TC_MAIN,
    };
    dm_vk vk;
    if (dm_vk_create(&vk) != VK_SUCCESS) {
        fprintf(stderr, "VULKAN UNAVAILABLE: fault tests skipped, no fake pass\n");
        return 77;
    }
    dm_vk_pixels px = {0};
    CHECK(alloc(&px));
    unsigned char *before = malloc(px.capacity);
    CHECK(before);
    memcpy(before, px.bytes, px.capacity);

    // Baseline: a successful render creates and destroys every owned object.
    CHECK(dm_touch_hud_render(&vk, &s, &px, 0, argv[1], argv[2]));
    CHECK(dm_touch_hud_test_live_objects() == 0);

    // Missing vertex/fragment shader paths propagate their real failures.
    if (!getenv("HUD_FAULT_BEGIN_ONLY") && !getenv("HUD_FAULT_NUMERIC_ONLY")) {
    memset(px.bytes, 0x5a, px.capacity);
    memcpy(before, px.bytes, px.capacity);
    CHECK(!dm_touch_hud_render(&vk, &s, &px, 0, "/nonexistent-vertex.spv", argv[2]));
    CHECK(unchanged(&px, before));
    CHECK(dm_touch_hud_test_live_objects() == 0);
    CHECK(!dm_touch_hud_render(&vk, &s, &px, 0, argv[1], "/nonexistent-fragment.spv"));
    CHECK(unchanged(&px, before));
    CHECK(dm_touch_hud_test_live_objects() == 0);
    }

    // Command-begin failure after all render objects and both buffers exist.
    if (!getenv("HUD_FAULT_NUMERIC_ONLY")) {
    fail_begin = 1;
    CHECK(!dm_touch_hud_render(&vk, &s, &px, 0, argv[1], argv[2]));
    CHECK(unchanged(&px, before));
    CHECK(dm_touch_hud_test_live_objects() == 0);
    }

    // Numeric boundaries: reject before float->uint casts or GPU allocation.
    dm_touch_hud_snapshot b = s;
    b.width_pt = INFINITY; CHECK(bad_case("+inf width", b, &vk, &px, before, argv[1], argv[2]));
    b = s; b.width_pt = -INFINITY; CHECK(bad_case("-inf width", b, &vk, &px, before, argv[1], argv[2]));
    b = s; b.width_pt = NAN; CHECK(bad_case("nan width", b, &vk, &px, before, argv[1], argv[2]));
    b = s; b.height_pt = INFINITY; CHECK(bad_case("+inf height", b, &vk, &px, before, argv[1], argv[2]));
    b = s; b.height_pt = NAN; CHECK(bad_case("nan height", b, &vk, &px, before, argv[1], argv[2]));
    b = s; b.width_pt = FLT_MAX; CHECK(bad_case("FLT_MAX width", b, &vk, &px, before, argv[1], argv[2]));
    b = s; b.height_pt = FLT_MAX; CHECK(bad_case("FLT_MAX height", b, &vk, &px, before, argv[1], argv[2]));
    b = s; b.width_pt = 1.0e10f; b.pixel_width = 1;
    CHECK(bad_case("huge point width", b, &vk, &px, before, argv[1], argv[2]));
    b = s; b.width_pt = 1.0f; b.density = FLT_MAX; b.pixel_width = 1;
    CHECK(bad_case("product overflow", b, &vk, &px, before, argv[1], argv[2]));
    b = s; b.width_pt = 20000.0f; b.pixel_width = 1;
    CHECK(bad_case("width over MAX_DIM", b, &vk, &px, before, argv[1], argv[2]));
    b = s; b.height_pt = 20000.0f; b.pixel_height = 1;
    CHECK(bad_case("height over MAX_DIM", b, &vk, &px, before, argv[1], argv[2]));

    free(before); free(px.bytes);
    dm_vk_destroy(&vk);
    printf("touch HUD faults: %d checks\n", checks);
    return 0;
}
