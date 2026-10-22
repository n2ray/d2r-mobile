package org.d2r.android.port;

// Android implementation of the same three drop tiers consumed by the iOS
// front end. Values are android.view.HapticFeedbackConstants; keeping the
// mapping here makes it testable without an Android runtime.
final class DropHaptic {
    static final int HEAVY = 0;    // HapticFeedbackConstants.LONG_PRESS
    static final int MEDIUM = 6;   // HapticFeedbackConstants.CONTEXT_CLICK
    static final int LIGHT = 3;    // HapticFeedbackConstants.KEYBOARD_TAP
    static final int INVALID = -1;

    private DropHaptic() {}

    static int feedbackForTier(int tier) {
        if (tier == 1) return HEAVY;
        if (tier == 2) return MEDIUM;
        if (tier == 3) return LIGHT;
        return INVALID;
    }
}
