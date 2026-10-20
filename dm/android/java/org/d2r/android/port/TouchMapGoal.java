package org.d2r.android.port;

/** Pure minimap-goal geometry and generated-asset selection. */
final class TouchMapGoal {
    private TouchMapGoal() {}

    static boolean visible(int mode, int count, int navigation, int frontend, boolean haveMini) {
        return mode != 0 && count > 0 && navigation == 0 && frontend == 0 &&
               (mode == 2 || haveMini);
    }

    static float projectedX(float originX, float playerX, float playerY, int x, int y, float cell) {
        float dx = x - playerX, dy = y - playerY;
        return originX + (dx - dy) * 1.6f * cell;
    }

    static float projectedY(float originY, float playerX, float playerY, int x, int y, float cell) {
        float dx = x - playerX, dy = y - playerY;
        return originY + (dx + dy) * .8f * cell;
    }

    static boolean inside(float x, float y, float left, float top, float right, float bottom,
                          int mode, float fullRight, float fullBottom) {
        return x >= left && x <= right && y >= top && y <= bottom &&
               !(mode == 2 && x > fullRight && y > fullBottom);
    }

    static float[] clampToEdge(float x, float y, float originX, float originY,
                               float left, float top, float right, float bottom) {
        float tx = x > originX ? (right - originX) / (x - originX)
                 : x < originX ? (left - originX) / (x - originX) : 1e9f;
        float ty = y > originY ? (bottom - originY) / (y - originY)
                 : y < originY ? (top - originY) / (y - originY) : 1e9f;
        float t = Math.min(tx, ty);
        return new float[]{originX + (x - originX) * t, originY + (y - originY) * t};
    }

    static String labelAsset(int art, boolean zh, int langIndex, boolean old) {
        int id = art >= 64 ? 0 : art;
        if (!zh) return "label_" + id + "_en.png";
        if (old && langIndex == 8) return "label_" + id + "_old.png";
        return "label_" + id + ".png";
    }

    static String markAsset(int art, boolean inside) {
        if (!inside) return null;
        return art >= 64 ? "sym_" + (art - 64) + ".png" : "cross.png";
    }
}
