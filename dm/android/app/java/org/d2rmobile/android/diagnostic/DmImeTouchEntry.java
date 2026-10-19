package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.widget.Button;

/**
 * Single owned diagnostic touch entry for the accepted NativeActivity text
 * host.
 *
 * <p>The launcher installs one clearly labelled {@link Button} into the
 * existing {@code android.R.id.content}; it never replaces the NativeActivity
 * surface, HUD or other views. Construction/installation does not request the
 * IME. Only a user click asks the registered native owner, using an opaque
 * numeric token rather than a native pointer.</p>
 *
 * <p>Native validates the owner/token and the existing active-App text guard
 * before calling the real {@code text_begin}. Late clicks after teardown,
 * paused/focus-lost/windowless Apps and a second owner are inert. Teardown
 * invalidates the token before removing only this owned button.</p>
 */
public final class DmImeTouchEntry {
    private static final String LABEL = "IME test / 输入测试";

    /** Narrow test seam; production uses the registered native method. */
    interface RequestSink {
        boolean request(long token);
    }

    private static Button ownedView;
    private static Activity ownedActivity;
    private static long ownedToken;
    private static RequestSink testSink;

    private DmImeTouchEntry() {
    }

    public static boolean install(Activity activity, long token) {
        requireMainLooper();
        if (activity == null || token == 0) return false;
        if (ownedView != null) {
            return ownedActivity == activity && ownedToken == token
                    && ownedView.getParent() != null;
        }
        View content = activity.findViewById(android.R.id.content);
        if (!(content instanceof ViewGroup)) return false;
        Button button = new Button(activity);
        button.setText(LABEL);
        button.setAllCaps(false);
        button.setClickable(true);
        // A launcher must consume its own click without taking text/game focus.
        button.setFocusable(false);
        button.setFocusableInTouchMode(false);
        button.setOnClickListener(view -> {
            if (ownedView == view && ownedActivity == activity && ownedToken == token) {
                request(token);
            }
        });
        ViewGroup group = (ViewGroup) content;
        try {
            group.addView(button, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT));
        } catch (RuntimeException failure) {
            button.setOnClickListener(null);
            ViewParent parent = button.getParent();
            if (parent instanceof ViewGroup) {
                ((ViewGroup) parent).removeView(button);
            }
            return false;
        }
        ownedView = button;
        ownedActivity = activity;
        ownedToken = token;
        return true;
    }

    /**
     * Invalidates the owned identity before removing exactly the button this
     * class installed. A stale token or a different Activity is inert.
     */
    public static boolean uninstall(Activity activity, long token) {
        requireMainLooper();
        if (ownedView == null || ownedActivity != activity || ownedToken != token) {
            return false;
        }
        Button view = ownedView;
        ownedView = null;
        ownedActivity = null;
        ownedToken = 0;
        view.setOnClickListener(null);
        ViewParent parent = view.getParent();
        if (parent instanceof ViewGroup) {
            ((ViewGroup) parent).removeView(view);
        }
        return true;
    }

    /** Test-only sink; never set by production callers. */
    static void setTestSink(RequestSink sink) {
        testSink = sink;
    }

    private static boolean request(long token) {
        RequestSink sink = testSink;
        return sink != null ? sink.request(token) : nativeRequest(token);
    }

    private static void requireMainLooper() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            throw new IllegalStateException("DmImeTouchEntry must run on the main looper");
        }
    }

    private static native boolean nativeRequest(long token);
}
