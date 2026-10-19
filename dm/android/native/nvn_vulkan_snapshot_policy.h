// #253 GPU snapshot consumer-ordering policy. The producer records a snapshot
// for a draw/dispatch and advances the encoded serial only after the real
// outer draw/dispatch is recorded. A cached buffer may be patched in place
// only when every consumer that has touched it is already encoded; a second
// binding in the same draw must fork a clone instead.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int gpu_snapshot_inplace_allowed(uint64_t last_acquired, uint64_t encoded) {
    return last_acquired <= encoded;
}

static int gpu_snapshot_reuse_cached = -1;

static int gpu_snapshot_reuse_enabled(void) {
    if (gpu_snapshot_reuse_cached < 0) {
        const char *value = getenv("DM_VK_GPU_SNAPSHOT_REUSE");
        gpu_snapshot_reuse_cached = !(value && !strcmp(value, "0"));
    }
    return gpu_snapshot_reuse_cached;
}

#ifdef DM_VK_POLICY_TEST
static void gpu_snapshot_policy_reset_for_test(void) { gpu_snapshot_reuse_cached = -1; }
#endif
