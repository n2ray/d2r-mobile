// #143 WSI 与 app/lifecycle 的集成桥：确保排空发生在旧 Surface 引用被释放之前，且排空失败时保留真实引用。
// 依据 app/lifecycle.c 的 surface_created/surface_destroyed 契约与 dm_wsi 的排空式关闭；纯 C，可宿主测试顺序与失败。
// #143 integration bridge between WSI and app/lifecycle: the drain happens before the old Surface reference is
// released, and a failed drain keeps the real reference. Based on app/lifecycle.c's surface_created/surface_destroyed
// contract and dm_wsi's drain-based close; plain C so host tests can check ordering and failure handling.
#pragma once

#include <stdint.h>

#include "wsi_diagnostic.h"

typedef struct {
    void *ctx;
    int (*surface_created)(void *ctx, void *window);    // 真实 dm_app_lifecycle_surface_created
    int (*surface_destroyed)(void *ctx, void *window);  // 真实 dm_app_lifecycle_surface_destroyed
    void (*trace)(void *ctx, const char *event);        // 仅用于观察顺序；可为 NULL
} dm_wsi_lifecycle_ops;

typedef struct {
    dm_wsi *wsi;
    dm_wsi_lifecycle_ops lifecycle;
    void *window;   // 生命周期当前持有的窗口令牌 | token the lifecycle currently owns
    int accepted;
} dm_wsi_bridge;

void dm_wsi_bridge_init(dm_wsi_bridge *bridge, dm_wsi *wsi, const dm_wsi_lifecycle_ops *lifecycle);

// 新窗口/重绘：窗口不同时先排空旧设备（旧引用仍被生命周期持有），再让生命周期取新引用，最后打开 WSI。
// 返回 1 = 可继续出帧；0 = 拒绝或失败，调用者不得绘制，也不得释放引用。
// New window/redraw: when the token differs, drain the old device first (the lifecycle still owns that reference),
// then let the lifecycle acquire the new one, then open WSI. Returns 1 when frames may continue; 0 rejects or fails,
// and the caller must neither draw nor release a reference.
int dm_wsi_bridge_window_ready(dm_wsi_bridge *bridge, void *window, uint32_t width, uint32_t height);

// 窗口销毁：只有排空成功才调用生命周期销毁；失败保留真实引用并返回 0。
// Window destroyed: the lifecycle release happens only after a successful drain; failure keeps the real reference and returns 0.
int dm_wsi_bridge_window_destroyed(dm_wsi_bridge *bridge, void *window);
// 尺寸变化：走真实 dm_wsi_resize（排程重建），不使用只更新尺寸的快速路径。
// Size change: goes through the real dm_wsi_resize (schedules a rebuild), never the dimensions-only fast path.
dm_wsi_status dm_wsi_bridge_window_resized(dm_wsi_bridge *bridge, uint32_t width, uint32_t height);
