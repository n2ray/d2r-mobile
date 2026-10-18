// #170 真实 Android AudioManager 焦点边界：只请求/放弃平台焦点，并把平台回调按调用方 generation 转发。
// 不创建或操作 AAudio、不修改原生策略；构造时不请求焦点。后续 JNI 在串行控制线程调用 request/abandon/release，
// 把 listener 事件入队回同一控制线程，再交给 dm_af_policy。
// #170 real Android AudioManager focus boundary: requests/abandons platform focus only and forwards platform
// callbacks tagged with the caller's generation. It never creates or touches AAudio and never mutates the native
// policy, and it never requests focus on construction. A later JNI layer calls request/abandon/release on the
// serialized control thread and enqueues listener events back onto that same thread before dispatching to policy.
//
// Thread contract (enforced, not merely documented):
//   - request/abandon/release must be serialized by the owner. A request that overlaps another request is refused
//     with AUDIOFOCUS_REQUEST_FAILED instead of racing; release wins over an in-flight request.
//   - The platform listener is delivered by Android on the main looper. Ownership is published under a short lock
//     before any callback can be forwarded. A callback that arrives while requestAudioFocus is still executing is
//     staged and flushed only if the platform then reports GRANTED or DELAYED; it is discarded on FAILED.
//   - No lock is held across requestAudioFocus/abandonAudioFocusRequest and the listener runs outside the lock, so
//     a platform callback can never deadlock the control thread.
//   - The listener must enqueue to the serialized control thread before touching AAudio or native policy.
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

    private static final int MAX_STAGED = 8;

    /** 平台调用结果原样返回 AudioManager.AUDIOFOCUS_REQUEST_GRANTED/DELAYED/FAILED。 */
    public interface Listener {
        /** 平台回调线程（主 looper）；不得在此调用 AAudio 或原生策略。| Main-looper callback; never call AAudio here. */
        void onAudioFocusEvent(int generation, int event);
    }

    private final AudioManager audioManager;
    private final Listener listener;
    private final Object lock = new Object();

    // 以下状态由 lock 保护。| Guarded by lock.
    private AudioFocusRequest activeRequest;
    private int activeGeneration;
    private long nextToken = 1;
    private long activeToken;
    private boolean released;
    private boolean requesting;
    private long requestingToken;
    private int requestingGeneration;
    private final int[] staged = new int[MAX_STAGED];
    private int stagedCount;
    private int droppedCallbacks;

    public AudioFocusBoundary(AudioManager audioManager, Listener listener) {
        if (audioManager == null) throw new IllegalArgumentException("audioManager == null");
        if (listener == null) throw new IllegalArgumentException("listener == null");
        this.audioManager = audioManager;
        this.listener = listener;
    }

    /** 不启用 delayed focus 的请求。| Request without delayed-focus opt-in. */
    public int request(int generation) {
        return request(generation, false);
    }

    /**
     * 请求 AUDIOFOCUS_GAIN（游戏用途）。只有 acceptsDelayedFocusGain 为 true 才会向平台设置
     * setAcceptsDelayedFocusGain(true)；否则平台不会返回 DELAYED。返回平台真实结果，只有 GRANTED/DELAYED
     * 建立所有权。已释放、或与前一次 request 重叠时返回 FAILED 且不调用平台。
     * Requests AUDIOFOCUS_GAIN for game usage. Delayed focus is opted into on the real request only when
     * acceptsDelayedFocusGain is true. Returns the real platform result; only GRANTED/DELAYED establish ownership.
     * Returns FAILED without a platform call when released or when another request is in flight.
     */
    public int request(int generation, boolean acceptsDelayedFocusGain) {
        synchronized (lock) {
            if (released || requesting) return AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        }
        abandon();   // 先放弃旧请求；旧 token 立即失效 | abandon the old request first

        final long token;
        synchronized (lock) {
            if (released || requesting) return AudioManager.AUDIOFOCUS_REQUEST_FAILED;
            token = nextToken++;
            requesting = true;
            requestingToken = token;
            requestingGeneration = generation;
            stagedCount = 0;
        }

        final AudioManager.OnAudioFocusChangeListener platformListener =
                new AudioManager.OnAudioFocusChangeListener() {
                    @Override
                    public void onAudioFocusChange(int focusChange) {
                        dispatch(token, focusChange);
                    }
                };
        final AudioAttributes attributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build();
        final AudioFocusRequest request = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(attributes)
                .setWillPauseWhenDucked(false)
                .setAcceptsDelayedFocusGain(acceptsDelayedFocusGain)
                .setOnAudioFocusChangeListener(platformListener)
                .build();

        final int result;
        try {
            result = audioManager.requestAudioFocus(request);
        } catch (RuntimeException failure) {
            synchronized (lock) {
                requesting = false;
                stagedCount = 0;
            }
            throw failure;
        }

        final boolean owned = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
                || result == AudioManager.AUDIOFOCUS_REQUEST_DELAYED;
        final int[] toFlush;
        int flushCount = 0;
        boolean abandonedByRelease = false;
        synchronized (lock) {
            requesting = false;
            if (released) {
                // release 与 in-flight 请求竞争：平台若已授予，必须立刻放弃，绝不留下无主焦点。
                // Release raced the in-flight request: abandon any granted focus, never leak it.
                droppedCallbacks += stagedCount;
                stagedCount = 0;
                activeRequest = null;
                activeToken = 0;
                activeGeneration = 0;
                abandonedByRelease = owned;
                toFlush = null;
            } else if (owned) {
                activeRequest = request;
                activeToken = token;
                activeGeneration = generation;
                flushCount = stagedCount;
                toFlush = new int[flushCount];
                System.arraycopy(staged, 0, toFlush, 0, flushCount);
                stagedCount = 0;
            } else {
                // 平台明确失败：早到的回调没有被授予的焦点，全部丢弃，绝不伪造成功。
                // The platform failed: stashed callbacks have no granted focus and are discarded, never replayed.
                droppedCallbacks += stagedCount;
                stagedCount = 0;
                activeRequest = null;
                activeToken = 0;
                activeGeneration = 0;
                toFlush = null;
            }
        }
        if (abandonedByRelease) {
            audioManager.abandonAudioFocusRequest(request);
            return AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        }
        if (toFlush != null) {
            for (int i = 0; i < flushCount; i++) listener.onAudioFocusEvent(generation, toFlush[i]);
        }
        return result;
    }

    /**
     * 放弃当前请求（幂等）。只放弃自己实际持有的那个 AudioFocusRequest 实例；无活跃请求时不调用平台。
     * 返回是否真的放弃了平台请求。| Abandons exactly the retained request; no platform call when idle.
     */
    public boolean abandon() {
        final AudioFocusRequest request;
        synchronized (lock) {
            request = activeRequest;
            if (request == null) return false;
            activeToken = 0;            // 先失效 token：随后到达的回调不能再影响新状态
            activeRequest = null;
            activeGeneration = 0;
        }
        audioManager.abandonAudioFocusRequest(request);
        return true;
    }

    /** 释放边界（幂等）：放弃活跃请求并永久拒绝后续 request；迟到回调被丢弃。 */
    public void release() {
        synchronized (lock) {
            if (released) return;
            released = true;
        }
        abandon();
    }

    public boolean isActive() {
        synchronized (lock) { return activeRequest != null; }
    }

    public int generation() {
        synchronized (lock) { return activeGeneration; }
    }

    /** 因释放/替换/失败请求而被丢弃的平台回调数。| Callbacks dropped for released/replaced/failed requests. */
    public int droppedCallbackCount() {
        synchronized (lock) { return droppedCallbacks; }
    }

    /** 兼容旧调用方的别名。| Alias kept for older callers. */
    public int lateCallbackCount() {
        return droppedCallbackCount();
    }

    private void dispatch(long token, int focusChange) {
        final int event = mapFocusChange(focusChange);
        if (event < 0) return;          // 未知平台码不发明事件 | unknown platform codes yield no invented event
        final int forwardGeneration;
        synchronized (lock) {
            if (requesting && token == requestingToken) {
                // 平台还在 requestAudioFocus 内：先暂存，由真实结果决定转发或丢弃。
                // requestAudioFocus is still running: stage until the real result decides.
                if (stagedCount < MAX_STAGED) staged[stagedCount++] = event;
                else droppedCallbacks++;
                return;
            }
            if (released || token != activeToken || activeRequest == null) {
                droppedCallbacks++;
                return;
            }
            forwardGeneration = activeGeneration;
        }
        listener.onAudioFocusEvent(forwardGeneration, event);
    }

    private static int mapFocusChange(int focusChange) {
        if (focusChange == AudioManager.AUDIOFOCUS_GAIN) return EVENT_GAIN;
        if (focusChange == AudioManager.AUDIOFOCUS_LOSS) return EVENT_LOSS;
        if (focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) return EVENT_LOSS_TRANSIENT;
        if (focusChange == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK) return EVENT_CAN_DUCK;
        return -1;
    }
}
