package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;

/**
 * #211 focused host test for the actual DmAudioTouchEntry lifecycle and click authorization.
 * The control sink is the only replaced boundary; production uses the registered JNI methods.
 */
public final class DmAudioTouchEntryTest {
    static final class Sink implements DmAudioTouchEntry.ControlSink {
        int plays, stops;
        long lastToken;

        @Override
        public boolean play(long token) {
            plays++;
            lastToken = token;
            return true;
        }

        @Override
        public boolean stop(long token) {
            stops++;
            lastToken = token;
            return true;
        }
    }

    private static int checks, failures;

    private static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
            failures++;
            System.err.println("FAIL: " + what);
        }
    }

    private static Activity activity() {
        Activity activity = new Activity();
        activity.setContentView(new ViewGroup(activity));
        return activity;
    }

    public static void main(String[] args) {
        Sink sink = new Sink();
        DmAudioTouchEntry.setTestSink(sink);
        Activity activity = activity();
        ViewGroup content = activity.findViewById(android.R.id.content);

        check(DmAudioTouchEntry.install(activity, 77), "install accepted");
        check(content.getChildCount() == 1, "one owned layout container");
        LinearLayout box = (LinearLayout) content.getChildAt(0);
        check(box.getOrientation() == LinearLayout.VERTICAL, "vertical measured layout");
        check(box.getChildCount() == 2, "exactly two owned buttons");
        Button play = (Button) box.getChildAt(0);
        Button stop = (Button) box.getChildAt(1);
        check("诊断音频 播放".contentEquals(play.getText()), "play label");
        check("诊断音频 停止".contentEquals(stop.getText()), "stop label");
        check(!play.isFocusable() && !stop.isFocusable(), "buttons never take text/game focus");
        // Large font/density: 260px and 320px measured buttons with a 24px gap must not share pixels.
        int gap = Math.round(8 * 3.0f);
        check(DmAudioTouchEntry.stackedBoundsDisjoint(new int[]{260, 320}, gap),
                "large measured buttons are disjoint with density-aware gap");
        check(!DmAudioTouchEntry.stackedBoundsDisjoint(new int[]{260, 320}, -60),
                "overlap would be detected, so the check is meaningful");
        check(sink.plays == 0 && sink.stops == 0, "install never calls native audio");

        check(play.performClick(), "play click dispatched");
        check(sink.plays == 1 && sink.lastToken == 77, "play routes the owner token once");
        check(stop.performClick(), "stop click dispatched");
        check(sink.stops == 1 && sink.lastToken == 77, "stop routes the same owner token");

        check(!DmAudioTouchEntry.uninstall(activity, 78), "wrong token uninstall rejected");
        check(content.getChildCount() == 1, "wrong token keeps the owned container");
        check(DmAudioTouchEntry.uninstall(activity, 77), "uninstall accepted");
        check(content.getChildCount() == 0, "uninstall removes both buttons");
        check(!play.performClick(), "removed play button has no listener");
        check(sink.plays == 1 && sink.stops == 1, "stale click has no native side effect");

        // 第二个 owner 不得覆盖当前 owner | a second owner cannot steal the entry
        Activity other = activity();
        check(DmAudioTouchEntry.install(activity, 79), "reinstall accepted");
        check(!DmAudioTouchEntry.install(other, 80), "second owner rejected");
        check(((ViewGroup) activity.findViewById(android.R.id.content)).getChildCount() == 1,
                "current owner container preserved");
        check(DmAudioTouchEntry.uninstall(activity, 79), "current owner cleanup");

        // remove 失败：先失效点击，保留 handle，可重试成功
        Activity failing = new Activity();
        FailingGroup group = new FailingGroup(failing);
        failing.setContentView(group);
        check(DmAudioTouchEntry.install(failing, 81), "install on failing group");
        group.failRemove = true;
        int playsBefore = sink.plays;
        check(!DmAudioTouchEntry.uninstall(failing, 81), "remove failure reported");
        Button retained = (Button) ((LinearLayout) group.getChildAt(0)).getChildAt(0);
        check(!retained.performClick(), "retained button click is already invalidated");
        check(sink.plays == playsBefore, "no native play during failed removal");
        group.failRemove = false;
        check(DmAudioTouchEntry.uninstall(failing, 81), "cleanup retry succeeds");
        check(group.getChildCount() == 0, "retry removed the retained buttons");

        System.out.println("DmAudioTouchEntryTest: " + checks + " checks, " + failures + " failures");
        if (failures != 0) System.exit(1);
    }

    private static final class FailingGroup extends ViewGroup {
        boolean failRemove;

        FailingGroup(Activity activity) {
            super(activity);
        }

        @Override
        public void removeView(View child) {
            if (failRemove) throw new IllegalStateException("remove failed");
            super.removeView(child);
        }
    }
}
