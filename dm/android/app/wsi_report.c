// #147 WSI 证据报告实现：只统计实际结果，未知不编造，速率只按 queued-present 计。
// 依据 #143 的 dm_wsi_status 与 vk_swapchain 的协商字段；本文件不创建定时器，也不持有任何 GPU 对象。
// #147 evidence report implementation: counts only real results, never invents unknown values and rates only
// queued presents. Based on the #143 dm_wsi_status values and the vk_swapchain negotiation fields; this file
// creates no timer and owns no GPU object.
#include "wsi_report.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DM_WSI_REPORT_INTERVAL_NS 1000000000ull

void dm_wsi_report_init(dm_wsi_report *report, int wsi_mode, uint64_t now_ns) {
    if (!report) return;
    memset(report, 0, sizeof *report);
    report->config.wsi_mode = wsi_mode ? 1 : 0;
    report->config.format = -1;
    report->config.present_mode = -1;
    report->config.maintenance1 = -1;
    report->started_ns = now_ns;
    report->last_rate_ns = now_ns;
    report->last_status = DM_WSI_IDLE;
}

void dm_wsi_report_request(dm_wsi_report *report, uint32_t width, uint32_t height) {
    if (!report) return;
    report->config.requested_width = width;
    report->config.requested_height = height;
}

void dm_wsi_report_negotiated(dm_wsi_report *report, uint32_t width, uint32_t height,
                              int format, int present_mode, uint32_t image_count, int maintenance1) {
    if (!report) return;
    report->config.extent_width = width;
    report->config.extent_height = height;
    report->config.format = format;
    report->config.present_mode = present_mode;
    report->config.image_count = image_count;
    report->config.maintenance1 = maintenance1;
    report->dirty = 1;
}

void dm_wsi_report_result(dm_wsi_report *report, dm_wsi_status status) {
    if (!report) return;
    report->last_status = (int)status;
    switch (status) {
    case DM_WSI_OK:
        report->queued_presents++;   // 只有真实排队的 present 才计数 | only a real queued present counts
        report->dirty = 1;
        break;
    case DM_WSI_SUBOPTIMAL:
        report->queued_presents++;
        report->suboptimal++;
        report->dirty = 1;
        break;
    case DM_WSI_REBUILD:
        report->rebuild_requests++;
        report->dirty = 1;
        break;
    case DM_WSI_SURFACE_LOST:
        report->surface_lost++;
        report->dirty = 1;
        break;
    case DM_WSI_ERROR:
        report->errors++;            // 失败绝不成为有效帧 | a failure never becomes a valid frame
        report->dirty = 1;
        break;
    case DM_WSI_UNSUPPORTED:
        report->unsupported++;
        report->dirty = 1;
        break;
    case DM_WSI_IDLE:
    default:
        break;
    }
}

void dm_wsi_report_event(dm_wsi_report *report, dm_wsi_event event) {
    if (!report) return;
    switch (event) {
    case DM_WSI_EVENT_PAUSE: report->paused++; break;
    case DM_WSI_EVENT_RESUME: report->resumed++; break;
    case DM_WSI_EVENT_STOP: report->stops++; break;
    }
    report->dirty = 1;
}

int dm_wsi_report_due(dm_wsi_report *report, uint64_t now_ns) {
    if (!report) return 0;
    if (!report->started) {
        report->started = 1;
        report->last_report_ns = now_ns;
        report->dirty = 0;
        return 1;
    }
    if (!report->dirty) return 0;
    if (now_ns < report->last_report_ns) return 0;   // 时钟回退不触发 | no report on clock regression
    if (now_ns - report->last_report_ns < DM_WSI_REPORT_INTERVAL_NS) return 0;
    report->last_report_ns = now_ns;
    report->dirty = 0;
    return 1;
}

void dm_wsi_report_force(dm_wsi_report *report, uint64_t now_ns) {
    if (!report) return;
    report->started = 1;
    report->last_report_ns = now_ns;
    report->dirty = 0;
}

