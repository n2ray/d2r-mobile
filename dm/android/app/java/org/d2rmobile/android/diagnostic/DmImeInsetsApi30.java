package org.d2rmobile.android.diagnostic;

import android.graphics.Insets;
import android.view.WindowInsets;

/** API 30+ implementation isolated so API 29 never resolves WindowInsets.Type. */
final class DmImeInsetsApi30 {
    private DmImeInsetsApi30() { }

    static DmImeInsetsObservation observe(WindowInsets insets, long generation, long sequence) {
        int type = WindowInsets.Type.ime();
        boolean visible = insets != null && insets.isVisible(type);
        Insets geometry = visible ? insets.getInsets(type) : Insets.NONE;
        return new DmImeInsetsObservation(true, visible, geometry.left, geometry.top,
                geometry.right, geometry.bottom, generation, sequence);
    }
}
