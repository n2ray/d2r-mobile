// #184 已验收 AudioFocusBoundary 的实例化 Java 适配：只做 main-looper 控制和 generation 事件转发。
// 回调绝不调用 AAudio、dm_af_policy 或任何播放逻辑；它只把事件交给 C owner 的有界待处理环。
// #184 instance adapter around the accepted AudioFocusBoundary: main-looper control and generation-tagged event
// forwarding only. The callback never calls AAudio, dm_af_policy or playback logic; it only hands the event to the
// C owner's bounded pending ring.
package org.d2rmobile.android.diagnostic;

import android.content.Context;
import android.media.AudioManager;

public final class AudioFocusJniBridge implements AudioFocusBoundary.Listener {
    private final AudioFocusBoundary boundary;
    private final long nativeHandle;
    private boolean released;

    public AudioFocusJniBridge(Context context, long nativeHandle) {
        if (context == null) throw new IllegalArgumentException("context == null");
        AudioManager audioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        if (audioManager == null) throw new IllegalStateException("no AudioManager");
        this.nativeHandle = nativeHandle;
        this.boundary = new AudioFocusBoundary(audioManager, this);
    }

    /** 与 AudioFocusBoundary.request 相同的真实平台结果与 main-looper 拒绝语义。 */
    public int request(int generation, boolean acceptsDelayedFocusGain) {
        return boundary.request(generation, acceptsDelayedFocusGain);
    }

    public boolean abandon() {
        return boundary.abandon();
    }

    /** main 线程调用；先置 released 拒绝后续原生转发，再释放平台焦点。 */
    public void release() {
        if (released) return;
        released = true;
        boundary.release();
    }

    @Override
    public void onAudioFocusEvent(int generation, int event) {
        if (released) return;   // release 之后的迟到事件不再进入原生层
        nativeOnFocusEvent(nativeHandle, generation, event);
    }

    private static native void nativeOnFocusEvent(long handle, int generation, int event);
}