static const char *mode_name(const dm_wsi_report *report) {
    return report->config.wsi_mode ? "wsi-clear-swapchain" : "cpu-copy-indexed";
}

static size_t append(char *out, size_t capacity, size_t used, const char *format, ...) __attribute__((format(printf, 4, 5)));
static size_t append(char *out, size_t capacity, size_t used, const char *format, ...) {
    if (used >= capacity) return used;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(out + used, capacity - used, format, args);
    va_end(args);
    if (written < 0) return used;
    size_t next = used + (size_t)written;
    return next < capacity ? next : capacity - 1;
}

size_t dm_wsi_report_config_line(const dm_wsi_report *report, char *out, size_t capacity) {
    if (!out || capacity == 0) return 0;
    out[0] = '\0';
    if (!report) return 0;
    size_t used = 0;
    used = append(out, capacity, used, "wsi config: mode=%s requested=", mode_name(report));
    if (report->config.requested_width && report->config.requested_height)
        used = append(out, capacity, used, "%ux%u", report->config.requested_width, report->config.requested_height);
    else used = append(out, capacity, used, "unknown");
    used = append(out, capacity, used, " extent=");
    if (report->config.extent_width && report->config.extent_height)
        used = append(out, capacity, used, "%ux%u", report->config.extent_width, report->config.extent_height);
    else used = append(out, capacity, used, "unknown");
    if (report->config.format >= 0) used = append(out, capacity, used, " format=%d", report->config.format);
    else used = append(out, capacity, used, " format=unknown");
    if (report->config.present_mode >= 0) used = append(out, capacity, used, " present_mode=%d", report->config.present_mode);
    else used = append(out, capacity, used, " present_mode=unknown");
    if (report->config.image_count) used = append(out, capacity, used, " images=%u", report->config.image_count);
    else used = append(out, capacity, used, " images=unknown");
    if (report->config.maintenance1 == 1) used = append(out, capacity, used, " maintenance1=enabled");
    else if (report->config.maintenance1 == 0) used = append(out, capacity, used, " maintenance1=disabled");
    else used = append(out, capacity, used, " maintenance1=unknown");
    return used;
}

size_t dm_wsi_report_progress_line(dm_wsi_report *report, uint64_t now_ns, char *out, size_t capacity) {
    if (!out || capacity == 0) return 0;
    out[0] = '\0';
    if (!report) return 0;
    const uint64_t elapsed_ms = now_ns >= report->started_ns ? (now_ns - report->started_ns) / 1000000ull : 0;
    const uint64_t presents_delta = report->queued_presents - report->last_rate_presents;
    const uint64_t ns_delta = now_ns >= report->last_rate_ns ? now_ns - report->last_rate_ns : 0;
    double rate = ns_delta ? (double)presents_delta * 1e9 / (double)ns_delta : 0.0;
    report->last_rate_ns = now_ns;
    report->last_rate_presents = report->queued_presents;
    size_t used = 0;
    used = append(out, capacity, used, "wsi progress: elapsed_ms=%llu", (unsigned long long)elapsed_ms);
    used = append(out, capacity, used, " queued_presents=%llu", (unsigned long long)report->queued_presents);
    used = append(out, capacity, used, " rate=%.1f/s queued-present throughput (NOT display fps/latency/vsync)", rate);
    used = append(out, capacity, used, " suboptimal=%llu", (unsigned long long)report->suboptimal);
    used = append(out, capacity, used, " rebuild=%llu", (unsigned long long)report->rebuild_requests);
    used = append(out, capacity, used, " surface_lost=%llu", (unsigned long long)report->surface_lost);
    used = append(out, capacity, used, " errors=%llu", (unsigned long long)report->errors);
    used = append(out, capacity, used, " unsupported=%llu", (unsigned long long)report->unsupported);
    used = append(out, capacity, used, " paused=%llu resumed=%llu stops=%llu",
                  (unsigned long long)report->paused, (unsigned long long)report->resumed,
                  (unsigned long long)report->stops);
    used = append(out, capacity, used, " last_status=%d", report->last_status);
    return used;
}
