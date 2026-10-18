// #147 WSI 证据报告宿主测试：真实统计/格式化边界，确定性时间戳；失败不计为有效帧。
// 依据 app/wsi_report.h 契约；负向对照覆盖"失败被当成成功"与"未知被编造"两类回归。
// #147 host tests for the WSI evidence report: real accounting/formatting with deterministic timestamps; a failure is
// never a valid frame. Based on the app/wsi_report.h contract; negative controls cover "failure counted as success"
// and "unknown invented".
#include "../app/wsi_report.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void test_config_lines(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 1000);
    char line[512];
    // 协商前：extent/format/present/images/maintenance 全是 unknown，不编造。
    // Before negotiation every negotiated field stays unknown, never invented.
    dm_wsi_report_config_line(&r, line, sizeof line);
    CHECK(strstr(line, "mode=wsi-clear-swapchain") != NULL);
    CHECK(strstr(line, "extent=unknown") != NULL);
    CHECK(strstr(line, "format=unknown") != NULL);
    CHECK(strstr(line, "present_mode=unknown") != NULL);
    CHECK(strstr(line, "images=unknown") != NULL);
    CHECK(strstr(line, "maintenance1=unknown") != NULL);
    CHECK(strstr(line, "requested=unknown") != NULL);

    dm_wsi_report_request(&r, 1440, 3200);
    dm_wsi_report_negotiated(&r, 1440, 3200, 37, 2, 4, 1);
    dm_wsi_report_config_line(&r, line, sizeof line);
    CHECK(strstr(line, "requested=1440x3200") != NULL);
    CHECK(strstr(line, "extent=1440x3200") != NULL);
    CHECK(strstr(line, "format=37") != NULL);
    CHECK(strstr(line, "present_mode=2") != NULL);
    CHECK(strstr(line, "images=4") != NULL);
    CHECK(strstr(line, "maintenance1=enabled") != NULL);

    // 请求与实际不同时两者都必须出现。| Requested and actual extents both appear when they differ.
    dm_wsi_report_negotiated(&r, 1280, 720, 37, 2, 3, 0);
    dm_wsi_report_config_line(&r, line, sizeof line);
    CHECK(strstr(line, "requested=1440x3200") != NULL);
    CHECK(strstr(line, "extent=1280x720") != NULL);
    CHECK(strstr(line, "maintenance1=disabled") != NULL);

    // 默认 CPU 模式标签。| Default CPU-mode label.
    dm_wsi_report cpu;
    dm_wsi_report_init(&cpu, 0, 0);
    dm_wsi_report_config_line(&cpu, line, sizeof line);
    CHECK(strstr(line, "mode=cpu-copy-indexed") != NULL);
}

static void test_counters_never_count_failure(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    dm_wsi_report_result(&r, DM_WSI_OK);
    dm_wsi_report_result(&r, DM_WSI_OK);
    dm_wsi_report_result(&r, DM_WSI_SUBOPTIMAL);
    CHECK(r.queued_presents == 3);
    CHECK(r.suboptimal == 1);

    // 负向对照：失败绝不增加 queued_presents。| Negative control: a failure never increments queued presents.
    dm_wsi_report_result(&r, DM_WSI_ERROR);
    dm_wsi_report_result(&r, DM_WSI_UNSUPPORTED);
    CHECK(r.queued_presents == 3);
    CHECK(r.errors == 1);
    CHECK(r.unsupported == 1);

    dm_wsi_report_result(&r, DM_WSI_REBUILD);
    dm_wsi_report_result(&r, DM_WSI_SURFACE_LOST);
    dm_wsi_report_result(&r, DM_WSI_IDLE);
    CHECK(r.rebuild_requests == 1);
    CHECK(r.surface_lost == 1);
    CHECK(r.queued_presents == 3);

    dm_wsi_report_event(&r, DM_WSI_EVENT_PAUSE);
    dm_wsi_report_event(&r, DM_WSI_EVENT_RESUME);
    dm_wsi_report_event(&r, DM_WSI_EVENT_STOP);
    CHECK(r.paused == 1 && r.resumed == 1 && r.stops == 1);

    // 进度行把失败与成功分开，并把速率标成 queued-present。| The progress line separates failures and labels the rate.
    char line[512];
    dm_wsi_report_progress_line(&r, 2000000000ull, line, sizeof line);
    CHECK(strstr(line, "queued_presents=3") != NULL);
    CHECK(strstr(line, "errors=1") != NULL);
    CHECK(strstr(line, "surface_lost=1") != NULL);
    CHECK(strstr(line, "queued-present throughput") != NULL);
    CHECK(strstr(line, "NOT display fps/latency/vsync") != NULL);
    CHECK(strstr(line, "display_fps") == NULL);
}

