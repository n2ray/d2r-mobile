// 自有 AAudio 输出：真实 open/配置校验/start/pause/stop/close，固定 PCM16 stereo 48k，单生产者有界队列。
// Owned AAudio output: real open/config validation/start/pause/stop/close, fixed PCM16 stereo 48k, bounded single-producer queue.
#include "dm_aaudio.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct dm_audio {
    const dm_audio_api *api;
    dm_aaudio_stream *stream;
    int16_t *ring;
    uint32_t capacity, mask;
    _Atomic uint32_t head, tail;
    _Atomic uint64_t written, dropped, underrun_frames, underrun_events, callbacks;
    _Atomic int closing, disconnected, callback_active;
    _Atomic uintptr_t callback_thread;
    pthread_mutex_t lifecycle;
    int started;
};

#ifdef __ANDROID__
static const dm_audio_api g_real_api = {
    .create_builder = AAudio_createStreamBuilder,
    .delete_builder = AAudioStreamBuilder_delete,
    .set_format = AAudioStreamBuilder_setFormat,
    .set_channel_count = AAudioStreamBuilder_setChannelCount,
    .set_sample_rate = AAudioStreamBuilder_setSampleRate,
    .set_direction = AAudioStreamBuilder_setDirection,
    .set_data_callback = AAudioStreamBuilder_setDataCallback,
    .set_error_callback = AAudioStreamBuilder_setErrorCallback,
    .open = AAudioStreamBuilder_openStream,
    .close = AAudioStream_close,
    .request_start = AAudioStream_requestStart,
    .request_pause = AAudioStream_requestPause,
    .request_stop = AAudioStream_requestStop,
    .get_state = AAudioStream_getState,
    .wait_for_state_change = AAudioStream_waitForStateChange,
    .get_format = AAudioStream_getFormat,
    .get_channel_count = AAudioStream_getChannelCount,
    .get_sample_rate = AAudioStream_getSampleRate,
    .get_buffer_size = AAudioStream_getBufferSizeInFrames,
    .get_xrun_count = AAudioStream_getXRunCount,
};
const dm_audio_api *dm_audio_default_api(void) { return &g_real_api; }
#else
const dm_audio_api *dm_audio_default_api(void) { return NULL; }
#endif

static uint32_t round_pow2(uint32_t n) {
    uint32_t p = 2;
    while (p < n && p < (1u << 20)) p <<= 1;
    return p;
}

static dm_audio_callback_result data_callback(dm_aaudio_stream *stream, void *user, void *data, int32_t num_frames) {
    (void)stream;
    dm_audio *a = user;
    if (!a || num_frames <= 0 || atomic_load_explicit(&a->closing, memory_order_acquire))
        return DM_AUDIO_CALLBACK_STOP;
    atomic_store_explicit(&a->callback_thread, (uintptr_t)pthread_self(), memory_order_relaxed);
    atomic_fetch_add_explicit(&a->callback_active, 1, memory_order_acq_rel);
    if (atomic_load_explicit(&a->closing, memory_order_acquire)) {
        atomic_fetch_sub_explicit(&a->callback_active, 1, memory_order_acq_rel);
        atomic_store_explicit(&a->callback_thread, 0, memory_order_relaxed);
        return DM_AUDIO_CALLBACK_STOP;
    }
    atomic_fetch_add_explicit(&a->callbacks, 1, memory_order_relaxed);
    uint32_t frames = (uint32_t)num_frames;
    uint32_t tail = atomic_load_explicit(&a->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&a->head, memory_order_acquire);
    uint32_t available = head - tail;
    uint32_t take = available < frames ? available : frames;
    int16_t *out = data;
    for (uint32_t i = 0; i < take * DM_AUDIO_CHANNELS; i++) out[i] = a->ring[(tail * DM_AUDIO_CHANNELS + i) & (a->capacity * DM_AUDIO_CHANNELS - 1)];
    if (take < frames) {
        memset(out + take * DM_AUDIO_CHANNELS, 0, (size_t)(frames - take) * DM_AUDIO_FRAME_BYTES);
        atomic_fetch_add_explicit(&a->underrun_frames, frames - take, memory_order_relaxed);
        atomic_fetch_add_explicit(&a->underrun_events, 1, memory_order_relaxed);
    }
    atomic_store_explicit(&a->tail, tail + take, memory_order_release);
    atomic_fetch_sub_explicit(&a->callback_active, 1, memory_order_acq_rel);
    atomic_store_explicit(&a->callback_thread, 0, memory_order_relaxed);
    return DM_AUDIO_CALLBACK_CONTINUE;
}

