package org.d2r.android.port;

public final class TouchHudPolicyTest {
    private static int failures;
    private static void check(boolean actual, String what) {
        if (!actual) { failures++; System.err.println("FAIL " + what); }
    }
    public static void main(String[] args) {
        int generation = 0;
        int stale = generation;
        generation = TouchHaptic.nextGeneration(generation);
        check(!TouchHaptic.allowed(stale, generation, true, true, false, false),
              "enqueue -> loss -> resume rejects stale generation");
        int fresh = generation;
        check(TouchHaptic.allowed(fresh, generation, true, true, false, false),
              "fresh request is accepted once");
        check(!TouchHaptic.allowed(fresh, generation, false, true, false, false), "paused rejects");
        check(!TouchHaptic.allowed(fresh, generation, true, false, false, false), "focus loss rejects");
        check(!TouchHaptic.allowed(fresh, generation, true, true, true, false), "finishing rejects");
        check(!TouchHaptic.allowed(fresh, generation, true, true, false, true), "destroyed rejects");
        check(TouchRift.visible(0, 0, 1), "active rift in game draws");
        check(!TouchRift.visible(1, 0, 1), "navigation hides");
        check(!TouchRift.visible(0, 1, 1), "frontend hides");
        check(!TouchRift.visible(0, 0, 0), "invalid/notinrift hides");
        if (failures != 0) throw new AssertionError(failures + " HUD policy checks failed");
        System.out.println("PASS: haptic generation/focus and rift visibility decisions");
    }
}
