#include "live_touch.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static int zero_bytes(const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) if (b[i]) return 0;
    return 1;
}
int main(void) {
    dm_rift_hud_t src = {0}, dst;
    src.tier = 7; src.progress = .5f; src.secs_left = 42;
    memset(&dst, 0x5a, sizeof dst);
    int on = 1;
    // This is the dm_rift_hud(...) == 0 path: output is unspecified, and the
    // production cache must clear it rather than publish sentinel bytes.
    dm_live_touch_rift_cache(&dst, &on, &src, 0);
    assert(on == 0);
    assert(zero_bytes(&dst, sizeof dst));
    memset(&dst, 0x5a, sizeof dst);
    dm_live_touch_rift_cache(&dst, &on, &src, 1);
    assert(on == 1 && dst.tier == 7 && dst.secs_left == 42);
    puts("PASS: rift cache clears no-rift sentinel and copies a valid snapshot");
}
