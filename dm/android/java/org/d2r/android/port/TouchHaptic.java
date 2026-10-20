package org.d2r.android.port;

/** Production eligibility for one host-queued drop haptic. */
final class TouchHaptic {
    private TouchHaptic() {}

    static boolean allowed(int scheduledGeneration, int currentGeneration,
                           boolean resumed, boolean focused,
                           boolean finishing, boolean destroyed) {
        return scheduledGeneration == currentGeneration &&
               resumed && focused && !finishing && !destroyed;
    }

    static int nextGeneration(int current) {
        return current == Integer.MAX_VALUE ? 1 : current + 1;
    }
}
