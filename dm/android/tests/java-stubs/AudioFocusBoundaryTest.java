// #170 焦点边界主机测试：只替换 android.media 边界，验证真实请求属性（含 delayed opt-in）、结果区分、
// 跨线程 early callback 的发布/暂存、回调身份/代次与放弃顺序、release 竞争不泄漏焦点。
// #170 host tests replacing only the android.media boundary: request attributes including the delayed opt-in,
// distinct results, cross-thread early-callback staging, callback identity/generation, abandon ordering and
// release racing an in-flight request without leaking focus.
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

        // 1. 真实属性与 GRANTED；默认不 opt-in delayed | real attributes, no delayed opt-in by default
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
        check(!granted.getAcceptsDelayedFocusGain(), "default request does not accept delayed focus");
        check(granted.getOnAudioFocusChangeListener() != null, "platform listener registered");

        // 2. 焦点事件映射 | distinct event semantics
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
        check(boundary.droppedCallbackCount() == 1, "late callback counted");

        // 4. delayed 需要真实 opt-in | delayed requires the real opt-in
        audio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_DELAYED;
        check(boundary.request(8) == AudioManager.AUDIOFOCUS_REQUEST_FAILED,
                "platform refuses delayed without opt-in");
        check(!boundary.isActive(), "failed delayed request owns nothing");
        check(boundary.request(9, true) == AudioManager.AUDIOFOCUS_REQUEST_DELAYED,
                "delayed result returned with opt-in");
        check(audio.lastRequest.getAcceptsDelayedFocusGain(), "real request carries the delayed opt-in");
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
        check(boundary.request(11, true) == AudioManager.AUDIOFOCUS_REQUEST_DELAYED,
                "re-request returns platform result");
        check(audio.abandonedRequest == old, "re-request abandoned the previous exact request");
        AudioFocusRequest fresh = audio.lastRequest;
        check(fresh != old, "re-request built a new platform request");
        before = recorder.calls;
        audio.emit(old, AudioManager.AUDIOFOCUS_LOSS);
        check(recorder.calls == before, "old request callback is dropped after replacement");
        audio.emit(fresh, AudioManager.AUDIOFOCUS_LOSS_TRANSIENT);
        check(recorder.generation == 11 && recorder.event == AudioFocusBoundary.EVENT_LOSS_TRANSIENT,
                "new request callback forwarded with new generation");

        // 7. 跨线程 early callback：requestAudioFocus 未返回即从另一线程回调；真实 DELAYED 时必须转发一次。
        // Cross-thread early callback: delivered before requestAudioFocus returns; with a genuine DELAYED it must be
        // forwarded exactly once with the request generation.
        AudioManager earlyAudio = new AudioManager();
        earlyAudio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_DELAYED;
        earlyAudio.earlyCallbackOnRequest = true;
        Recorder earlyRecorder = new Recorder();
        AudioFocusBoundary early = new AudioFocusBoundary(earlyAudio, earlyRecorder);
        check(early.request(21, true) == AudioManager.AUDIOFOCUS_REQUEST_DELAYED, "early request delayed");
        check(earlyRecorder.calls == 1, "early cross-thread focus callback is not lost");
        check(earlyRecorder.generation == 21 && earlyRecorder.event == AudioFocusBoundary.EVENT_GAIN,
                "early callback forwarded with its generation");
        check(early.isActive(), "early callback did not fabricate ownership before the result");

        // 8. early callback 但平台 FAILED：不得凭空转发，也不得建立所有权。
        // Early callback but platform FAILED: it must be discarded and no ownership invented.
        AudioManager failedEarlyAudio = new AudioManager();
        failedEarlyAudio.nextResult = AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        failedEarlyAudio.earlyCallbackOnRequest = true;
        Recorder failedEarlyRecorder = new Recorder();
        AudioFocusBoundary failedEarly = new AudioFocusBoundary(failedEarlyAudio, failedEarlyRecorder);
        check(failedEarly.request(22, false) == AudioManager.AUDIOFOCUS_REQUEST_FAILED, "failed early request");
        check(failedEarlyRecorder.calls == 0, "early callback is discarded when the platform fails");
        check(!failedEarly.isActive(), "failed early request owns nothing");
        check(failedEarly.droppedCallbackCount() == 1, "discarded early callback counted");

        // 9. release 与 in-flight 请求竞争：平台已授予也必须马上放弃，不留下无主焦点。
        // Release racing an in-flight request: a granted focus must be abandoned, never leaked.
        AudioManager raceAudio = new AudioManager();
        Recorder raceRecorder = new Recorder();
        AudioFocusBoundary racing = new AudioFocusBoundary(raceAudio, raceRecorder);
        raceAudio.duringRequest = racing::release;
        check(racing.request(23) == AudioManager.AUDIOFOCUS_REQUEST_FAILED,
                "request racing release reports failure");
        check(!racing.isActive(), "no ownership after release race");
        check(raceAudio.abandonCalls == 1, "granted focus was abandoned after the release race");
        check(raceRecorder.calls == 0, "no callback forwarded after the release race");

        // 10. release 幂等并永久拒绝后续请求 | release is idempotent and refuses later requests
        boundary.release();
        boundary.release();
        check(!boundary.isActive(), "release abandons the active request");
        int requestsBefore = audio.requestCalls;
        check(boundary.request(12) == AudioManager.AUDIOFOCUS_REQUEST_FAILED, "request after release fails");
        check(audio.requestCalls == requestsBefore, "request after release does not touch the platform");
        before = recorder.calls;
        audio.emit(fresh, AudioManager.AUDIOFOCUS_GAIN);
        check(recorder.calls == before, "callback after release is dropped");

        System.out.println("AudioFocusBoundaryTest: " + checks + " checks, " + failures + " failures");
        if (failures != 0) System.exit(1);
    }
}
