package android.widget;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/** Narrow host-test vertical layout boundary; production uses the real LinearLayout. */
public class LinearLayout extends ViewGroup {
    public static final int HORIZONTAL = 0;
    public static final int VERTICAL = 1;

    public static class LayoutParams extends ViewGroup.LayoutParams {
        public LayoutParams(int width, int height) {
            super(width, height);
        }
    }

    private int orientation;

    public LinearLayout(Context context) {
        super(context);
    }

    public void setOrientation(int value) {
        orientation = value;
    }

    public int getOrientation() {
        return orientation;
    }
}
