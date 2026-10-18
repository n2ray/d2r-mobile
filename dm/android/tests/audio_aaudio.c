// #149 actual dm_audio production module with only AAudio boundaries stubbed.
// Covers sample order, partial/overflow/underrun, lifecycle failures, disconnect and callback retirement.
#include "../audio/dm_aaudio.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

struct dm_aaudio_builder {
    int32_t format, channels, rate, direction;
    dm_audio_data_callback data;
    dm_audio_error_callback error;
    void *user;
};
struct dm_aaudio_stream {
    int state;
    int32_t format, channels, rate;
    dm_audio_data_callback data;
    void *user;
    dm_audio_error_callback error;
};
static dm_audio_data_callback g_cb;
static void *g_cb_user;
static dm_audio_error_callback g_err;
static void *g_err_user;
static int g_open_fail, g_mismatch, g_start_fail, g_pause_fail, g_stop_fail, g_close_fail, g_wait_fail;
static int g_close_calls, g_delete_builder_calls, g_stop_calls;
static int g_callback_destroy_result;

static dm_audio_result f_create_builder(dm_aaudio_builder **b) { *b = calloc(1, sizeof **b); return *b ? 0 : -12; }
static dm_audio_result f_delete_builder(dm_aaudio_builder *b) { g_delete_builder_calls++; free(b); return 0; }
static void f_set_format(dm_aaudio_builder *b, int32_t v) { b->format = v; }
static void f_set_channels(dm_aaudio_builder *b, int32_t v) { b->channels = v; }
static void f_set_rate(dm_aaudio_builder *b, int32_t v) { b->rate = v; }
static void f_set_direction(dm_aaudio_builder *b, int32_t v) { b->direction = v; }
static void f_set_data(dm_aaudio_builder *b, dm_audio_data_callback cb, void *u) { b->data = cb; b->user = u; }
static void f_set_error(dm_aaudio_builder *b, dm_audio_error_callback cb, void *u) { b->error = cb; (void)u; }
static dm_audio_result f_open(dm_aaudio_builder *b, dm_aaudio_stream **s) {
    if (g_open_fail) return -5;
    dm_aaudio_stream *x = calloc(1, sizeof *x);
    if (!x) return -12;
    x->state = 2; x->format = b->format; x->channels = b->channels; x->rate = b->rate;
    x->data = b->data; x->user = b->user; x->error = b->error;
    if (g_mismatch) x->rate = 44100;
    *s = x; g_cb = x->data; g_cb_user = x->user; g_err = x->error; g_err_user = b->user; return 0;
}
static dm_audio_result f_close(dm_aaudio_stream *s) { g_close_calls++; if (g_close_fail) return -5; free(s); return 0; }
static dm_audio_result f_start(dm_aaudio_stream *s) { if (g_start_fail) return -5; s->state = 4; return 0; }
static dm_audio_result f_pause(dm_aaudio_stream *s) { if (g_pause_fail) return -5; s->state = 6; return 0; }
static dm_audio_result f_stop(dm_aaudio_stream *s) { g_stop_calls++; if (g_stop_fail) return -5; s->state = g_wait_fail ? 9 : 10; return 0; }
static dm_audio_result f_get_state(dm_aaudio_stream *s) { return s->state; }
static dm_audio_result f_wait(dm_aaudio_stream *s, int32_t cur, int32_t *next, int64_t ns) { (void)s; (void)cur; (void)ns; if (g_wait_fail) return -5; *next = 10; return 0; }
static dm_audio_result f_get_format(dm_aaudio_stream *s) { return s->format; }
static dm_audio_result f_get_channels(dm_aaudio_stream *s) { return s->channels; }
static dm_audio_result f_get_rate(dm_aaudio_stream *s) { return s->rate; }
static dm_audio_result f_get_buffer(dm_aaudio_stream *s) { (void)s; return 0; }
static dm_audio_result f_get_xruns(dm_aaudio_stream *s) { (void)s; return 0; }

static const dm_audio_api api = {
    f_create_builder, f_delete_builder, f_set_format, f_set_channels, f_set_rate, f_set_direction,
    f_set_data, f_set_error, f_open, f_close, f_start, f_pause, f_stop, f_get_state, f_wait,
    f_get_format, f_get_channels, f_get_rate, f_get_buffer, f_get_xruns,
};
static dm_audio_options options(uint32_t frames) { dm_audio_options o = DM_AUDIO_OPTIONS_INIT; o.queue_frames = frames; o.api = &api; return o; }
static void reset(void) {
    g_open_fail = g_mismatch = g_start_fail = g_pause_fail = g_stop_fail = g_close_fail = g_wait_fail = 0;
    g_close_calls = g_delete_builder_calls = g_stop_calls = 0; g_callback_destroy_result = 99;
}

