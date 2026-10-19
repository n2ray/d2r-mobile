// #222 owned failure injection for the single-consumer handoff. No AAudio hardware or game.
#include "../../host/audio_handoff.h"
#include <stdio.h>

static int sink_calls;
static void fake_sink(void *ctx) { (void)ctx; ++sink_calls; }

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    sink_calls = 0;
    CHECK(dm_audio_handoff(1, -895, fake_sink, NULL) == 0);   // AAUDIO_ERROR_TIMEOUT
    CHECK(sink_calls == 0);
    CHECK(dm_audio_handoff(1, -1, fake_sink, NULL) == 0);     // requestStop/other error
    CHECK(sink_calls == 0);
    CHECK(dm_audio_handoff(1, 0, fake_sink, NULL) == 1);      // confirmed STOPPED/DISCONNECTED
    CHECK(sink_calls == 1);
    CHECK(dm_audio_handoff(0, -895, fake_sink, NULL) == 1);   // no live stream at all
    CHECK(sink_calls == 2);
    CHECK(dm_audio_handoff(1, 0, NULL, NULL) == 1);           // no sink requested, stop still confirmed
    puts("PASS: unconfirmed stop never starts a second consumer; confirmed/no-stream hands off exactly once");
    return 0;
}