static void error_callback(dm_aaudio_stream *stream, void *user, dm_audio_result error) {
    (void)stream; (void)error;
    dm_audio *a = user;
    if (a) atomic_store_explicit(&a->disconnected, 1, memory_order_release);
}

int dm_audio_create(const dm_audio_options *options, dm_audio **out) {
    if (!out) return -EINVAL;
    *out = NULL;
    if (!options || options->struct_size != sizeof *options || options->queue_frames < 2) return -EINVAL;
    const dm_audio_api *api = options->api ? options->api : dm_audio_default_api();
    if (!api) return -ENOTSUP;
    if (!api->create_builder || !api->delete_builder || !api->open || !api->close ||
        !api->set_data_callback || !api->set_error_callback ||
        !api->request_start || !api->request_pause || !api->request_stop || !api->get_format ||
        !api->get_channel_count || !api->get_sample_rate || !api->get_state || !api->wait_for_state_change)
        return -EINVAL;
    if (options->queue_frames > (1u << 20)) return -EINVAL;
    dm_audio *a = calloc(1, sizeof *a);
    if (!a) return -ENOMEM;
    a->api = api;
    a->capacity = round_pow2(options->queue_frames);
    a->mask = a->capacity - 1;
    a->ring = calloc((size_t)a->capacity * DM_AUDIO_CHANNELS, sizeof *a->ring);
    if (!a->ring) { free(a); return -ENOMEM; }
    if (pthread_mutex_init(&a->lifecycle, NULL) != 0) { free(a->ring); free(a); return -ENOMEM; }
    dm_aaudio_builder *builder = NULL;
    dm_audio_result result = api->create_builder(&builder);
    if (result != DM_AUDIO_OK || !builder) { pthread_mutex_destroy(&a->lifecycle); free(a->ring); free(a); return -EIO; }
    api->set_format(builder, DM_AUDIO_FORMAT_PCM16);
    api->set_channel_count(builder, DM_AUDIO_CHANNELS);
    api->set_sample_rate(builder, DM_AUDIO_RATE);
    if (api->set_direction) api->set_direction(builder, 0); // AAUDIO_DIRECTION_OUTPUT
    if (api->set_data_callback) api->set_data_callback(builder, data_callback, a);
    if (api->set_error_callback) api->set_error_callback(builder, error_callback, a);
    result = api->open(builder, &a->stream);
    (void)api->delete_builder(builder);
    if (result != DM_AUDIO_OK || !a->stream) {
        pthread_mutex_destroy(&a->lifecycle); free(a->ring); free(a); return -EIO;
    }
    if (api->get_format(a->stream) != DM_AUDIO_FORMAT_PCM16 ||
        api->get_channel_count(a->stream) != DM_AUDIO_CHANNELS ||
        api->get_sample_rate(a->stream) != DM_AUDIO_RATE) {
        (void)api->close(a->stream);
        pthread_mutex_destroy(&a->lifecycle); free(a->ring); free(a); return -EPROTO;
    }
    *out = a;
    return 0;
}

int dm_audio_start(dm_audio *a) {
    if (!a || atomic_load_explicit(&a->closing, memory_order_acquire)) return -EINVAL;
    if (atomic_load_explicit(&a->disconnected, memory_order_acquire)) return -ENOTCONN;
    dm_audio_result r = a->api->request_start(a->stream);
    if (r == DM_AUDIO_OK) a->started = 1;
    return r == DM_AUDIO_OK ? 0 : -EIO;
}
int dm_audio_pause(dm_audio *a) {
    if (!a || !a->stream) return -EINVAL;
    dm_audio_result r = a->api->request_pause(a->stream);
    if (r == DM_AUDIO_OK) a->started = 0;
    return r == DM_AUDIO_OK ? 0 : -EIO;
}
int dm_audio_stop(dm_audio *a) {
    if (!a || !a->stream) return -EINVAL;
    dm_audio_result r = a->api->request_stop(a->stream);
    if (r == DM_AUDIO_OK) a->started = 0;
    return r == DM_AUDIO_OK ? 0 : -EIO;
}

