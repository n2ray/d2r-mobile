package android.content.res;

/** Narrow host-test Resources boundary exposing only display metrics density. */
public class Resources {
    private final DisplayMetrics metrics = new DisplayMetrics();

    public DisplayMetrics getDisplayMetrics() {
        return metrics;
    }

    public static final class DisplayMetrics {
        public float density = 1.0f;
    }
}
