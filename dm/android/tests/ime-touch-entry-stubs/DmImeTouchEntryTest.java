package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;

/** Focused actual launcher test; only Java-to-native delivery is replaced. */
public final class DmImeTouchEntryTest {
    private static int checks;

    private static final class Recorder implements DmImeTouchEntry.RequestSink {
        int calls;
        long token;
        boolean result = true;

        @Override
        public boolean request(long value) {
            calls++;
            token = value;
            return result;
        }
    }

    private static void check(boolean condition, String label) {
        checks++;
        if (!condition) throw new AssertionError("FAIL: " + label);
    }

    private static ViewGroup content(Activity activity) {
        return activity.findViewById(android.R.id.content);
    }

    public static void main(String[] args) {
        Looper.prepareMainLooper();
        Activity activity = new Activity();
        ViewGroup content = new ViewGroup(activity);
        View nativeSurface = new View(activity);
        content.addView(nativeSurface);
        activity.setContentView(content);
        Recorder requests = new Recorder();
        DmImeTouchEntry.setTestSink(requests);

        check(DmImeTouchEntry.install(activity, 42L), "install owned button");
        check(content.getChildCount() == 2, "existing surface preserved, one button added");
        check(content.getChildAt(0) == nativeSurface, "existing content remains first");
        Button button = (Button) content.getChildAt(1);
        check("IME test / 输入测试".contentEquals(button.getText()), "diagnostic label");
        check(!button.isFocusable() && !button.isFocusableInTouchMode(), "does not steal focus");
        check(requests.calls == 0, "install/constructor does not request IME");
        check(DmImeTouchEntry.install(activity, 42L), "repeat install is idempotent");
        check(content.getChildCount() == 2, "repeat install does not duplicate view");

        check(button.performClick(), "button consumes click");
        check(requests.calls == 1 && requests.token == 42L, "click forwards checked token");
        requests.result = false;
        check(button.performClick(), "click still consumed when request rejected");
        check(requests.calls == 2, "request called again, no synthetic input");

        check(!DmImeTouchEntry.uninstall(activity, 41L), "stale token cannot remove");
        check(content.getChildCount() == 2, "stale token leaves owned view");
        check(DmImeTouchEntry.uninstall(activity, 42L), "owned token removes");
        check(content.getChildCount() == 1 && content.getChildAt(0) == nativeSurface,
                "only owned button removed");
        int calls = requests.calls;
        check(!button.performClick(), "detached button has no listener");
        check(requests.calls == calls, "late click after teardown inert");

        check(DmImeTouchEntry.install(activity, 7L), "reinstall after teardown");
        Button second = (Button) content.getChildAt(1);
        Activity other = new Activity();
        ViewGroup otherContent = new ViewGroup(other);
        other.setContentView(otherContent);
        check(!DmImeTouchEntry.install(other, 8L), "second owner/activity rejected");
        check(otherContent.getChildCount() == 0, "second activity untouched");
        check(DmImeTouchEntry.uninstall(activity, 7L), "first owner still removable");
        check(!second.performClick(), "removed second view inert");

        Activity empty = new Activity();
        check(!DmImeTouchEntry.install(empty, 9L), "missing content rejected");
        System.out.println("PASS ime touch entry: " + checks + " checks");
    }
}
