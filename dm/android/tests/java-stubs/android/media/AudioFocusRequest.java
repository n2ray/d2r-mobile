// #170 主机测试用窄 stub；签名与真实 android.media.AudioFocusRequest 的 Builder 子集一致。
// #170 narrow host-test stub matching the real AudioFocusRequest.Builder subset.
package android.media;

public class AudioFocusRequest {
    private final int focusGain;
    private final AudioAttributes attributes;
    private final boolean pauseWhenDucked;
    private final AudioManager.OnAudioFocusChangeListener listener;

    private AudioFocusRequest(Builder b) {
        this.focusGain = b.focusGain;
        this.attributes = b.attributes;
        this.pauseWhenDucked = b.pauseWhenDucked;
        this.listener = b.listener;
    }

    public int getFocusGain() { return focusGain; }
    public AudioAttributes getAudioAttributes() { return attributes; }
    public boolean willPauseWhenDucked() { return pauseWhenDucked; }
    public AudioManager.OnAudioFocusChangeListener getOnAudioFocusChangeListener() { return listener; }

    public static final class Builder {
        private final int focusGain;
        private AudioAttributes attributes;
        private boolean pauseWhenDucked;
        private AudioManager.OnAudioFocusChangeListener listener;

        public Builder(int focusGain) { this.focusGain = focusGain; }

        public Builder setAudioAttributes(AudioAttributes attributes) {
            this.attributes = attributes;
            return this;
        }

        public Builder setWillPauseWhenDucked(boolean pauseWhenDucked) {
            this.pauseWhenDucked = pauseWhenDucked;
            return this;
        }

        public Builder setOnAudioFocusChangeListener(AudioManager.OnAudioFocusChangeListener listener) {
            this.listener = listener;
            return this;
        }

        public AudioFocusRequest build() {
            if (listener == null) throw new IllegalStateException("listener == null");
            return new AudioFocusRequest(this);
        }
    }
}
