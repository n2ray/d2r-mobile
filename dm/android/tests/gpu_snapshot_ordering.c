// #253 consumer-order regression for the production GPU snapshot policy.
#include <stdio.h>
#include <stdlib.h>

#define DM_VK_POLICY_TEST 1
#include "../native/nvn_vulkan_snapshot_policy.h"

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

int main(void) {
    // Acquired before the current draw was encoded -> not reusable in place.
    CHECK(gpu_snapshot_inplace_allowed(6, 5) == 0);
    // Same draw, second binding: the first binding advanced last_acquired.
    CHECK(gpu_snapshot_inplace_allowed(6, 6) == 1);
    // A consumer from any earlier encoded draw may be patched after it.
    CHECK(gpu_snapshot_inplace_allowed(6, 7) == 1);

    unsetenv("DM_VK_GPU_SNAPSHOT_REUSE");
    gpu_snapshot_policy_reset_for_test();
    CHECK(gpu_snapshot_reuse_enabled() == 1);   // default unchanged
    setenv("DM_VK_GPU_SNAPSHOT_REUSE", "0", 1);
    gpu_snapshot_policy_reset_for_test();
    CHECK(gpu_snapshot_reuse_enabled() == 0);   // A/B: reuse off, safe raw fallback
    setenv("DM_VK_GPU_SNAPSHOT_REUSE", "1", 1);
    gpu_snapshot_policy_reset_for_test();
    CHECK(gpu_snapshot_reuse_enabled() == 1);

    if (failures) return 1;
    puts("PASS gpu snapshot ordering: same-draw clone, prior-consumer inplace, A/B switch");
    return 0;
}
