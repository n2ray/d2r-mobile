package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.widget.Button;

/**
 * Owned diagnostic touch entry for the accepted NativeActivity audio controls.
 *
 * <p>Installs two clearly labelled buttons (play/stop) into the existing
 * {@code android.R.id.content}; it never replaces the NativeActivity surface,
 * HUD, IME entry or any peer view. They are offset below the IME entry's
 * default top-left position and never request text/game focus.</p>
 *
 * <p>Construction/install/resume never calls native_audio: only a user click
 * asks the registered native owner through an opaque token. Native validates
 * the current owner/token and the App active/resumed/focused/window guards
 * before sharing the same explicit play/stop path as keyboard P/S. Stale or
 * duplicate clicks after teardown/pause/focus loss are inert.</p>
 */
public final class DmAudioTouchEntry {
    private static final String PLAY_LABEL = "诊断音频 播放";
    private static final String STOP_LABEL = "诊断音频 停止";

    /** Narrow test seam; production uses the registered native methods. */
    interface ControlSink {
        boolean play(long token);
        boolean stop(long token);
    }

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
        if (playView != null || stopView != null) {
            if (ownedActivity != activity) return false;
            if (clickAuthorized && ownedToken == token && playView != null && playView.getParent() != null
                    && stopView != null && stopView.getParent() != null) {
                return true;
            }
            if (!cleanupOwned()) return false;
        }
        View content = activity.findViewById(android.R.id.content);
        if (!(content instanceof ViewGroup)) return false;
        ViewGroup group = (ViewGroup) content;

        Button play = makeButton(activity, PLAY_LABEL, 160f);
        Button stop = makeButton(activity, STOP_LABEL, 208f);
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
        // Retain both cleanup handles before attaching so a partial attach can be retried.
        playView = play;
        stopView = stop;
        ownedActivity = activity;
        ownedToken = token;
        clickAuthorized = false;
        try {
            group.addView(play, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            group.addView(stop, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        } catch (RuntimeException failure) {
            cleanupOwned();
            return false;
        }
        clickAuthorized = true;
        return true;
    }

    private static Button makeButton(Activity activity, String label, float y) {
        Button button = new Button(activity);
        button.setText(label);
        button.setAllCaps(false);
        button.setClickable(true);
        button.setFocusable(false);
        button.setFocusableInTouchMode(false);
        // Offset below the IME test entry at the top-left; no global window/focus changes.
        button.setX(16f);
        button.setY(y);
        return button;
    }

    /**
     * Invalidates click authorization immediately, then removes exactly the two buttons this class installed.
     * If platform removal throws, cleanup handles are retained (clicks disabled) for a bounded retry.
     */
    public static boolean uninstall(Activity activity, long token) {
        requireMainLooper();
        if (playView == null && stopView == null) return true;
        if (ownedActivity != activity || ownedToken != token) return false;
        clickAuthorized = false;
        if (playView != null) playView.setOnClickListener(null);
        if (stopView != null) stopView.setOnClickListener(null);
        return cleanupOwned();
    }

    /** Retries removal of retained owned views without reviving clicks. */
    private static boolean cleanupOwned() {
        clickAuthorized = false;
        if (!removeOwned(playView)) return false;
        if (!removeOwned(stopView)) return false;
        playView = null;
        stopView = null;
        ownedActivity = null;
        ownedToken = 0;
        return true;
    }

    private static boolean removeOwned(Button view) {
        if (view == null) return true;
        view.setOnClickListener(null);
        ViewParent parent = view.getParent();
        if (parent instanceof ViewGroup) {
            try {
                ((ViewGroup) parent).removeView(view);
            } catch (RuntimeException failure) {
                return false;
            }
        }
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
