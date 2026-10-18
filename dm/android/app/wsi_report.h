// #147 显式 WSI 诊断的有界证据报告：协商配置、实际提交计数、生命周期事件与至多 1 Hz 的进度行。
// 依据 #143 的 WSI 控制器/翻译器语义；纯 C（无 Vulkan/Android 头），宿主可用注入时间戳确定性测试。
// #147 bounded evidence report for the explicit WSI diagnostic: negotiated configuration, actual present counts,
// lifecycle events and at most one progress line per second. Based on the #143 WSI controller/translator semantics;
// plain C (no Vulkan/Android headers) so host tests can drive it with injected timestamps.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "wsi_diagnostic.h"

typedef enum {
    DM_WSI_EVENT_PAUSE = 0,
    DM_WSI_EVENT_RESUME,
    DM_WSI_EVENT_STOP
} dm_wsi_event;

// 协商事实：请求尺寸与实际 extent 分开；未知保持 unknown（format/present=-1，尺寸 0，images 0，maintenance1 -1）。
// Negotiated facts: requested and actual extents are separate; unknown stays unknown (format/present -1, extent 0,
// images 0, maintenance1 -1).
typedef struct {
    int wsi_mode;              // 1 = wsi-clear-swapchain，0 = 默认 cpu-copy-indexed | renderer mode label
    uint32_t requested_width, requested_height;
    uint32_t extent_width, extent_height;
    int format;
    int present_mode;
    uint32_t image_count;
    int maintenance1;          // -1 未知 / 0 未启用 / 1 已启用 | unknown / disabled / enabled
} dm_wsi_report_config;

typedef struct {
    dm_wsi_report_config config;
    uint64_t started_ns;
    uint64_t last_report_ns;
    uint64_t last_rate_ns;
    uint64_t last_rate_presents;
    uint64_t queued_presents;   // 成功排队的 present，绝不代表物理显示完成 | queued presents, never display completion
    uint64_t suboptimal;
    uint64_t rebuild_requests;  // OUT_OF_DATE 等重建请求 | rebuild requests such as OUT_OF_DATE
    uint64_t surface_lost;
    uint64_t errors;            // 失败路径，绝不累加 queued_presents | failure paths, never counted as presents
    uint64_t unsupported;
    uint64_t paused, resumed, stops;
    int last_status;
    int started;
    int dirty;
} dm_wsi_report;

void dm_wsi_report_init(dm_wsi_report *report, int wsi_mode, uint64_t now_ns);
// 请求尺寸（窗口回调给出的尺寸）。| Requested size, as reported by the window callback.
void dm_wsi_report_request(dm_wsi_report *report, uint32_t width, uint32_t height);
// 实际协商结果（来自 swapchain 与设备能力）。| Actual negotiation result from the swapchain and device capability.
void dm_wsi_report_negotiated(dm_wsi_report *report, uint32_t width, uint32_t height,
                              int format, int present_mode, uint32_t image_count, int maintenance1);
// 每次真实帧结果登记；ERROR/UNSUPPORTED 只加错误计数，绝不成为有效帧。
// Records a real frame result; ERROR/UNSUPPORTED increment only their counters and never become a valid frame.
void dm_wsi_report_result(dm_wsi_report *report, dm_wsi_status status);
void dm_wsi_report_event(dm_wsi_report *report, dm_wsi_event event);

// 至多每秒一次且只在实际有新进展时触发；首次调用返回 1（初始快照）。不创建定时器。
// At most once per second and only when real progress exists since the last report; the first call reports the
// initial snapshot. Creates no timer.
int dm_wsi_report_due(dm_wsi_report *report, uint64_t now_ns);
// 强制一次快照（pause/窗口丢失/stop/错误），不受 1 Hz 限制。| Forced snapshot (pause/window loss/stop/error), not rate limited.
void dm_wsi_report_force(dm_wsi_report *report, uint64_t now_ns);

// 格式化：未知值写 unknown；吞吐只标 queued-present。
// Formatting: unknown values print as unknown; any rate is labelled queued-present.
size_t dm_wsi_report_config_line(const dm_wsi_report *report, char *out, size_t capacity);
size_t dm_wsi_report_progress_line(dm_wsi_report *report, uint64_t now_ns, char *out, size_t capacity);
