// #170 仅用于主机测试的窄 Android 边界 stub；不对应真实设备行为。
// #170 narrow Android-boundary stubs for host tests only; not device behaviour.
package android.media;

public class AudioManager {
    public static final int AUDIOFOCUS_GAIN = 1;
    public static final int AUDIOFOCUS_GAIN_TRANSIENT = 2;
    public static final int AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK = 3;
    public static final int AUDIOFOCUS_LOSS = -1;
    public static final int AUDIOFOCUS_LOSS_TRANSIENT = -2;
    public static final int AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK = -3;
    public static final int AUDIOFOCUS_REQUEST_FAILED = 0;
    public static final int AUDIOFOCUS_REQUEST_GRANTED = 1;
    public static final int AUDIOFOCUS_REQUEST_DELAYED = 2;

    public interface OnAudioFocusChangeListener {
        void onAudioFocusChange(int focusChange);
    }

    public int nextResult = AUDIOFOCUS_REQUEST_GRANTED;
    public AudioFocusRequest lastRequest;
    public AudioFocusRequest abandonedRequest;
    public int requestCalls;
    public int abandonCalls;
    public boolean earlyCallbackOnRequest;   // requestAudioFocus 内从另一线程回调 | callback from another thread
    public int earlyFocusChange = AUDIOFOCUS_GAIN;
    public Runnable duringRequest;           // 测试注入：模拟请求进行中的并发操作 | inject concurrent action

    public int requestAudioFocus(AudioFocusRequest request) {
        requestCalls++;
        lastRequest = request;
        if (duringRequest != null) duringRequest.run();
        if (earlyCallbackOnRequest) {
            Thread thread = new Thread(() ->
                    request.getOnAudioFocusChangeListener().onAudioFocusChange(earlyFocusChange));
            thread.start();
            try {
                thread.join();
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw new RuntimeException(interrupted);
            }
        }
        // 真实平台不会在没有 opt-in 的情况下延迟授予。| A real platform does not delay without the opt-in.
        if (nextResult == AUDIOFOCUS_REQUEST_DELAYED && !request.getAcceptsDelayedFocusGain()) {
            return AUDIOFOCUS_REQUEST_FAILED;
        }
        return nextResult;
    }

    public int abandonAudioFocusRequest(AudioFocusRequest request) {
        abandonCalls++;
        abandonedRequest = request;
        return AUDIOFOCUS_REQUEST_GRANTED;
    }

    /** 测试助手：模拟平台把焦点变化送到注册时的监听器。| Test helper: deliver a platform focus change. */
    public void emit(AudioFocusRequest request, int focusChange) {
        request.getOnAudioFocusChangeListener().onAudioFocusChange(focusChange);
    }
}
