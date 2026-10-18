// Narrow Android physical-gamepad event conversion.
//
// The caller owns device admission/generation and supplies the prior npad
// state for one admitted device. This module only converts KeyEvent/MotionEvent
// getters into that state; it does not arbitrate sources or touch input.
#pragma once

#include <stdint.h>
#include "input.h"

#ifdef __ANDROID__
#include <android/input.h>
#else
typedef struct AInputEvent AInputEvent;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 and atomically updates *state for a recognized gamepad/joystick
// KeyEvent or MotionEvent. Returns 0 and leaves *state byte-for-byte unchanged
// for unknown sources/actions/keys, malformed repeats, or non-finite/out-of-
// range axes. Digital buttons, D-pad/hat, both sticks and both triggers are
// handled; unrelated fields and the caller's sampling/attributes are preserved.
int dm_gamepad_event_apply(const AInputEvent *event, dm_npad_state *state);

#ifdef __cplusplus
}
#endif
