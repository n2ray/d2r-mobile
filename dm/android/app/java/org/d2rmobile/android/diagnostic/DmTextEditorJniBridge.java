package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.content.Context;
import android.os.Looper;
import android.view.inputmethod.InputMethodManager;

/**
 * Narrow JNI bridge for the accepted Activity text host.
 *
 * <p>The native caller owns one bridge per text session and invalidates the
 * session before releasing the host. Java forwards the actual bounded event
 * payload (commit/delete/submit or an empty cancel) and its UTF-16 code-unit
 * length to native; it never logs user text and never forwards a raw C
 * pointer.</p>
 */
public final class DmTextEditorJniBridge implements DmInputConnection.Sink {
    public static final int EVENT_COMMIT = 0;
    public static final int EVENT_DELETE = 1;
    public static final int EVENT_SUBMIT = 2;
    public static final int EVENT_CANCEL = 3;

    /** Test seam only; production passes null and calls the registered native method. */
    interface EventSink {
        void onTextEvent(long session, int event, String payload, int codeUnits);

        default void onImeInsets(long session, long generation, long sequence,
                                 boolean known, boolean visible,
                                 int left, int top, int right, int bottom) { }

        default void onImeInsetsClear(long session) { }
    }

    private final DmTextEditorHost host;
    private final Looper mainLooper;
    private final EventSink eventSink;
    private long session;
    private boolean released;

    public DmTextEditorJniBridge(Activity activity, long session) {
        this(activity, session, null,
                (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE));
    }

    DmTextEditorJniBridge(Activity activity, long session, EventSink eventSink) {
        this(activity, session, eventSink,
                (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE));
    }

    DmTextEditorJniBridge(Activity activity, long session, EventSink eventSink,
                          InputMethodManager inputMethodManager) {
        if (activity == null) throw new IllegalArgumentException("activity == null");
        if (session == 0) throw new IllegalArgumentException("session == 0");
        this.mainLooper = Looper.getMainLooper();
        if (mainLooper == null) throw new IllegalStateException("no main looper");
        this.session = session;
        this.eventSink = eventSink;
        this.host = new DmTextEditorHost(activity, inputMethodManager);
        this.host.setImeInsetsListener(this::emitImeInsets);
    }

    public boolean show() {
        requireMainLooper();
        if (released || session == 0) return false;
        return host.show(this);
    }

    public boolean hide() {
        requireMainLooper();
        if (released) return false;
        emitImeInsetsClear();
        return host.hide();
    }

    /** Invalidates the native session before releasing, so late callbacks are harmless. */
    public boolean release() {
        requireMainLooper();
        if (released) return false;
        emitImeInsetsClear();
        released = true;
        session = 0;
        return host.release();
    }

    public boolean isActive() {
        return !released && session != 0 && host.isSessionActive();
    }

    @Override
    public void onCommit(CharSequence text) {
        emit(EVENT_COMMIT, text);
    }

    @Override
    public void onDelete(CharSequence deletedText) {
        emit(EVENT_DELETE, deletedText);
    }

    @Override
    public void onSubmit(CharSequence text) {
        emit(EVENT_SUBMIT, text);
    }

    @Override
    public void onCancel() {
        emitCancel();
    }

    private void emit(int event, CharSequence text) {
        if (released || session == 0) return;
        int codeUnits = length(text);
        if (codeUnits < 0 || codeUnits > DmInputConnection.MAX_TEXT_UTF16_UNITS) return;
        String payload = text == null ? null : text.toString();
        if (payload != null && payload.length() != codeUnits) return;
        if (eventSink != null) eventSink.onTextEvent(session, event, payload, codeUnits);
        else nativeTextEvent(session, event, payload, codeUnits);
    }

    private void emitCancel() {
        if (released || session == 0) return;
        if (eventSink != null) eventSink.onTextEvent(session, EVENT_CANCEL, null, 0);
        else nativeTextEvent(session, EVENT_CANCEL, null, 0);
    }

    private void emitImeInsets(DmImeInsetsObservation observation) {
        if (released || session == 0 || observation == null) return;
        if (observation.sessionGeneration <= 0 || observation.sequence <= 0) return;
        if (eventSink != null) {
            eventSink.onImeInsets(session, observation.sessionGeneration, observation.sequence,
                    observation.known, observation.imeVisible,
                    observation.left, observation.top, observation.right, observation.bottom);
        } else {
            nativeImeInsets(session, observation.sessionGeneration, observation.sequence,
                    observation.known, observation.imeVisible,
                    observation.left, observation.top, observation.right, observation.bottom);
        }
    }

    private void emitImeInsetsClear() {
        if (released || session == 0) return;
        if (eventSink != null) eventSink.onImeInsetsClear(session);
        else nativeImeInsetsClear(session);
    }

    private static int length(CharSequence text) {
        return text == null ? 0 : text.length();
    }

    private void requireMainLooper() {
        if (Looper.myLooper() != mainLooper) {
            throw new IllegalStateException("DmTextEditorJniBridge must be called on the main looper");
        }
    }

    private static native void nativeTextEvent(long session, int event, String text, int codeUnits);
    private static native void nativeImeInsets(long session, long generation, long sequence,
                                               boolean known, boolean visible,
                                               int left, int top, int right, int bottom);
    private static native void nativeImeInsetsClear(long session);
}
