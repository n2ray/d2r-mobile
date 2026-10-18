// #170 焦点边界主机测试：只替换 android.media 边界，验证真实请求属性、结果区分、回调身份/代次与放弃顺序。
// #170 host tests replacing only the android.media boundary: request attributes, distinct results, callback identity
// and generation, abandon identity/order, late/duplicate rejection.
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;

import org.d2rmobile.android.diagnostic.AudioFocusBoundary;

public final class AudioFocusBoundaryTest {
    private static int checks;
    private static int failures;

    private static void check(boolean condition, String what) {
        checks++;
        if (!condition) {
            failures++;
            System.err.println("FAIL: " + what);
        }
    }

    private static final class Recorder implements AudioFocusBoundary.Listener {
        int calls;
        int generation = -1;
        int event = -1;

        @Override
        public void onAudioFocusEvent(int generation, int event) {
            calls++;
            this.generation = generation;
            this.event = event;
        }
    }

    public static void main(String[] args) {
        AudioManager audio = new AudioManager();
        Recorder recorder = new Recorder();
        AudioFocusBoundary boundary = new AudioFocusBoundary(audio, recorder);

        // 构造不自动请求 | no request on construction
        check(audio.requestCalls == 0, "construction must not request focus");
        check(!boundary.isActive(), "new boundary is inactive");

        // 1. 真实属性与 GRANTED | real attributes and granted result
        check(boundary.request(7) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED, "granted result returned");
        check(audio.requestCalls == 1, "exactly one platform request");
        check(boundary.isActive(), "granted request is owned");
        check(boundary.generation() == 7, "generation retained");
        AudioFocusRequest granted = audio.lastRequest;
        check(granted != null, "request object retained");
        check(granted.getFocusGain() == AudioManager.AUDIOFOCUS_GAIN, "AUDIOFOCUS_GAIN requested");
        check(granted.getAudioAttributes().getUsage() == AudioAttributes.USAGE_GAME, "USAGE_GAME attribute");
        check(granted.getAudioAttributes().getContentType() == AudioAttributes.CONTENT_TYPE_MUSIC,
                "CONTENT_TYPE_MUSIC attribute");
        check(!granted.willPauseWhenDucked(), "willPauseWhenDucked=false");
        check(granted.getOnAudioFocusChangeListener() != null, "platform listener registered");

        // 2. 焦点事件映射，保留 GAIN/LOSS/TRANSIENT/DUCK 语义 | distinct event semantics
        audio.emit(granted, AudioManager.AUDIOFOCUS_GAIN);
        check(recorder.calls == 1 && recorder.generation == 7
                && recorder.event == AudioFocusBoundary.EVENT_GAIN, "gain forwarded with generation");
        audio.emit(granted, AudioManager.AUDIOFOCUS_LOSS);
        check(recorder.event == AudioFocusBoundary.EVENT_LOSS, "loss forwarded");
        audio.emit(granted, AudioManager.AUDIOFOCUS_LOSS_TRANSIENT);
        check(recorder.event == AudioFocusBoundary.EVENT_LOSS_TRANSIENT, "transient loss forwarded");
        audio.emit(granted, AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK);
        check(recorder.event == AudioFocusBoundary.EVENT_CAN_DUCK, "can-duck forwarded");
        int before = recorder.calls;
        audio.emit(granted, 12345);
        check(recorder.calls == before, "unknown platform code yields no invented event");

        // 3. 放弃指向实际所有者且幂等 | abandon targets the actual owner and is idempotent
        check(boundary.abandon(), "abandon reports a real abandonment");
        check(audio.abandonCalls == 1, "one platform abandon");
        check(audio.abandonedRequest == granted, "abandon uses the exact owned request");
        check(!boundary.isActive(), "abandon clears ownership");
        check(!boundary.abandon(), "second abandon is a no-op");
        check(audio.abandonCalls == 1, "second abandon does not call the platform");
        before = recorder.calls;
        audio.emit(granted, AudioManager.AUDIOFOCUS_GAIN);
        check(recorder.calls == before, "late callback after abandon is dropped");
        check(boundary.lateCallbackCount() == 1, "late callback counted");

        // 4. DELAYED 与 GRANTED 区分 | delayed is distinct from granted
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_DELAYED;
        check(boundary.request(9) == AudioManager.AUDIOFOCUS_REQUEST_DELAYED, "delayed result returned");
        check(boundary.isActive(), "delayed request is owned");
        AudioFocusRequest delayed = audio.lastRequest;
        audio.emit(delayed, AudioManager.AUDIOFOCUS_GAIN);
        check(recorder.generation == 9 && recorder.event == AudioFocusBoundary.EVENT_GAIN,
                "delayed grant forwarded with its generation");

        // 5. FAILED 不建立所有权 | failed establishes no ownership
        AudioManager failedAudio = new AudioManager();
        failedAudio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        Recorder failedRecorder = new Recorder();
        AudioFocusBoundary failed = new AudioFocusBoundary(failedAudio, failedRecorder);
        check(failed.request(3) == AudioManager.AUDIOFOCUS_REQUEST_FAILED, "failed result returned");
        check(!failed.isActive(), "failed request owns nothing");
        check(!failed.abandon(), "failed request has nothing to abandon");
        check(failedAudio.abandonCalls == 0, "no platform abandon after failure");

        // 6. 重新请求：旧请求被真实放弃，旧回调失效，新回调带新代次 | re-request replaces ownership
        AudioFocusRequest old = audio.lastRequest;
        check(boundary.request(11) == AudioManager.AUDIOFOCUS_REQUEST_DELAYED, "re-request returns platform result");
        check(audio.abandonedRequest == old, "re-request abandoned the previous exact request");
        AudioFocusRequest fresh = audio.lastRequest;
        check(fresh != old, "re-request built a new platform request");
        before = recorder.calls;
        audio.emit(old, AudioManager.AUDIOFOCUS_LOSS);
        check(recorder.calls == before, "old request callback is late after replacement");
        audio.emit(fresh, AudioManager.AUDIOFOCUS_LOSS_TRANSIENT);
        check(recorder.generation == 11 && recorder.event == AudioFocusBoundary.EVENT_LOSS_TRANSIENT,
                "new request callback forwarded with new generation");

        // 7. release 幂等并永久拒绝后续请求 | release is idempotent and refuses later requests
        boundary.release();
        boundary.release();
        check(!boundary.isActive(), "release abandons the active request");
        int requestsBefore = audio.requestCalls;
        check(boundary.request(12) == AudioManager.AUDIOFOCUS_REQUEST_FAILED, "request after release fails");
        check(audio.requestCalls == requestsBefore, "request after release does not touch the platform");
        before = recorder.calls;
        audio.emit(fresh, AudioManager.AUDIOFOCUS_GAIN);
        check(recorder.calls == before, "callback after release is dropped");
        check(boundary.lateCallbackCount() == 3, "all late callbacks counted");

        System.out.println("AudioFocusBoundaryTest: " + checks + " checks, " + failures + " failures");
        if (failures != 0) System.exit(1);
    }
}
