package org.d2rmobile.android.diagnostic;

import android.view.inputmethod.EditorInfo;

import java.util.ArrayList;
import java.util.List;

/**
 * Host-only focused tests for the actual DmInputConnection implementation.
 * Compiled and run against the real android.jar; no Android device is used.
 */
public final class DmInputConnectionTest {
    private static int checks;

    private static final class Recorder implements DmInputConnection.Sink {
        final List<String> commits = new ArrayList<>();
        final List<String> deletes = new ArrayList<>();
        final List<String> submits = new ArrayList<>();
        int cancels;

        @Override public void onCommit(CharSequence text) { commits.add(text.toString()); }
        @Override public void onDelete(CharSequence text) { deletes.add(text.toString()); }
        @Override public void onSubmit(CharSequence text) { submits.add(text.toString()); }
        @Override public void onCancel() { cancels++; }
    }

    private static void check(boolean condition, String label) {
        checks++;
        if (!condition) throw new AssertionError("FAIL: " + label);
    }

    private static String before(DmInputConnection connection) {
        CharSequence value = connection.getTextBeforeCursor(4096, 0);
        return value == null ? "" : value.toString();
    }

    private static void testLatinCommit() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.commitText("hello", 1), "latin commit accepted");
        check("hello".equals(before(c)), "latin text visible");
        check(sink.commits.size() == 1 && "hello".equals(sink.commits.get(0)), "one latin commit");
        check(sink.deletes.isEmpty() && sink.submits.isEmpty(), "latin no unrelated callbacks");
    }

    private static void testChineseComposition() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.setComposingText("n", 1), "first pinyin composing accepted");
        check(c.setComposingText("ni", 1), "second pinyin composing accepted");
        check(c.setComposingText("你", 1), "hanzi composing accepted");
        check(sink.commits.isEmpty(), "composition does not commit");
        check("你".equals(before(c)), "composing text visible at cursor");
        check(c.commitText("你", 1), "final commit accepted");
        check(sink.commits.size() == 1 && "你".equals(sink.commits.get(0)), "commit exactly once");
        check(c.finishComposingText(), "finish after commit accepted");
        check(sink.commits.size() == 1, "finish does not duplicate commit");
    }

    private static void testFinishWithoutExplicitCommit() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.setComposingText("你", 1), "composing accepted");
        check(c.finishComposingText(), "finish accepted");
        check(sink.commits.size() == 1 && "你".equals(sink.commits.get(0)), "finish commits once");
        check(c.finishComposingText(), "second finish accepted");
        check(sink.commits.size() == 1, "second finish is a no-op");
    }

    private static void testSurrogateDeletion() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.commitText("A😀B", 1), "emoji commit accepted");
        check(c.deleteSurroundingTextInCodePoints(1, 0), "codepoint delete B accepted");
        check("A😀".equals(before(c)), "one codepoint removes B");
        check(sink.deletes.size() == 1 && "B".equals(sink.deletes.get(0)), "delete callback B");
        check(c.deleteSurroundingTextInCodePoints(1, 0), "codepoint delete emoji accepted");
        check("A".equals(before(c)), "one codepoint removes whole emoji");
        check(sink.deletes.size() == 2 && "😀".equals(sink.deletes.get(1)), "delete callback whole emoji");

        Recorder units = new Recorder();
        DmInputConnection d = new DmInputConnection(units);
        check(d.commitText("A😀", 1), "emoji unit commit accepted");
        check(d.deleteSurroundingText(1, 0), "one unit delete accepted without split");
        check("A".equals(before(d)), "unit delete removes surrogate pair safely");
        check(units.deletes.size() == 1 && "😀".equals(units.deletes.get(0)), "unit delete callback pair");
    }

    private static void testLengthBound() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        StringBuilder maximum = new StringBuilder();
        for (int i = 0; i < DmInputConnection.MAX_TEXT_UTF16_UNITS; i++) maximum.append('x');
        check(c.commitText(maximum, 1), "maximum length accepted");
        check(!c.commitText("y", 1), "over-limit commit rejected");
        check(before(c).length() == DmInputConnection.MAX_TEXT_UTF16_UNITS, "bound preserved");
        check(sink.commits.size() == 1, "over-limit has no commit callback");
    }

    private static void testMalformedUtf16() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.commitText("ok", 1), "baseline commit accepted");
        check(!c.commitText("\uD83D", 1), "lone high surrogate rejected");
        check(!c.commitText("\uDE00", 1), "lone low surrogate rejected");
        check(!c.setComposingText("a\uD83Db", 1), "malformed composing rejected");
        check("ok".equals(before(c)), "malformed input preserves committed state");
        check(sink.commits.size() == 1, "malformed input has no commit callback");

        check(c.setComposingText("你", 1), "valid composing before malformed replacement");
        check(!c.setComposingText("\uD83D", 1), "malformed replacement rejected");
        check("ok你".equals(before(c)), "malformed replacement preserves composing");
    }

    private static void testSubmitCancelAndStaleSession() {
        Recorder sink = new Recorder();
        DmInputConnection c = new DmInputConnection(sink);
        check(c.commitText("done", 1), "submit baseline accepted");
        check(c.performEditorAction(EditorInfo.IME_ACTION_DONE), "editor done accepted");
        check(sink.submits.size() == 1 && "done".equals(sink.submits.get(0)), "submit text once");
        check(!c.commitText("late", 1), "late commit after submit rejected");
        check(sink.commits.size() == 1, "late commit has no callback");

        Recorder cancelSink = new Recorder();
        DmInputConnection cancelled = new DmInputConnection(cancelSink);
        check(cancelled.commitText("cancel", 1), "cancel baseline accepted");
        check(cancelled.cancel(), "cancel accepted");
        check(cancelSink.cancels == 1, "cancel callback once");
        check(!cancelled.commitText("late", 1), "late commit after cancel rejected");
        check(cancelSink.commits.size() == 1, "no late commit after cancel");

        Recorder oldSink = new Recorder();
        DmInputConnection old = new DmInputConnection(oldSink);
        check(old.commitText("old", 1), "old session commit");
        old.closeConnection();
        check(!old.commitText("stale", 1), "closed old connection rejects commit");
        Recorder newSink = new Recorder();
        DmInputConnection newer = new DmInputConnection(newSink);
        check(newer.commitText("new", 1), "new session commit");
        check(oldSink.commits.size() == 1, "old sink unchanged by new session");
        check(newSink.commits.size() == 1 && "new".equals(newSink.commits.get(0)), "new sink only new text");

        Recorder resetSink = new Recorder();
        DmInputConnection reset = new DmInputConnection(resetSink);
        check(reset.commitText("before", 1), "reset baseline accepted");
        reset.reset();
        check(!reset.commitText("after", 1), "reset terminal rejects commit");
        check(resetSink.commits.size() == 1, "reset has no late callback");
    }

    public static void main(String[] args) {
        testLatinCommit();
        testChineseComposition();
        testFinishWithoutExplicitCommit();
        testSurrogateDeletion();
        testLengthBound();
        testMalformedUtf16();
        testSubmitCancelAndStaleSession();
        System.out.println("PASS ime boundary: " + checks + " checks");
    }
}
