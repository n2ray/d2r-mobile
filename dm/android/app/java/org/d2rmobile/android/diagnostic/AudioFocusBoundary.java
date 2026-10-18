// #170 真实 Android AudioManager 焦点边界：只请求/放弃平台焦点，并把平台回调按调用方 generation 转发。
// 不创建或操作 AAudio、不修改原生策略；本类不自动请求焦点。后续 JNI 在串行控制线程调用 request/abandon/release，
// 把 listener 事件入队回同一控制线程，再交给 dm_af_policy。
// #170 real Android AudioManager focus boundary: requests/abandons platform focus only and forwards platform
// callbacks tagged with the caller's generation. It never creates or touches AAudio and never mutates the native
// policy, and it never requests focus on construction. A later JNI layer calls request/abandon/release on the
// serialized control thread and enqueues listener events back onto that same thread before dispatching to policy.
//
// Thread contract: request/abandon/release/generation/isActive may be called from any one caller thread but must be
// serialized by the owner; the platform listener set through AudioFocusRequest.Builder is delivered by Android on
// the main looper. The listener therefore must not call AAudio or native policy synchronously: enqueue to the
// serialized control thread. No method here blocks waiting for a focus callback.
package org.d2rmobile.android.diagnostic;

import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;

public final class AudioFocusBoundary {
    /** 与 dm_af_event 的焦点子集一一对应；JNI 侧直接映射。| Mirrors the focus subset of dm_af_event for JNI. */
    public static final int EVENT_GAIN = 0;
    public static final int EVENT_LOSS = 1;
    public static final int EVENT_LOSS_TRANSIENT = 2;
    public static final int EVENT_CAN_DUCK = 3;

    /** 平台调用结果原样返回 AudioManager.AUDIOFOCUS_REQUEST_GRANTED/DELAYED/FAILED。 */
    public interface Listener {
        /** 平台回调线程（主 looper）；不得在此调用 AAudio 或原生策略。| Main-looper callback; never call AAudio here. */
        void onAudioFocusEvent(int generation, int event);
    }

    private final AudioManager audioManager;
    private final Listener listener;
    private AudioFocusRequest activeRequest;
    private int activeGeneration;
    private long nextToken = 1;
    private long activeToken;
    private boolean released;
    private int lateCallbacks;

    public AudioFocusBoundary(AudioManager audioManager, Listener listener) {
        if (audioManager == null) throw new IllegalArgumentException("audioManager == null");
        if (listener == null) throw new IllegalArgumentException("listener == null");
        this.audioManager = audioManager;
        this.listener = listener;
    }

    /**
     * 请求 AUDIOFOCUS_GAIN（游戏用途）。返回平台真实结果；只有 GRANTED/DELAYED 才建立所有者身份。
     * 若已有请求，先放弃旧请求再申请，旧请求的迟到回调因 token 失效被丢弃。
     * Requests AUDIOFOCUS_GAIN for game usage and returns the real platform result. Only GRANTED/DELAYED establish
     * ownership. An existing request is abandoned first, invalidating its token so late callbacks are dropped.
     */
    public int request(int generation) {
        if (released) return AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        abandon();
        final long token = nextToken++;
        final AudioManager.OnAudioFocusChangeListener platformListener =
                new AudioManager.OnAudioFocusChangeListener() {
                    @Override
                    public void onAudioFocusChange(int focusChange) {
                        dispatch(token, generation, focusChange);
                    }
                };
        final AudioAttributes attributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build();
        final AudioFocusRequest request = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(attributes)
                .setWillPauseWhenDucked(false)
                .setOnAudioFocusChangeListener(platformListener)
                .build();
        final int result = audioManager.requestAudioFocus(request);
        if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
                || result == AudioManager.AUDIOFOCUS_REQUEST_DELAYED) {
            activeRequest = request;
            activeGeneration = generation;
            activeToken = token;
        } else {
            activeRequest = null;
            activeGeneration = 0;
            activeToken = 0;
        }
        return result;
    }

    /**
     * 放弃当前请求（幂等）。只放弃自己实际持有的那个 AudioFocusRequest 实例；无活跃请求时不调用平台。
     * 返回是否真的放弃了平台请求。| Abandons exactly the retained request; no platform call when idle.
     */
    public boolean abandon() {
        final AudioFocusRequest request = activeRequest;
        if (request == null) return false;
        activeToken = 0;            // 先失效 token：随后的迟到回调不能再影响新状态
        activeRequest = null;
        activeGeneration = 0;
        audioManager.abandonAudioFocusRequest(request);
        return true;
    }

    /** 释放边界（幂等）：放弃活跃请求并永久拒绝后续 request；迟到回调被丢弃。 */
    public void release() {
        if (released) return;
        abandon();
        released = true;
    }

    public boolean isActive() { return activeRequest != null; }

    public int generation() { return activeGeneration; }

    public int lateCallbackCount() { return lateCallbacks; }

    private void dispatch(long token, int generation, int focusChange) {
        if (released || token != activeToken || activeRequest == null) {
            lateCallbacks++;
            return;
        }
        final int event;
        if (focusChange == AudioManager.AUDIOFOCUS_GAIN) event = EVENT_GAIN;
        else if (focusChange == AudioManager.AUDIOFOCUS_LOSS) event = EVENT_LOSS;
        else if (focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) event = EVENT_LOSS_TRANSIENT;
        else if (focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK) event = EVENT_CAN_DUCK;
        else return;    // 未知平台码不发明事件 | unknown platform codes yield no invented event
        listener.onAudioFocusEvent(generation, event);
    }
}