static void test_bounded_rate(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 1000000000ull);
    // 首次调用给出初始快照。| The first call provides the initial snapshot.
    CHECK(dm_wsi_report_due(&r, 1000000000ull) == 1);
    // 无新进展：即使过了一秒也不重复。| No new progress: no repeat even after a second.
    CHECK(dm_wsi_report_due(&r, 2000000000ull) == 0);
    // 有新进展但距上次报告不足 1 秒：不触发。| New progress under one second since the last report: not due.
    dm_wsi_report_result(&r, DM_WSI_OK);
    CHECK(dm_wsi_report_due(&r, 1200000000ull) == 0);
    // 满 1 秒：触发一次并清 dirty。| At one second: one report, dirty cleared.
    CHECK(dm_wsi_report_due(&r, 2000000000ull) == 1);
    CHECK(dm_wsi_report_due(&r, 2000000001ull) == 0);
    // 时钟回退不触发。| Clock regression does not report.
    dm_wsi_report_result(&r, DM_WSI_OK);
    CHECK(dm_wsi_report_due(&r, 1900000000ull) == 0);
    // 继续进展后再次到期。| Later progress becomes due again.
    CHECK(dm_wsi_report_due(&r, 3000000000ull) == 1);
    // 强制快照不受限制，并清 dirty。| Forced snapshots are not rate limited and clear dirty.
    dm_wsi_report_force(&r, 3000000001ull);
    CHECK(dm_wsi_report_due(&r, 3000000002ull) == 0);
}

static void test_inactive_periods(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    CHECK(dm_wsi_report_due(&r, 0) == 1);
    // 暂停/停顿期间没有任何结果：10 秒内不产生任何报告（没有空转输出）。
    // No results while paused/idle: ten seconds produce no report (no idle spam).
    for (uint64_t t = 1000000000ull; t <= 10000000000ull; t += 1000000000ull)
        CHECK(dm_wsi_report_due(&r, t) == 0);
    // 停顿后的第一个真实进展立即产生一行，随后仍限制在每秒一行。
    // The first real progress after the pause reports immediately, then stays at most one line per second.
    dm_wsi_report_result(&r, DM_WSI_OK);
    CHECK(dm_wsi_report_due(&r, 10500000000ull) == 1);
    CHECK(dm_wsi_report_due(&r, 10600000000ull) == 0);
    dm_wsi_report_result(&r, DM_WSI_OK);
    CHECK(dm_wsi_report_due(&r, 11400000000ull) == 0);
    CHECK(dm_wsi_report_due(&r, 11600000000ull) == 1);
}

static void test_capacity_truncation(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    dm_wsi_report_result(&r, DM_WSI_OK);
    char small[48];
    size_t used = dm_wsi_report_progress_line(&r, 1000000000ull, small, sizeof small);
    CHECK(used < sizeof small);
    CHECK(small[sizeof small - 1] == '\0');
}

int main(void) {
    test_config_lines();
    test_counters_never_count_failure();
    test_bounded_rate();
    test_inactive_periods();
    test_capacity_truncation();
    printf("wsi_report: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
