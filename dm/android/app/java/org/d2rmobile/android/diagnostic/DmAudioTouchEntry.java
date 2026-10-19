package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;

/**
 * Owned diagnostic touch entry for the accepted NativeActivity audio controls.
 *
 * <p>A small owned vertical {@link LinearLayout} holds the play/stop buttons. Its padding is
 * density-aware and the platform places children from their measured heights, so larger
 * buttons/fonts never overlap each other; the container sits below the IME entry's default
 * top-left position. It never replaces the NativeActivity surface, HUD, IME or peer views and
 * never changes window/focus settings.</p>
 */
public final class DmAudioTouchEntry {
    private static final String PLAY_LABEL = "诊断音频 播放";
    private static final String STOP_LABEL = "诊断音频 停止";

    /** Narrow test seam; production uses the registered native methods. */
    interface ControlSink {
        boolean play(long token);
        boolean stop(long token);
    }

    private static LinearLayout ownedContainer;
    private static Button playView;
    private static Button stopView;
    private static Activity ownedActivity;
    private static long ownedToken;
    private static boolean clickAuthorized;
    private static ControlSink testSink;

    private DmAudioTouchEntry() {
    }

    public static boolean install(Activity activity, long token) {
        requireMainLooper();
        if (activity == null || token == 0) return false;
        if (ownedContainer != null) {
            if (ownedActivity != activity) return false;
            if (clickAuthorized && ownedToken == token && ownedContainer.getParent() != null) return true;
            if (!cleanupOwned()) return false;
        }
        View content = activity.findViewById(android.R.id.content);
        if (!(content instanceof ViewGroup)) return false;
        ViewGroup group = (ViewGroup) content;

        float density = activity.getResources().getDisplayMetrics().density;
        LinearLayout box = new LinearLayout(activity);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(8, density), dp(72, density), dp(8, density), dp(8, density));
        Button play = makeButton(activity, PLAY_LABEL);
        Button stop = makeButton(activity, STOP_LABEL);
        play.setOnClickListener(view -> {
            if (clickAuthorized && playView == view && ownedActivity == activity && ownedToken == token) {
                requestPlay(token);
            }
        });
        stop.setOnClickListener(view -> {
            if (clickAuthorized && stopView == view && ownedActivity == activity && ownedToken == token) {
                requestStop(token);
            }
        });
        box.addView(play, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        box.addView(stop, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        // Retain the cleanup handle before attaching so a partial attach can be retried.
        ownedContainer = box;
        playView = play;
        stopView = stop;
        ownedActivity = activity;
        ownedToken = token;
        clickAuthorized = false;
        try {
            group.addView(box, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        } catch (RuntimeException failure) {
            cleanupOwned();
            return false;
        }
        clickAuthorized = true;
        return true;
    }

    private static Button makeButton(Activity activity, String label) {
        Button button = new Button(activity);
        button.setText(label);
        button.setAllCaps(false);
        button.setClickable(true);
        button.setFocusable(false);
        button.setFocusableInTouchMode(false);
        return button;
    }

    private static int dp(int value, float density) {
        return Math.round(value * density);
    }

    /** Focused-test helper: measured stacked bounds stay disjoint for large fonts/density. */
    static boolean stackedBoundsDisjoint(int[] heights, int gap) {
        int top = 0;
        for (int i = 0; i < heights.length; i++) {
            if (heights[i] <= 0) return false;
            int bottom = top + heights[i];
            if (i + 1 < heights.length) {
                int nextTop = bottom + gap;
                if (nextTop < bottom) return false;   // negative gap would overlap
                top = nextTop;
            }
        }
        return true;
    }

    /**
     * Invalidates click authorization immediately, then removes exactly the container this class installed.
     * If platform removal throws, the cleanup handle is retained (clicks disabled) for a bounded retry.
     */
    public static boolean uninstall(Activity activity, long token) {
        requireMainLooper();
        if (ownedContainer == null) return true;
        if (ownedActivity != activity || ownedToken != token) return false;
        clickAuthorized = false;
        if (playView != null) playView.setOnClickListener(null);
        if (stopView != null) stopView.setOnClickListener(null);
        return cleanupOwned();
    }

    /** Retries removal of the retained container without reviving clicks. */
    private static boolean cleanupOwned() {
        clickAuthorized = false;
        if (ownedContainer == null) return true;
        ViewGroup parent = ownedContainer.getParent() instanceof ViewGroup
                ? (ViewGroup) ownedContainer.getParent() : null;
        if (parent != null) {
            try {
                parent.removeView(ownedContainer);
            } catch (RuntimeException failure) {
                return false;
            }
        }
        ownedContainer = null;
        playView = null;
        stopView = null;
        ownedActivity = null;
        ownedToken = 0;
        return true;
    }

    /** Test-only sink; never set by production callers. */
    static void setTestSink(ControlSink sink) {
        testSink = sink;
    }

    private static boolean requestPlay(long token) {
        ControlSink sink = testSink;
        return sink != null ? sink.play(token) : nativePlay(token);
    }

    private static boolean requestStop(long token) {
        ControlSink sink = testSink;
        return sink != null ? sink.stop(token) : nativeStop(token);
    }

    private static void requireMainLooper() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            throw new IllegalStateException("DmAudioTouchEntry must run on the main looper");
        }
    }

    private static native boolean nativePlay(long token);

    private static native boolean nativeStop(long token);
}
