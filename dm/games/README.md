# Game profiles / 逐游戏配置

`generic/android` is the first extraction stage, with D2R as its only supported title. Organization follows [nxnative's game profiles](https://github.com/seedds/nxnative/blob/c484b13cdbccd89a43733abf28d1185a43ad4917/dm/games/README.md); no nxnative runtime code is imported. This is not a generic APK that can launch arbitrary Switch games.

`generic/android` 是第一阶段拆分，目前只支持 D2R。参考 nxnative 的逐游戏配置结构，未引入其运行时代码，也不代表现在可以启动任意 Switch 游戏。

## D2R profile / D2R 配置

Directory: `0100726014352000/`, the base-game Title ID.

| File | Role / 用途 |
| --- | --- |
| `game.tsv` | Identity, modules, accepted build IDs and host defaults / 身份、模块、构建号与宿主参数 |
| `patches.py` | Existing CUTS, OBS, PARAMS and touch-mode patch selection / 原有补丁、观测门、参数位及触屏补丁选择 |
| `game_profile.h` | Generated public configuration snapshot for existing Apple builds and standalone tests; never generated from game assets / 从公开配置生成，供原有苹果构建和独立测试使用，不含游戏资源 |
| `../game.py` | Configuration parser, header generator, module validation and rewrite-input fingerprint / 配置读取、头文件生成、模块校验和改写缓存指纹 |

The extracted patch tables retain their original bytes and build guards. The rewriter still asserts original instruction bytes. Android packaging checks both main and sdk build IDs before rewriting or producing output. Changing the shared rewriter, profile loader, game.tsv or patches.py invalidates the rewrite cache.

迁出的补丁表保留原字节和构建号约束，改写时仍核验原指令。Android 出包在改写和输出之前检查 main、sdk 构建号；公共改写器、配置加载器、game.tsv 或 patches.py 有变都会使改写缓存失效。

## Commands / 命令

```sh
python3 dm/games/game.py list
python3 dm/games/game.py get --game 0100726014352000 --key modules
python3 dm/games/game.py header --game 0100726014352000 --output dm/games/0100726014352000/game_profile.h
python3 -m unittest discover -s dm/games/tests -v
python3 dm/tools/d2rm_android.py --game 0100726014352000 <Update> <output>
```

Existing D2R tool invocations keep their default. `DM_GAME` selects a registered profile; the Android Gradle build accepts `-PdmGame=0100726014352000` and passes it to CMake. Android generates its selected configuration header in the build directory. Its program ID, argv[0], save-throttle/seed addresses and renderer-scale addresses use that header. The checked-in header must stay byte-identical to the TSV-generated header; a test checks this for existing non-Android build paths.

现有 D2R 命令保持缺省行为。工具可用 `DM_GAME` 选择已登记游戏；Android Gradle 用 `-PdmGame=0100726014352000` 传给 CMake，构建目录生成宿主配置头。程序 ID、启动参数、存盘节流／种子地址及内部画质地址已接入；仓内配置头服务原有非 Android 构建，测试保证它与 TSV 一致。

## Remaining boundaries / 尚未拆完的边界

- D2R touch/UI, save initialization, cloud sync and game feature modules still belong to the D2R runtime. They have not been made generic or physically relocated.
- Private D2R trampoline emission remains guarded inside the shared rewriter. This stage extracts its patch tables and touch cuts, not the entire extension API.
- Shader facts/tables and the graphics pipeline are still the existing D2R implementation. Other games' shader translation and NVN coverage are not verified.
- The Android shell/importer remains D2R-specific. CMake rejects another Title ID; unknown profiles and mismatched input modules are rejected rather than run with D2R defaults.
- Only one registered build row is supported in this stage. Additional versions need explicit compatibility work.

触屏界面、存档初始化、云同步及游戏功能模块仍属 D2R，尚未全部迁移；私有桩的生成代码仍受规则域保护、保留在改写器中；着色器表和图形管线仍是 D2R 的实现。Android 壳与导入器仍专用，构建拒绝其他 Title ID，输入不匹配时停止。目前只支持一行已登记构建。后续应逐款补齐这些边界，不能只复制一个 game.tsv 就宣称新增游戏兼容。

Phone unavailable during this extraction: build and offline equivalence checks can run on the computer, but device startup, rendering, touch and saves require a later regression. No phone settings, saves or installed APK are changed by these tools unless separately installed.

本次手机不可用，只做电脑构建与离线等价检查；启动、显示、触控、存档留待实机回归。未部署到手机，不改手机设置、存档或实装 APK。
