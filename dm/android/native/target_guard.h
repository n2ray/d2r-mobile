// Android 装配产物的现有目标标记校验：只接受 android-context-v1。
// Validate the existing target marker emitted by patch_nso for Android packages. This is a layout
// guard at the loader boundary, not a signature or a new versioning framework.
#pragma once
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#define DM_ANDROID_TARGET "android-context-v1"
#define DM_ANDROID_TARGET_LEN (sizeof(DM_ANDROID_TARGET) - 1)

static inline int dm_android_target_validate(const char *layout, char *error, size_t cap) {
    if (!layout || !*layout) {
        if (error && cap) snprintf(error, cap, "target: missing layout path");
        return -1;
    }
    const char *slash = strrchr(layout, '/');
    if (!slash || (size_t)(slash - layout) + sizeof("/target") > 1024) {
        if (error && cap) snprintf(error, cap, "target: malformed layout path");
        return -1;
    }
    char path[1024];
    size_t dir_len = (size_t)(slash - layout);
    memcpy(path, layout, dir_len);
    memcpy(path + dir_len, "/target", sizeof("/target"));
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (error && cap) snprintf(error, cap, "target: missing marker");
        return -1;
    }
    char data[64];
    size_t n = fread(data, 1, sizeof data, f);
    int extra = fgetc(f);
    fclose(f);
    if (extra != EOF || n == 0 || n > sizeof data) {
        if (error && cap) snprintf(error, cap, "target: malformed marker");
        return -1;
    }
    int has_newline = n && data[n - 1] == '\n';
    size_t text_len = has_newline ? n - 1 : n;
    int printable = 1;
    for (size_t i = 0; i < text_len; ++i) if (data[i] < 0x20 || data[i] > 0x7e) printable = 0;
    if (!printable || (n > 1 && memchr(data, 0, n))) {
        if (error && cap) snprintf(error, cap, "target: malformed marker");
        return -1;
    }
    if (text_len != DM_ANDROID_TARGET_LEN || memcmp(data, DM_ANDROID_TARGET, DM_ANDROID_TARGET_LEN)) {
        if (error && cap) snprintf(error, cap, "target: wrong marker");
        return -1;
    }
    return 0;
}
