package org.d2r.android.port;

/** Exact packed colors used by the upstream socket treatment. */
public final class TouchSocketColorTest {
    private static int failures;
    private static void equal(int actual, int expected, String what) {
        if (actual != expected) {
            failures++;
            System.err.printf("FAIL %s: %08x != %08x%n", what, actual, expected);
        }
    }
    public static void main(String[] args) {
        // DmTouchControlArt.h metal/well/rim/face/primary values, normalized
        // straight RGBA -> Android ARGB.
        equal(TouchSocketColor.argb(.26f, .28f, .28f, .9f), 0xE6424747, "metal normal top");
        equal(TouchSocketColor.argb(.26f, .28f, .28f, .22f), 0x38424747, "metal empty top");
        equal(TouchSocketColor.argb(.055f, .065f, .07f, .95f), 0xF20E1112, "metal normal mid");
        equal(TouchSocketColor.argb(.021f, .021f, .021f, .94f), 0xF0050505, "well normal top");
        equal(TouchSocketColor.argb(.021f, .021f, .021f, .08f), 0x14050505, "well empty top");
        equal(TouchSocketColor.argb(.008f, .012f, .015f, .9f), 0xE6020304, "rim normal");
        equal(TouchSocketColor.argb(.008f, .012f, .015f, .38f), 0x61020304, "rim empty");
        equal(TouchSocketColor.argb(.64f, .64f, .64f, .72f), 0xB8A3A3A3, "face held");
        equal(TouchSocketColor.argb(.26f, .29f, .28f, .18f), 0x2E424A47, "face empty");
        equal(TouchSocketColor.argb(.31f, .23f, .22f, .52f), 0x854F3B38, "primary ring");
        equal(TouchSocketColor.argb(.60f, .65f, .64f, .25f), 0x4099A6A3, "empty cross idle");
        equal(TouchSocketColor.argb(.60f, .65f, .64f, .75f), 0xBF99A6A3, "empty cross held");
        if (failures != 0) throw new AssertionError(failures + " socket color checks failed");
        System.out.println("PASS: upstream socket colors pack to exact Android ARGB");
    }
}