uint32_t dm_audio_write(dm_audio *a, const int16_t *samples, uint32_t frames) {
    if (!a || !samples || !frames) return 0;
    if (atomic_load_explicit(&a->closing, memory_order_acquire) ||
        atomic_load_explicit(&a->disconnected, memory_order_acquire)) return 0;
    uint32_t head = atomic_load_explicit(&a->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&a->tail, memory_order_acquire);
    uint32_t free_frames = a->capacity - (head - tail);
    uint32_t take = frames < free_frames ? frames : free_frames;
    for (uint32_t i = 0; i < take * DM_AUDIO_CHANNELS; i++)
        a->ring[(head * DM_AUDIO_CHANNELS + i) & (a->capacity * DM_AUDIO_CHANNELS - 1)] =
            samples[i];
    atomic_store_explicit(&a->head, head + take, memory_order_release);
    atomic_fetch_add_explicit(&a->written, take, memory_order_relaxed);
    if (take < frames) atomic_fetch_add_explicit(&a->dropped, frames - take, memory_order_relaxed);
    return take;
}

void dm_audio_get_stats(const dm_audio *a, dm_audio_stats *stats) {
    if (!stats) return;
    memset(stats, 0, sizeof *stats);
    if (!a) return;
    stats->frames_written = atomic_load_explicit(&a->written, memory_order_relaxed);
    stats->frames_dropped = atomic_load_explicit(&a->dropped, memory_order_relaxed);
    stats->underrun_frames = atomic_load_explicit(&a->underrun_frames, memory_order_relaxed);
    stats->underrun_events = atomic_load_explicit(&a->underrun_events, memory_order_relaxed);
    stats->callbacks = atomic_load_explicit(&a->callbacks, memory_order_relaxed);
    stats->disconnected = atomic_load_explicit(&a->disconnected, memory_order_acquire);
    stats->closing = atomic_load_explicit(&a->closing, memory_order_acquire);
    if (a->api && a->api->get_xrun_count && a->stream) {
        dm_audio_result x = a->api->get_xrun_count(a->stream);
        if (x > 0) stats->xruns = (uint64_t)x;
    }
}

int dm_audio_destroy(dm_audio *a) {
    if (!a) return 0;
    pthread_mutex_lock(&a->lifecycle);
    if (atomic_load_explicit(&a->closing, memory_order_acquire)) { pthread_mutex_unlock(&a->lifecycle); return -EBUSY; }
    if (atomic_load_explicit(&a->callback_thread, memory_order_relaxed) == (uintptr_t)pthread_self()) {
        pthread_mutex_unlock(&a->lifecycle); return -EBUSY;
    }
    atomic_store_explicit(&a->closing, 1, memory_order_release);
    pthread_mutex_unlock(&a->lifecycle);
    dm_audio_result stop = a->api->request_stop(a->stream);
    int32_t current = a->api->get_state(a->stream);
    int32_t next = current;
    dm_audio_result wait = DM_AUDIO_OK;
    if (current != DM_AUDIO_STATE_STOPPED)
        wait = a->api->wait_for_state_change(a->stream, current, &next, 100000000);
    dm_audio_result close = a->api->close(a->stream);
    while (atomic_load_explicit(&a->callback_active, memory_order_acquire) != 0) sched_yield();
    if (stop != DM_AUDIO_OK || wait != DM_AUDIO_OK || next != DM_AUDIO_STATE_STOPPED || close != DM_AUDIO_OK) {
        // 证明失败时不释放队列/控制块；保留对象供调用者诊断，绝不假装安全关闭。
        return -EIO;
    }
    a->stream = NULL;
    pthread_mutex_destroy(&a->lifecycle);
    free(a->ring);
    free(a);
    return 0;
}
