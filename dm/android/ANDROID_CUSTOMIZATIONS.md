# Android customizations on upstream / 上游基础上的 Android 定制

Base: upstream `d93985b7d086c367efa3f01ddb086b8d4e9f334c`, Android 2.0.1 (synced 2026-10-26).

本分支沿用上游 Android 渲染和触控实现，增加 Turnip／系统驱动选择、显示比例、简易 FPS，并接通省电模式的 30 帧节拍。旧 Android 分支保留作回退。

This branch retains upstream Android rendering and controls, adding Turnip/system-driver selection, aspect selection, a simple FPS display and the Android 30 fps power-saving pacing fix. The previous Android implementation remains a separate rollback.

## Settings / 设置

- `AndroidPortSettings.java` owns startup controls and the private `android_port_options_v1` preferences. Game resource imports do not include this file. Same-package, same-signature APK updates preserve preferences; uninstalling or clearing app data does not.
- `16:9` uses a centered surface and 1280×720 game output. Phone widescreen uses upstream aspect-aware output at height 720 (1600×720 on the test phone), without stretching the 16:9 image. Internal 3D scale remains unchanged.
- FPS counts successful/suboptimal queue-present submissions over elapsed time. It is labelled submitted FPS, not a measurement of evenly spaced panel scanout. Turning it off hides the view and stops its periodic callback.
- Driver selection uses the private `use_turnip` preference, defaulting to Turnip. The native loader validates the selection and Turnip identity; a Turnip failure never silently selects the system driver. Both choices use Vulkan. The FPS label follows the selected driver.

设置独立于游戏导入目录保存；同包名、同签名覆盖安装会保留。卸载或清除应用数据会删除设置。宽屏调用上游原有的比例适配；增加像素后需要另测性能。FPS 是提交帧率，不能据此判断每帧上屏是否均匀。

## Physical controllers / 实体手柄

Integrates upstream PR #130, source commit `336e02ee8163f726f0545ac4f3de6622169bc603`, retaining its author attribution. Buttons and sticks feed the existing host controller input; clear controller input selects the original controller layout, and touching the screen returns to the touch layout. No game resource reimport is required for the current matching upstream overlay.

已集成上游 #130 的实体手柄与触屏／手柄界面切换。额外修复启动页尚未装载 JNI 时的输入保护、暂停时纯模拟扳机与十字键清零、设备断开通知，以及十字键按键与轴重复上报时的合并。保留我们已有的驱动、比例、FPS 和 30 帧接线。

Local integration guards JNI until the game starts, registers Android device-removal callbacks, releases all controller channels on pause/focus loss, and merges d-pad key/axis reports independently. State-machine regression checks and instructions are in `tests/pad-input/README.md`. Physical-controller device acceptance is separate from these checks.

## Android Auto parked games / Android Auto 停车游戏

The same `DmActivity` declares `MAIN` + `CAR_LAUNCHER` and is resizable for the projected window. Android Auto parked games require Android 15 or newer. Both drivers still use the phone GPU; game files, saves, touch controls and physical-controller routing remain on the phone. Existing surface resize callbacks update the Vulkan window; touch controls share the same game viewport. Existing pause/focus-loss callbacks clear input and notify the host. Android Auto controls parked-only availability and automatically exits the app when vehicle motion is detected.

复用现有游戏 Activity、触屏和手柄输入，增加停车游戏入口及窗口尺寸适配。需要 Android 15 或更新版本以及支持停车游戏的 Android Auto。画面由手机渲染，存档保留在手机，不需要重新导入游戏数据。先在手机完成资源导入和文件访问授权，再从车机启动。宽屏设置使用当前游戏窗口的比例。

For this locally installed test APK, enable Android Auto developer settings and its **Unknown sources** option, then reconnect Android Auto while parked. This is the documented parked-app development path; it does not bypass driving restrictions. Look for **D2R Mobile** in the car's app launcher (the test package is `org.d2rmobile.upstream.test`). Pair a controller with the phone, or use the car touchscreen. Verify launch, aspect/touch alignment, controller takeover, leaving/reopening the app, audio pause/resume and disconnect while parked. A phone launch or synthetic secondary display alone does not establish Android Auto compatibility; DHU or vehicle validation is required.

