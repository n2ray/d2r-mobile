// #184 主机测试用最小 Context stub（仅 getSystemService/getClassLoader）。
// #184 minimal host-test Context stub (getSystemService/getClassLoader only).
package android.content;

import android.media.AudioManager;

public class Context {
    public static final String AUDIO_SERVICE = "audio";
    private final AudioManager audioManager;
    private final ClassLoader classLoader;

    public Context() {
        this(new AudioManager(), Context.class.getClassLoader());
    }

    public Context(AudioManager audioManager, ClassLoader classLoader) {
        this.audioManager = audioManager;
        this.classLoader = classLoader;
    }

    public Object getSystemService(String name) {
        return AUDIO_SERVICE.equals(name) ? audioManager : null;
    }

    public ClassLoader getClassLoader() {
        return classLoader;
    }

    public AudioManager audio() {
        return audioManager;
    }
}