int main(void) {
    reset(); dm_audio *a = NULL;
    CHECK(dm_audio_create(&(dm_audio_options){0}, &a) == -EINVAL);
    dm_audio_options bad = options(4); bad.struct_size = 0;
    CHECK(dm_audio_create(&bad, &a) == -EINVAL);
    dm_audio_options noapi = options(4); noapi.api = NULL;
    CHECK(dm_audio_create(&noapi, &a) == -ENOTSUP);

    reset(); CHECK(dm_audio_create(&(dm_audio_options)DM_AUDIO_OPTIONS_INIT, &a) == -EINVAL);

    reset(); dm_audio_options o = options(4);
    CHECK(dm_audio_create(&o, &a) == 0 && a);
    CHECK(g_cb != NULL && g_cb_user == a && g_delete_builder_calls == 1);
    CHECK(dm_audio_start(a) == 0);
    CHECK(dm_audio_pause(a) == 0);
    CHECK(dm_audio_stop(a) == 0);

    // Sample order and multiple chunks.
    int16_t frames[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(dm_audio_write(a, frames, 2) == 2);
    CHECK(dm_audio_write(a, frames + 4, 2) == 2);
    int16_t out[8] = {0};
    CHECK(g_cb(NULL, g_cb_user, out, 4) == DM_AUDIO_CALLBACK_CONTINUE);
    for (int i = 0; i < 8; i++) CHECK(out[i] == (int16_t)(i + 1));
    dm_audio_stats st; dm_audio_get_stats(a, &st);
    CHECK(st.frames_written == 4 && st.frames_dropped == 0 && st.underrun_frames == 0);
    CHECK(dm_audio_destroy(a) == 0);

    // Overflow preserves the bounded prefix and counts dropped frames.
    reset(); o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    int16_t big[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    CHECK(dm_audio_write(a, big, 6) == 4);
    memset(out, 0, sizeof out); CHECK(g_cb(NULL, g_cb_user, out, 4) == 0);
    for (int i = 0; i < 8; i++) CHECK(out[i] == big[i]);
    dm_audio_get_stats(a, &st); CHECK(st.frames_written == 4 && st.frames_dropped == 2);
    CHECK(dm_audio_destroy(a) == 0);

    // Partial callback: available frames then explicit silence + underrun count.
    reset(); o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    CHECK(dm_audio_write(a, frames, 1) == 1);
    memset(out, 0xff, sizeof out);
    CHECK(g_cb(NULL, g_cb_user, out, 3) == 0);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 0 && out[3] == 0 && out[4] == 0 && out[5] == 0);
    dm_audio_get_stats(a, &st); CHECK(st.underrun_frames == 2 && st.underrun_events == 1);
    CHECK(dm_audio_destroy(a) == 0);

    // Format mismatch is rejected after open and the stream is closed.
    reset(); g_mismatch = 1; o = options(4); a = NULL;
    CHECK(dm_audio_create(&o, &a) == -EPROTO && a == NULL && g_close_calls == 1);
    reset(); g_open_fail = 1; a = NULL; CHECK(dm_audio_create(&o, &a) == -EIO && a == NULL);

    // Lifecycle and disconnect/error paths.
    reset(); g_start_fail = 1; o = options(4); a = NULL;
    CHECK(dm_audio_create(&o, &a) == 0 && dm_audio_start(a) == -EIO);
    CHECK(dm_audio_destroy(a) == 0);
    reset(); g_pause_fail = g_stop_fail = 1; o = options(4); a = NULL;
    CHECK(dm_audio_create(&o, &a) == 0);
    CHECK(dm_audio_pause(a) == -EIO && dm_audio_stop(a) == -EIO);
    g_pause_fail = g_stop_fail = 0;
    CHECK(dm_audio_destroy(a) == 0);
    reset(); o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    dm_audio_stats before; dm_audio_get_stats(a, &before);
    (void)before;
    if (g_err) g_err(NULL, g_err_user, -3);
    dm_audio_get_stats(a, &before); CHECK(before.disconnected == 1);
    CHECK(dm_audio_write(a, frames, 1) == 0);
    CHECK(dm_audio_start(a) == -ENOTCONN);
    CHECK(dm_audio_destroy(a) == 0);

    // Callback-context destroy returns EBUSY, never closes/reenters.
    reset(); g_cb = NULL; o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    CHECK(g_cb != NULL);
    g_cb(NULL, a, out, 1); // no data -> underrun; still callback context ended
    CHECK(dm_audio_destroy(a) == 0);

    // Close/wait/stop failure preserves the object rather than claiming safe cleanup.
    reset(); g_close_fail = 1; o = options(4); a = NULL;
    CHECK(dm_audio_create(&o, &a) == 0);
    CHECK(dm_audio_destroy(a) == -EIO);
    CHECK(dm_audio_destroy(a) == -EBUSY); // retained and closing; caller sees proof failure
    reset(); g_wait_fail = 1; o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    CHECK(dm_audio_destroy(a) == -EIO);

    printf("audio_aaudio: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
