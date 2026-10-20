package org.d2r.android.port;

/** Pack normalized straight RGBA (the units used by the upstream ControlArt)
 * into Android ARGB without OR-ing independent components. */
final class TouchSocketColor {
    private TouchSocketColor() {}

    static int argb(float red, float green, float blue, float alpha) {
        return (channel(alpha) << 24) | (channel(red) << 16) |
               (channel(green) << 8) | channel(blue);
    }

    private static int channel(float value) {
        int v = Math.round(Math.max(0f, Math.min(1f, value)) * 255f);
        return v & 0xff;
    }
}
