# Android customizations on upstream / 上游基础上的 Android 定制

Base: upstream `758f5d41509af5cd38b7eb934bc2334bdb3e3342`.

本分支沿用上游 Android 渲染和触控实现，仅增加 Turnip 加载、显示比例和简易 FPS。旧 Android 分支保留作回退，不把其渲染后端叠加回来。

This branch retains upstream Android rendering and controls, adding Turnip loading, aspect selection and a simple FPS display. The previous Android implementation remains a separate rollback.

## Settings / 设置

- `AndroidPortSettings.java` owns startup controls and the private `android_port_options_v1` preferences. Game resource imports do not include this file. Same-package, same-signature APK updates preserve preferences; uninstalling or clearing app data does not.
- `16:9` uses a centered surface and 1280×720 game output. Phone widescreen uses upstream aspect-aware output at height 720 (1600×720 on the test phone), without stretching the 16:9 image. Internal 3D scale remains unchanged.
- FPS counts successful/suboptimal queue-present submissions over elapsed time. It is labelled submitted FPS, not a measurement of evenly spaced panel scanout. Turning it off hides the view and stops its periodic callback.

设置独立于游戏导入目录保存；同包名、同签名覆盖安装会保留。卸载或清除应用数据会删除设置。宽屏调用上游原有的比例适配；增加像素后需要另测性能。FPS 是提交帧率，不能据此判断每帧上屏是否均匀。

## Updating upstream / 同步上游

Merge upstream changes into this branch and retain the customization commits. Review conflicts in `DmActivity`, JNI startup and Vulkan initialization rather than copying upstream files over this checkout. Keep the preference filename and keys stable. Verify both aspect modes, touch coordinates, FPS off/on and an APK update preserving preferences after each relevant upstream change. Game import compatibility still follows upstream's product/version checks.

后续合并上游更新时保留本分支提交；不能直接覆盖整个文件。冲突仍需审查，尤其是 Activity、JNI 和 Vulkan 初始化。保持设置文件名与键名稳定，更新后复验两种比例、触控和 FPS 开关。游戏包兼容性仍遵循上游检查。

## Test build dependencies / 测试版依赖

`-PdmTestDriverLibs` points to the existing arm64 adrenotools JNI libraries. A trusted Turnip library is provisioned separately as `files/dm/turnip/vulkan.purple.so`. This test branch does not contain or download driver binaries or game files. Startup requires Turnip and checks the Vulkan driver identity; it does not silently select the Qualcomm system driver.

`test-volk` is MIT-licensed volk pinned to `0dc3ce00bf98b9f0b6fe708ca0f7eb74e2830173`, used for dispatch through the explicitly loaded driver. Driver packaging for a general release is separate from this provisioned-phone build.
