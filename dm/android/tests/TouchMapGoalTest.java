package org.d2r.android.port;

public final class TouchMapGoalTest {
    private static int failures;
    private static void check(boolean v, String what) {
        if (!v) { failures++; System.err.println("FAIL " + what); }
    }
    private static void near(float a, float b, String what) {
        if (Math.abs(a - b) > .01f) { failures++; System.err.println("FAIL " + what + " " + a + " != " + b); }
    }
    private static void aspect(int w, int h) {
        float left = .65f * w, top = .02f * h, right = .90f * w, bottom = .35f * h;
        float ox = (left + right) * .5f, oy = (top + bottom) * .5f;
        float cell = 2f;
        // Same player/goal cell: centre of the minimap.
        float ix = TouchMapGoal.projectedX(ox, 10, 10, 10, 10, cell);
        float iy = TouchMapGoal.projectedY(oy, 10, 10, 10, 10, cell);
        check(TouchMapGoal.inside(ix, iy, left, top, right, bottom, 1, 1e9f, 1e9f),
              "goal at player is inside " + w + "x" + h);
        // Far goal: projected outside; the production edge clamp lands on a border.
        float fx = TouchMapGoal.projectedX(ox, 10, 10, 70, 10, cell);
        float fy = TouchMapGoal.projectedY(oy, 10, 10, 70, 10, cell);
        check(!TouchMapGoal.inside(fx, fy, left, top, right, bottom, 1, 1e9f, 1e9f),
              "far goal is outside " + w + "x" + h);
        float[] e = TouchMapGoal.clampToEdge(fx, fy, ox, oy, left, top, right, bottom);
        check(e[0] >= left - .01f && e[0] <= right + .01f &&
              e[1] >= top - .01f && e[1] <= bottom + .01f,
              "edge clamp inside " + w + "x" + h);
    }
    public static void main(String[] args) {
        aspect(667, 375);
        aspect(780, 360);
        check(TouchMapGoal.visible(1, 1, 0, 0, true), "mini visible");
        check(!TouchMapGoal.visible(1, 0, 0, 0, true), "no goals hidden");
        check(!TouchMapGoal.visible(1, 1, 1, 0, true), "navigation hidden");
        check(!TouchMapGoal.visible(2, 1, 0, 1, false), "frontend hidden");
        check(TouchMapGoal.labelAsset(5, true, 8, false).equals("label_5.png"), "zh base label");
        check(TouchMapGoal.labelAsset(5, false, 8, false).equals("label_5_en.png"), "en label");
        check(TouchMapGoal.labelAsset(5, true, 8, true).equals("label_5_old.png"), "old label");
        check(TouchMapGoal.labelAsset(66, false, 0, false).equals("label_0_en.png"), "symbol offscreen label 0");
        check(TouchMapGoal.markAsset(65, true).equals("sym_1.png"), "symbol asset");
        check(TouchMapGoal.markAsset(5, true).equals("cross.png"), "cross asset");
        check(TouchMapGoal.markAsset(5, false) == null, "edge has no mark");
        if (failures != 0) throw new AssertionError(failures + " map goal checks failed");
        System.out.println("PASS: map goal visibility, in/out geometry, edge clamp and asset IDs");
    }
}
