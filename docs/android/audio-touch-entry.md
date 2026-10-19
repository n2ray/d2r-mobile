# #211 触屏诊断音频入口

`DmAudioTouchEntry` 在现有 NativeActivity 的 `android.R.id.content` 中安装两个自有按钮：
“诊断音频 播放”和“诊断音频 停止”。按钮向下偏移，避开 IME 测试按钮的默认位置；不替换 Surface、
HUD、IME 或 peer 视图，不改变全局 Window 设置，也不请求文字/游戏焦点。

## 控制契约

- 构造、安装、resume 都不请求音频焦点、不开流、不播放。
- 只有用户点击才通过 opaque owner token 调用注册的 JNI 方法；native 端校验当前 owner/token，
  并复用键盘 P/S 的 active/resumed/focused/window 门控与同一个 `native_audio_play`/`native_audio_stop`。
- 停止撤销同一 owner 的播放意图；重复停止幂等。
- pause、输入队列退休、窗口丢失、destroy 先停止音频，再失效点击并移除按钮；remove 失败时点击已失效，
  保留 class/token 作为有界 cleanup handle 供重试，不重复 PR204 的提前返回或丢最后句柄。
- destroy 在音频/文字活动停止后处理视图清理；若视图移除未被证明，App 状态保留，不虚报清理完成。
- 键盘 P/S 路径保持有效。

## 限制

- host Java 测试替换的仅是 Android View/Button 边界和 control sink；真实 native_audio/session/JNI/output
  栈由 NativeAudioHarness 验证。
- 无手机/ADB/真实窗口/音频硬件/游戏/SDK runtime；不声明实际可听播放、延迟或 guest AV 同步。