本地安装版需要在 Android Auto 自身的开发者设置中开启“未知来源”，停车后重新连接，在车机应用列表选择 D2R Mobile。手柄连接手机。验收需要检查启动、比例与触控坐标、手柄切换、离开和恢复、声音及断开连接。手机启动和模拟副屏检查不能代替 DHU／真实车机验收。

References: [parked app manifest](https://developer.android.com/training/cars/parked/auto), [parked lifecycle](https://developer.android.com/training/cars/platforms/android-auto#parked-apps), [local vehicle testing](https://developer.android.com/training/cars/testing#allow-unknown-sources).

## Updating upstream / 同步上游

The 2026-10-26 sync includes widened-stash position restoration, controller-legend cleanup and capacity checks, the upstream adoption of our 30 fps fix, BC4/BC5 filtering fallback, Adreno 740/750 policy and compute-pipeline crash recovery. Adreno 830 keeps its rewrite defaults. Startup preferences, Turnip selection, Android Auto and local controller safeguards are retained. These changes need only an APK update; no game resource reimport is required.

2026-10-26 同步包含加宽储物箱位置恢复、手柄提示清理及条目上限、上游采纳的 30 帧修复、BC4／BC5 过滤兼容、Adreno 740／750 策略及计算管线崩溃恢复。Adreno 830 的改写默认值不变，保留启动设置、Turnip、Android Auto 和本地手柄保护。只需覆盖更新 APK，无需重新导入游戏资源。

The upstream map-state restoration is included through `195ae11d`. Menu-button pulses are adapted from dodola commit [`573106f3`](https://github.com/dodola/d2r-mobile/commit/573106f3f243cbcb948e5d4029650193390600c4): shared touch state keeps finger ownership but sends a one-sample command (`pulse_hold_s < 0`). Android's production sampling bridge releases it on the next guest poll; iOS/Harmony receivers understand the same marker. Item hold/drag and gameplay skills retain their existing timing. The optional navigation circles remain hidden by default in panel mode; this fix covers that fallback/debug path, not every kind of menu interaction. No game program or overlay-generation inputs change.

已同步上游地图状态恢复，并参考 dodola 的菜单短脉冲修复适配当前触控桥。保留触点归属防止重复路由，游戏采样一次后收回导航键；原有物品长按／拖动和局内技能时序不变。面板导航圆钮默认仍隐藏，本修复针对其备用／调试路径，不代表所有菜单交互均经过该路径。无需重新改写游戏程序或生成覆盖层。

Merge upstream changes into this branch and retain the customization commits. Review conflicts in `DmActivity`, JNI startup and Vulkan initialization rather than copying upstream files over this checkout. Keep the preference filename and keys stable. Verify both aspect modes, touch coordinates, FPS off/on and an APK update preserving preferences after each relevant upstream change. Game import compatibility still follows upstream's product/version checks.

后续合并上游更新时保留本分支提交；不能直接覆盖整个文件。冲突仍需审查，尤其是 Activity、JNI 和 Vulkan 初始化。保持设置文件名与键名稳定，更新后复验两种比例、触控和 FPS 开关。游戏包兼容性仍遵循上游检查。

## Test build dependencies / 测试版依赖

`-PdmTestDriverLibs` points to the existing arm64 adrenotools JNI libraries. `-PdmTurnipAssets` points to an assets root containing the trusted `turnip/vulkan.purple.so` (SHA256 `1d80dfa019659b008e4669311db5b1e4a02af59ff1d5458a98e2f5fe18ed013b`). The APK bundles this library; first use atomically extracts it into versioned private `files/gpu/turnip-1d80dfa0/`, outside imported game resources. Update that directory version when replacing the bundled driver. No driver binaries or game files are committed or downloaded by the application. System selection loads the platform Vulkan loader without adrenotools.

`test-volk` is MIT-licensed volk pinned to `0dc3ce00bf98b9f0b6fe708ca0f7eb74e2830173`, used for dispatch through the explicitly selected driver. A source build still needs the trusted external driver build inputs above.
