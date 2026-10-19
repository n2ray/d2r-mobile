// AAudio stop is asynchronous and fallible. Consumer ownership may move to the silent sink only
// after the old stream is confirmed stopped/disconnected; success is never synthesized on timeout.
#pragma once

static inline int dm_audio_handoff(int has_stream, int stop_result, void (*start_sink)(void *), void *ctx) {
    if (has_stream && stop_result != 0) return 0;
    if (start_sink) start_sink(ctx);
    return 1;
}
