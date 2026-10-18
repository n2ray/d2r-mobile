# D2R Mobile — Android fork

English: [README.md](README.md)

**Android 尚不可玩。** 本 fork 的目标是在**小米 15 Ultra / 骁龙 8 Elite、Android ARM64** 上顺畅离线游玩《暗黑破坏神 II：狱火重生》，完整移植 [jhaizhou-ops/d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile)（本项目目标中称为“r2d”）的游戏专用触控体系。**正常画质下持续稳定 60fps 是待测目标**，不是已达成结果或交付承诺。

上游项目由 **jhaizhou** 创作，采用 [MIT 许可证](LICENSE)。上游的 Apple 可玩性、60fps、功耗与载入时间成果属于上游 Apple 构建，不能作为 Android 兼容性或性能结论。本 fork 所用源码基线的 [上游 Apple 项目介绍与使用说明](https://github.com/jhaizhou-ops/d2r-mobile/blob/21cf97b91823111ead237858ea79929afacf71a5/README.zh-CN.md)仍可查阅；[上游当前 README](https://github.com/jhaizhou-ops/d2r-mobile/blob/main/README.zh-CN.md)可能已有后续变化。

## 当前证据与缺口

本摘要对应本次文档更新核对的已合并基线（2026-10-18）。后续进展以[总清单 #1](https://github.com/n2ray/d2r-mobile/issues/1)和关联 Issue 为准。局部实现合并不代表父任务完成；开放 PR 仍待审查与验收。

| 方向 | 已有内容 | 尚未验证或完成的部分 |
|---|---|---|
| 运行时与服务 | 离线模块检查／改写、自有运行时样例、有界 IPC 及资源／工具链基础 | 生产同步／抢占、TLR／SVC36 契约与严格准入仍待 [#3](https://github.com/n2ray/d2r-mobile/issues/3)、[#4](https://github.com/n2ray/d2r-mobile/issues/4)、[#5](https://github.com/n2ray/d2r-mobile/issues/5)闭合；完整受控 SDK 初始化及正常退出须通过 [#10](https://github.com/n2ray/d2r-mobile/issues/10) |
| 受限 SDK 诊断 | 单独受限的 macOS 诊断执行了 `_init_libc0` 和一个 `nnosInitialize` 阶段，返回并清理 | 生产准备仍拒绝原始 SDK 输入。私有诊断 quarantine 不等于生产准入、完整 SDK 启动或 Android 游戏执行 |
| 图形 | 自有 Vulkan 计算／绘制／读回检查、RGBA8 UNORM/SRGB 资源切片、shader 合同检查（[PR #47](https://github.com/n2ray/d2r-mobile/pull/47)） | 完整 NVN → Vulkan 资源、shader 反射／绑定、绘制和连续呈现仍在 [#12](https://github.com/n2ray/d2r-mobile/issues/12)、[#13](https://github.com/n2ray/d2r-mobile/issues/13)、[#18](https://github.com/n2ray/d2r-mobile/issues/18)。shader 构建合同闸门须显式启用（`DMSH_CONTRACT=1`），不证明完整 Android shader 链已打通 |
| 触控 | 已合并 [PR #46](https://github.com/n2ray/d2r-mobile/pull/46)：复用上游可移植 C 手势核心，实现 Android pointer-ID／时序适配器及自有测试 | NativeActivity 真实事件、游戏状态／命中／HID 桥、HUD、IME 及整机触控验收仍未闭合，见下表 |
| 设备诊断 | 较早合成 APK 在小米手机通过 ARM64 门、Vulkan 像素及窗口提交检查，见[诊断说明](dm/android/app/README.zh-CN.md) | 后续运行时改动仍需新设备证据。自有样例不验证游戏场景、完整生命周期、音频、存档或可玩交付 |

须保持严格扫描、quarantine 边界、W^X、所有权及失败回滚，不以成功占位替代未完成的契约或服务。未知缺口仍可能产生新任务；目前没有有证据支撑的完成百分比或完成日期。

## 完整触控目标

目标是上游针对 D2R 的整套交互，包括游戏状态与布局行为；通用虚拟手柄不能满足此目标。[#31](https://github.com/n2ray/d2r-mobile/issues/31)交付的[触控等价矩阵](docs/android/touch-parity.md)冻结了源码行为及依赖，是清单，不是 Android 功能已交付证明。矩阵缺口列描述冻结时的检查点，PR #46 是其后新增的适配器切片。

| 工作 | 跟踪 |
|---|---|
| 触点所有权、多指时序、按键脉冲、取消与失焦 | [#32](https://github.com/n2ray/d2r-mobile/issues/32) |
| 游戏／场景身份、面板状态、UI 命中及实际客体 HID 接入 | [#33](https://github.com/n2ray/d2r-mobile/issues/33)、父任务 [#19](https://github.com/n2ray/d2r-mobile/issues/19) |
| Vulkan HUD、布局、密度、安全区及用户资源生成的美术 | [#34](https://github.com/n2ray/d2r-mobile/issues/34) |
| 移动、拖动瞄准、双技能组／ZL 时序、玩家与佣兵腰带 | [#35](https://github.com/n2ray/d2r-mobile/issues/35) |
| 物品点击／双击／长按／拖放、商店／仓库／方块操作 | [#36](https://github.com/n2ray/d2r-mobile/issues/36) |
| 世界交互、菜单、导航与自定义页 | [#37](https://github.com/n2ray/d2r-mobile/issues/37) |
| 系统键盘、中英文 IME 与组合输入 | [#38](https://github.com/n2ray/d2r-mobile/issues/38) |
| 设置、持久布局及触控／手柄切换 | [#39](https://github.com/n2ray/d2r-mobile/issues/39) |
| 小米 15 Ultra 整套触控等价与触点到画面延迟验收 | [#40](https://github.com/n2ray/d2r-mobile/issues/40) |

## 不需要游戏文件的开发入口

在 ARM64 开发机的仓库根目录运行。依赖、断网前的依赖准备、输出覆盖与历史证据见 [Android 开发说明](dm/android/README.zh-CN.md)。显式把 `DM_ROOT` 设为外置磁盘工作区（维护者使用 T2），尤其是在 worktree 内；默认值按 checkout 位置计算，可能落到其他目录。

```sh
# 将这个示例替换为自己的外置磁盘工作区。
export DM_ROOT=/path/to/external-disk/d2r-work

# 自有宿主样例，不需要游戏文件；此 CPU 入口关闭 GPU 依赖。
./dm/android/test.sh -DDM_ANDROID_VULKAN=OFF

# 交叉编译 Android ARM64 工具；需要已安装 NDK，不执行手机程序。
./dm/android/build.sh -DDM_ANDROID_VULKAN=OFF
```

宿主入口需要 CMake 3.22+、C 编译器与 Capstone 5；原生执行用例要求 ARM64。交叉编译需安装 NDK r28+（默认查找 r29）。首次配置会下载固定版本的 LZ4／Mbed TLS，除非已在本地准备；“离线”指无需游戏的本地开发，不代表自动完成断网环境初始化。

构建[合成诊断 APK](dm/android/app/README.zh-CN.md)是单独一步：

```sh
# 另需 GPU 工具、Android SDK platform/build-tools 35、JDK/keytool 和 zip。
bash dm/android/app/build.sh
```

此命令构建带调试签名、仅含自有样例的工程诊断 APK，不自动安装或启动，也不构建可玩的游戏。`setup.sh` 准备的是 Apple 产物，不是 Android 入口。

## 游戏文件、验收与参与

请使用自己合法拥有的 Switch **1.0.34.0、含「术士君临」资料片**的文件（仓库版本目录 **3.3.94012**）。[游戏文件准备](docs/GAME_FILES.zh-CN.md)与 [Android 存储／检查说明](dm/android/README.zh-CN.md#存储与游戏文件)列出了输入要求。已有导出的 ExeFS／RomFS 不需要密钥。本仓库不提供游戏内容。

可玩交付仍需安全启动／退出、完整渲染、实际 HID／触控、音频、持久存档、资源导入和生命周期集成，再完成真实游戏／设备验收。[#23](https://github.com/n2ray/d2r-mobile/issues/23)规定持续性能测量：可复现的城镇／战斗／走图场景、冷机及至少 30 分钟热机、帧时间分布、温度／功耗条件与触点到画面延迟。数值验收阈值须在首轮实机基线后制定。[#22](https://github.com/n2ray/d2r-mobile/issues/22)跟踪完整游戏／生命周期验收；[#24](https://github.com/n2ray/d2r-mobile/issues/24)跟踪可安装交付及已知限制。能安装的诊断包不等于该交付。

参与者从[总清单 #1](https://github.com/n2ray/d2r-mobile/issues/1)领取边界明确的 Issue，在 topic branch 开发，提交可复现证据供独立审查。设计、代码、自有样例、SDK、设备／游戏验收须分别记录。大文件、构建与缓存放在外置工作区；不公开游戏、密钥、存档、凭据、私人路径／IP 或原始本地日志，只分享审查过且脱敏的诊断。上游通用贡献说明见 [CONTRIBUTING.zh-CN.md](CONTRIBUTING.zh-CN.md)，其中 Apple 命令不代表 Android 验收入口。

## 来源、致谢与许可证

本 fork 保留上游 Apple 代码与文档以供参考和复用：[架构](dm/README.zh-CN.md)、[图形](dm/gfx/README.zh-CN.md)、[iOS 外壳](dm/ios/README.zh-CN.md)、[验证](dm/verify/README.zh-CN.md)。这些说明分别描述对应平台，不证明 Android 支持。

感谢 **jhaizhou** 与[上游贡献者](https://github.com/jhaizhou-ops/d2r-mobile/graphs/contributors)。[上游完整致谢](https://github.com/jhaizhou-ops/d2r-mobile/blob/21cf97b91823111ead237858ea79929afacf71a5/README.zh-CN.md#致谢)与[依赖／许可证清单](docs/DEPENDENCIES.zh-CN.md)保留了 Ryujinx、deko3d、nvn.h、libdeflate、bcdec、FLIP 及其他工具和社区工作的署名；另见[派生接口信息](docs/DERIVED.zh-CN.md)。仓库继续采用 [MIT 许可证，copyright (c) 2026 jhaizhou](LICENSE)，第三方代码许可证保留在各自目录中。

Diablo 与 Blizzard Entertainment 是暴雪娱乐的商标，Nintendo Switch 是任天堂的商标。本项目独立、非商业，与这些公司没有关联，也未获其认可或赞助。
