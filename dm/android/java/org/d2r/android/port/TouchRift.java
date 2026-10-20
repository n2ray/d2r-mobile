package org.d2r.android.port;

/** Production visibility decision for the host rift snapshot. */
final class TouchRift {
    private TouchRift() {}

    static boolean visible(int navigation, int frontend, int active) {
        return navigation == 0 && frontend == 0 && active != 0;
    }
}
