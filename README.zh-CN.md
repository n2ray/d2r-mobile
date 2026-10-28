# D2R Mobile — Android 定制版

English: [README.md](README.md)

基于 [jhaizhou-ops/d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) 的 Android 定制版。游戏运行时、NVN → Vulkan 渲染与触屏界面跟随上游，只维护少量 Android 定制。沿用 MIT 许可证并保留上游及贡献者署名。**需要自备匹配的 Switch 游戏文件**，本仓库不分发游戏内容，见[游戏文件要求](docs/GAME_FILES.zh-CN.md)。

## 开发基线

本开发分支同步至上游 `1e0e7934`（Android 2.1.0），待实机验证。`main` 仍是以上游为基础的 Android 定制版，取代此前独立维护的 Android 实现。

旧源码保存在 [`archive/android-legacy-20261023`](https://github.com/n2ray/d2r-mobile/tree/archive/android-legacy-20261023)，归档点 `0fa019b02336af144a761668fd4728d8cde6b693`，供查阅和回退，不再继续开发。见[旧 issue／PR 归档](docs/android/LEGACY_ARCHIVE_20261023.md)。关闭代表路线变更，不代表验收全部完成。旧历史可追溯，当前源码树不包含旧版独立实现。

## 我们的定制

| 项目 | 行为 |
| --- | --- |
| 启动设置 | 独立保存驱动、比例及简易 FPS 选项 |
| 存档备份 | 启动页通过系统文件选择器导出／导入 ZIP；导入先校验并备份旧存档，待实机验证 |
| 镜头 | 接入上游可选的越肩和第一人称模式，待实机验证 |
| 显示驱动 | 默认内置 Turnip，可选择系统 Vulkan |
| 画面比例 | 居中16:9、1280×720，或按窗口比例铺满、高度720，不拉伸 |
| 帧率显示 | 提交 FPS，不等于逐帧均匀上屏测量 |
| 省电模式 | 接通上游30帧选项，[上游 PR #128](https://github.com/jhaizhou-ops/d2r-mobile/pull/128) |
| 实体手柄 | 集成[上游 #130](https://github.com/jhaizhou-ops/d2r-mobile/pull/130)，补充启动及生命周期输入修复 |
| Android Auto | 停车游戏入口，[草稿 #131](https://github.com/jhaizhou-ops/d2r-mobile/pull/131)，车机待验证 |

详见 [Android 定制说明](dm/android/ANDROID_CUSTOMIZATIONS.md)。旧版的任意驱动 ZIP 导入／删除及扩展诊断等功能，不能视为新版已具备。

最新输入修复会在关闭面板、切换区域后恢复玩家选择的全屏地图，并让虚拟菜单按钮在游戏取到一次输入后释放，避免重复触发。游戏内技能长按及物品拖动／长按维持原语义。APK 构建和本地回归不代替这些修复的手机／车机实测。

## 实机证据

测试设备：**小米15 Ultra／骁龙8 Elite／Adreno 830／Android 16**。上游基础版配 Turnip 已进入角色选择和营地，所观察场景显示正常。Game Turbo 开启时一次94秒营地站立测试，提交 FPS 中位数60.00，每秒读数56.69–61.09，仍存在部分长帧；不代表所有场景持续稳定60帧，也不是功耗结论。

实体手柄及 Android Auto 仍需对应硬件验收。Android Auto 需要 Android 15 或更新版本及停车游戏支持；本地安装版在 Android Auto 自身的开发者设置开启“未知来源”。

## 构建与升级

先阅读[上游 Android 构建与导入说明](dm/android/README.zh-CN.md)，以及 [ANDROID_CUSTOMIZATIONS.md](dm/android/ANDROID_CUSTOMIZATIONS.md) 中额外的可信驱动构建输入。驱动二进制及生成的游戏内容均留在仓库外。本仓库提供源码，不提供完整游戏下载。

当前已安装定制版包名为 `org.d2rmobile.upstream.test`，构建参数为 `-PdmAppId=org.d2rmobile.upstream.test`。保持包名和签名密钥一致才能覆盖升级并保留私有设置、存档。旧应用与新应用存档独立，不会自动同步；迁移或卸载前先备份。普通 APK 更新无需重导数据，只有上游游戏／覆盖层契约变化时才按要求重新生成导入。

## 后续维护

原项目通过 `upstream` 远端跟踪。适用更新经审查后，在本仓库 `main` 的短期分支合入，保留定制提交和稳定设置键名，通过构建和相关实测再部署。不要直接覆盖整个 fork，也不再把旧渲染实现并回主线。通用改进优先向上游提 PR，个人设置留在 fork。
