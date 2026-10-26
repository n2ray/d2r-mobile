"""D2R 构建号限定的改写补丁与观测门。
D2R build-specific rewrite patches and observation gates.
"""
import struct

CUTS = {'db0507f36b84200ac05f75d76ed5026f': [
    # （放进所有产物共用的基础改写）：游戏显存分配器预算 260 MB → 512 MB
    # (Placed in the base rewrite shared by all build outputs): game video memory allocator budget 260 MB → 512 MB
    (0x8524c4, '0808a2d2', '0800a4d2', '显存分配器初始化配置：mov x8,#0x10400000（260 MB）→ mov x8,#0x20000000（512 MB），随后 movk #0xa0,lsl48 与 str [sp,#0x238] 不变；'
     '原预算下内部分辨率 > 约 345 万像素时（2736x1260 过、2864x1240 崩）渲染目标放不下，main+0x174b018 返回空、main+0x17417a4 读空崩溃；'
     '只是显存预算，不进游戏逻辑：cvd5 逐拍对拍在 1600x900 与 2868x1320 两档均一致'),
    (0x1979bc, 'e8a79f1a', 'e8d79f1a', '离线放行窗口：读存档设置键（0x1d53e80 处字串）的上次在线时刻，'
     '「当前时刻 − 上次在线 < 0x0009356907420000 ns（= 30 天）」置位 [x20+0x2c]；lt→gt'),
    (0x5b14dc, 'e8a79f1a', 'e8d79f1a', '调 0xddab50（缓冲区 [x10+0x138]+x25、剩余长度、回调 0x527d50、表 0x209cdb8、9 项）返回值 <1 置位，'
     '为真时走 0x5b14f8 分支；lt→gt（业务含义未从字串定出）'),
    (0x5f3184, 'e8a79f1a', 'e8d79f1a', '数据表 [0x2d64a18+idx*16] → +0xf90 记录（0x1fc 字节）+0x4c 下标 → +0xfd0 记录（0x128 字节）+0xf1 有符号字节 <0 置位，'
     '与（w26>0x24）相与后作第 4 参传下游；lt→gt（业务含义未从字串定出）'),
    (0x1d53e80, '55736572204c617374204f6e6c696e6500', '566f6963654c616e677561676500000000',
     '只读字串 "User Last Online" → "VoiceLanguage"：第 1 处改读一个必定存在的设置键，「上次在线」取到小数值，差值恒超 30 天，配合 gt 恒放行'),
    # 产品剪口：开场 Logo 与「按 A 开始」页剪掉，启动直达主菜单（装载画面保留）
    # Product cut: the opening Logo and the 「按 A 开始」(Press A to Start) page are cut, and startup goes straight to the main menu (the loading screen is kept)
    (0x8019ec, '280f0034', 'e8feff34', '前端引导首次进入分支：cbz w8,#0x801bd0（LogoMenuPanel → Logo 视频 → 标题页）改为 cbz w8,#0x8019c8'
     '（落到直达路径的 0x23e5e0(1) → 0xb156a0 进主菜单；跳过 0x8019bc 起置联机模式 [0x2d7badc]=1 的两条，联机模式保持启动初值 0 = 离线，'
     '不触发 0x80ec44 的联网检查、断网弹窗与回标题页）；r4 版目标 0x8019bc 会置联机模式 1，实测仍回标题页并弹断网框'),
    # 宿主原生实现剪口（仅宿主：真值模拟器不应用，Ryujinx 不认识 0x80 起的宿主调用号；等价由逐块解码 5511/5511 一致证明）
    # Cuts for host-native implementations (host only: the ground-truth emulator does not apply them, Ryujinx does not know host call numbers from 0x80 up; equivalence is proven by per-block decoding matching 5511/5511)
    (0x1b0ee60, 'fc6fbaa9fa6701a9', '011000d4c0035fd6', 'TACT BLTE 块 zlib 解码入口（DecodeZlib）→ svc #0x80 ; ret：宿主用 libdeflate 实现'
     '（hle_tact.c，调用号登记在 hle.h SVC_HOST_TACT_ZLIB）；改写器先打剪口再扫描，svc #0x80 自动成为宿主门，门块保存恢复 x30，ret 回原调用者', '仅宿主'),
    # 宿主测试开关的落点（仅宿主）：缺省宿主原样转调，行为不变
    # Hook points for host test switches (host only): by default the host forwards as is, behavior unchanged
    (0x5d3a88, 'a27a0194', '211000d4', '收包泵 0x5d3960 投递客户端命令包的调用点 bl 0x632510 → svc #0x81：宿主缺省以原参数原样调用 0x632510（返回值调用方不用）；'
     'DM_C2S_REPLAY 时丢弃客户端自发命令、改由拍入口按文件注入（diag.c，调用号登记在 hle.h SVC_HOST_C2S_DELIVER）', '仅宿主'),
    (0x5d39c8, 'ba750194', '411000d4', '收包泵 0x5d3960 投递系统类消息的调用点 bl 0x6310b0 → svc #0x82：宿主缺省以原参数原样调用 0x6310b0（返回值调用方不用）；'
     'DM_C2S_REPLAY 时，回放文件首拍起丢弃宿主客户端自发的系统类消息（之前的加入消息放行），改由拍入口按文件注入（diag.c，调用号登记在 hle.h SVC_HOST_C2S_SYS）', '仅宿主'),
    # 屏幕比例（仅宿主，配合下方 PARAMS 分辨率参数位）：输出分辨率上限 {u32 宽, u32 高}，只被分辨率选择的三处读写
    # （0xe12e28 / 0xe10d70 读：选中值的宽或高超上限即不换；0xe12f54 初始化选完后写成选中值）；初值 1920x1080 → 4096x2160，让按屏幕比例放宽的宽度能通过
    # Screen aspect (host only, together with the resolution parameter slots in PARAMS below): output resolution cap {u32 width, u32 height}, read or written at only three places in resolution selection
    # (0xe12e28 / 0xe10d70 read: if the selected value's width or height exceeds the cap, no switch is made; 0xe12f54 writes it to the selected value after initialization finishes selecting); initial value 1920x1080 → 4096x2160, so that a width widened to the screen aspect can pass
    (0x211da30, '8007000038040000', '0010000070080000', '输出分辨率上限初值 1920x1080 → 4096x2160（数据段）：宿主按屏幕比例给的宽度（如 iPhone 19.5:9 的 1956x900）'
     '须不超上限才会被选中；Mac 前端仍给原值 1600x900 / 1280x720，行为不变', '仅宿主'),
    # 运行模式通知处理（0xe10d30）给「窗口尺寸变化」事件的宽度：原码 mov w9,#0x740; and x9,x25,x9——编译器知道宽只可能是 1280（0x500）/ 1600（0x640），
    # 把「取低 32 位」优化成了 & (0x500|0x640)。KRES0 / KRES1 参数位换掉了那两个常数，这个推导出来的掩码没跟着改：2868 & 0x740 = 768、2296 & 0x740 = 64
    # （运行中切主机 / 掌机后输出宽 64 / 768，iOS 过热降级走的就是这条）。改成直接取低 32 位：对原版两个宽度结果相同，对任意宽度正确
    # The width that the operation-mode notification handler (0xe10d30) gives to the "window size changed" event: original code mov w9,#0x740; and x9,x25,x9 -- the compiler knew the width could only be 1280 (0x500) / 1600 (0x640),
    # and optimized "take the low 32 bits" into & (0x500|0x640). The KRES0 / KRES1 parameter slots replaced those two constants, but this derived mask was not changed along with them: 2868 & 0x740 = 768, 2296 & 0x740 = 64
    # (after switching docked / handheld while running, the output width was 64 / 768; the iOS overheating downgrade takes exactly this path). Changed to take the low 32 bits directly: same result for the original game's two widths, correct for any width
    (0xe10e48, '09e880522903098a', 'e903192a1f2003d5', '运行模式通知发窗口尺寸事件的宽度：mov w9,#0x740; and x9,x25,x9 → mov w9,w25; nop（取低 32 位，不再用原版两档宽度推出的常数掩码）', '仅宿主'),
    (0xe12e88, '09e880528902098a', 'e903142a1f2003d5', '启动初始化选分辨率（0xe12d70）同一处掩码：mov w9,#0x740; and x9,x20,x9 → mov w9,w20; nop（structck 参数门下游掩码检查查出；启动时事件接收对象通常未建，平时不显形）', '仅宿主'),
    # 产品剪口（复刻排除清单「经典画面切换」）：经典画面切换 0xb15d60 的两条状态写入 → nop。
    # 画面过渡状态在 0x2dc0758 起（+4 起点、+8 目标、+0xc 进度；1.0 = 新画面）；全程序写它的只有 0x852280（启动初始化写 1）、0x85cb20（强制回新画面写 1）、
    # 0x856be0（只写 +0xc）与 0xb15d60（不在局内或局版本 1 / 2 时按当前系数在 0 / 1 间翻转目标）——后者是唯一能把目标推向 0 的地方；
    # 调用方 0x36a5a0 / 0x4035e8 / 0xc25f48 / 0xc2725c 都是切换动作（视频选项 LegacyGraphicsMode、建角页 ToggleLegacy、主机版 ClassicToggle、按键 LegacyToggle 汇入此处）。
    # 剪掉的是两条普通存储、不含调用：被剪部分的同步原语与对外副作用清单为空（剪口规矩）。局内局版本 3 的提示分支（0xb15e2c 起）不动
    # Product cut (reproducing the exclusion list item "classic picture toggle"): the two state writes of the classic picture toggle 0xb15d60 → nop.
    # The picture transition state starts at 0x2dc0758 (+4 start, +8 target, +0xc progress; 1.0 = new picture); in the whole program the only writers are 0x852280 (startup init writes 1), 0x85cb20 (force back to the new picture, writes 1),
    # 0x856be0 (writes only +0xc) and 0xb15d60 (when not in a run, or when the run version is 1 / 2, flips the target between 0 / 1 according to the current coefficient) -- the last is the only place that can push the target toward 0;
    # the callers 0x36a5a0 / 0x4035e8 / 0xc25f48 / 0xc2725c are all toggle actions (the video option LegacyGraphicsMode, the character creation page's ToggleLegacy, the console version's ClassicToggle and the key LegacyToggle all converge here).
    # What is cut is two plain stores, containing no calls: the list of synchronization primitives and external side effects of the cut part is empty (the cut rule). The hint branch for run version 3 in a run (from 0xb15e2c) is untouched
    (0xb15e20, '0281002d', '1f2003d5', '经典画面切换写起点 / 目标（stp s2, s0, [x8, #4]）→ nop：目标恒为 1.0（新画面）'),
    (0xb15e24, '1f0d00b9', '1f2003d5', '经典画面切换清进度（str wzr, [x8, #0xc]）→ nop'),
    # 宽屏（仅宿主，配合 PARAMS 的 KASPECT 宽高比上限）：界面画布宽 = min(屏宽, 4560 = 2160 × 19/9) 的上限放开到 0xffff。
    # 原版 16:9 下屏宽 3840 单位 < 4560，结果不变；iPhone 1952x900 下屏宽约 4685 单位，原先被夹到 4560、两侧各留约 26 像素
    # Widescreen (host only, together with the KASPECT aspect ratio cap in PARAMS): the cap in UI canvas width = min(screen width, 4560 = 2160 × 19/9) is opened up to 0xffff.
    # In the original game at 16:9 the screen width of 3840 units < 4560, so the result is unchanged; on iPhone 1952x900 the screen width is about 4685 units, which used to be clamped to 4560, leaving about 26 pixels on each side
    (0xb1a660, '083a8252', 'e8ff9f52', '界面画布宽上限 mov w8, #4560 → mov w8, #0xffff（画布宽 = 屏宽）', '仅宿主'),
    # （撤销：原 r13 / r15 起的「SetLanguage 0x317298 清空 + 重装字符串表」剪口 0x3172f8 / 0x3173e4（原为排除简中以外的语言而设），
    # 因英文版撤销——原版切语言要靠这次重装。）
    # 产品剪口：字符串表 A（legacy，0x2d70ff0 / 0x2d71010）的装载整段跳过。装载函数 0x7f64f0(w0 = 是否装 legacy)
    # 在 0x7f669c tbz w23 处分支：不装时成功标志置 1（0x7f6800），与 17 个 strings-legacy/*.json 全部装成功的结果相同。
    # 无读者证明：全程序引用表 A 的 128 个函数里 124 个同时读选表标志 [0x2d71098] 并按「标志 = 0 → 表 B（0x2d71030 / 0x2d71050）」选表
    # （如 0x13b7d8 csel），其余 4 个是表自身的静态构造 0x7f8f80、装载 0x7f64f0、插表 0x7f72e0、清空 0x7f5b60。标志唯一写入点 0xb1bcf4
    # 写 !obj[0xb9]，obj[0xb9] 由画面过渡状态 0x2dc0758 与 1.0 比较得出——r14 已剪掉唯一能把目标推离 1.0 的经典画面切换；标志在 .bss
    # （初值 0），r15 cvd5 全程探针 0xb1bcfc 命中 0 次（写入点从未执行）⇒ 全程只读表 B。被剪树 = 17 次 0x7f72e0 单文件装载，同上方
    # 语言重装剪口的补证（同一 0xdf67f0 读事务，成对、无外部等待者）
    # (Reverted: the cuts 0x3172f8 / 0x3173e4 for "SetLanguage 0x317298 clear + reload string tables" in place since r13 / r15 (originally set up to exclude languages other than Simplified Chinese),
    # reverted because of the English version -- switching language in the original game relies on this reload.)
    # Product cut: the loading of string table A (legacy, 0x2d70ff0 / 0x2d71010) is skipped as a whole. The load function 0x7f64f0(w0 = whether to load legacy)
    # branches at the tbz w23 at 0x7f669c: when not loading, the success flag is set to 1 (0x7f6800), the same result as all 17 strings-legacy/*.json loading successfully.
    # No-reader proof: of the 128 functions in the whole program that reference table A, 124 also read the table-select flag [0x2d71098] and select the table by "flag = 0 → table B (0x2d71030 / 0x2d71050)"
    # (e.g. the csel at 0x13b7d8); the other 4 are the table's own static constructor 0x7f8f80, load 0x7f64f0, table insert 0x7f72e0 and clear 0x7f5b60. The flag's only write site 0xb1bcf4
    # writes !obj[0xb9], and obj[0xb9] is obtained by comparing the picture transition state 0x2dc0758 with 1.0 -- r14 already cut the classic picture toggle, the only thing that can push the target away from 1.0; the flag is in .bss
    # (initial value 0), and the probe at 0xb1bcfc over the whole r15 cvd5 run hit 0 times (the write site never executed) ⇒ only table B is read throughout. The cut tree = 17 single-file loads via 0x7f72e0; same as the supplementary proof of
    # the language reload cut above (the same 0xdf67f0 read transaction, paired, no external waiters)
    (0x7f669c, '370b0036', '59000014', '字符串表装载 tbz w23, #0（是否装 legacy 表 A）→ b 0x7f6800：表 A 无读者（选表标志恒 0 → 表 B），整段 17 个 legacy 文件不装'),
    # Granny 段解码（BitKnit2）两条流式段读取路径的逐块解码调用点：宿主缺省原样转调 0x11f4af0（预解码方案 A 的替换落点，hle_granny.c）
    # The per-block decode call sites of the two streaming segment read paths of Granny segment decoding (BitKnit2): by default the host forwards as is to 0x11f4af0 (the replacement hook point of pre-decode plan A, hle_granny.c)
    (0x11f02dc, '05120094', '611000d4', '段读取 0x11f0110 逐块解码 bl 0x11f4af0(ctx, 块, 长度) → svc #0x83：宿主缺省以原参数原样调用并回填 w0（hle.h SVC_HOST_GRANNY_A）', '仅宿主'),
    # 掉落光（仅宿主）：物品取外观定义路径 bl 0xa14dc4(资源管理器, 单位, x8 输出字符串) → svc #0x87：宿主原样代调，
    # 对模式 3 / 5（地上 / 掉落中）的物品按规则把路径改指覆盖层里的带光变体（dm_dropfilter.c svc_ground_asset；hle.h SVC_HOST_GROUND_ASSET）
    # 一档掉落音（仅宿主）：落地取掉落音 bl 0x470c20(物品, &触发帧) → svc #0x88：宿主原样代调，一档物品换成提示音（dm_dropfilter.c svc_drop_sound）
    # Drop light (host only): the item's appearance definition path lookup bl 0xa14dc4(resource manager, unit, x8 output string) → svc #0x87: the host forwards the call as is,
    # and for items in mode 3 / 5 (on the ground / dropping) repoints the path by rule to the lit variant in the overlay (dm_dropfilter.c svc_ground_asset; hle.h SVC_HOST_GROUND_ASSET)
    # Tier-one drop sound (host only): fetching the drop sound on landing bl 0x470c20(item, &trigger frame) → svc #0x88: the host forwards the call as is, and tier-one items get a notification sound instead (dm_dropfilter.c svc_drop_sound)
    (0x468f2c, '3d1f0094', '011100d4', '物品落地取掉落音调用点 bl 0x470c20 → svc #0x88：宿主原样代调后按规则把一档物品的掉落音换成提示音（其余原样）', '仅宿主'),
    (0x8fa590, '0d6a0494', 'e11000d4', '物品外观路径调用点 bl 0xa14dc4 → svc #0x87：宿主原样代调后按规则改指地上物品的带光变体（存储 / 穿戴 / 腰带 / 镶嵌不碰）', '仅宿主'),
    # 进局出生点（进局落在城镇小站旁、尸体与人同处；仅宿主）：玩家进局函数 0x5d7f70 里给玩家、给尸体各取一次出生点，都是 bl 0x5596c0(数据版本, 幕, 关卡, 格类型 0, &x, &y, 单位尺寸)
    # → svc #0x8a（玩家）/ #0x8b（尸体）：宿主原样代调；进局在城镇时改按「小站格 13 + 固定安全距离」取点，人、尸体、佣兵由原版一次放好（不再第 2 拍换关挪人，尸体不再留在原出生点）（dm_townwp.c svc_join_spawn）
    # Run-entry spawn point (entering a run lands next to the town waypoint, with the corpse in the same place as the player; host only): the player join function 0x5d7f70 fetches a spawn point once for the player and once for the corpse, both via bl 0x5596c0(data version, act, level, cell type 0, &x, &y, unit size)
    # → svc #0x8a (player) / #0x8b (corpse): the host forwards the call as is; when the run is entered in town it instead picks the point by "waypoint cell 13 + a fixed safe distance", and the player, corpse and mercenary are placed in one go by the original game (the player is no longer moved by a level change on tick 2, and the corpse no longer stays at the original spawn point) (dm_townwp.c svc_join_spawn)
    (0x5d8040, 'a005fe97', '411100d4', '玩家进局取出生点调用点 bl 0x5596c0 → svc #0x8a：宿主原样代调，进局在城镇时出生点换成小站旁（hle.h SVC_HOST_JOIN_SPAWN）', '仅宿主'),
    (0x5d8400, 'b004fe97', '611100d4', '进局给尸体取落点调用点 bl 0x5596c0 → svc #0x8b：同上，尸体与人取同一个点（hle.h SVC_HOST_JOIN_CORPSE）', '仅宿主'),
    (0x11f0914, '77100094', '811000d4', '段读取 0x11f07d0 逐块解码 bl 0x11f4af0(ctx, 块, 长度) → svc #0x84：同上（hle.h SVC_HOST_GRANNY_B）', '仅宿主'),
]}

OBS = {'db0507f36b84200ac05f75d76ed5026f': [
    (0xb1dc60, 1, 'd10603ff', 'UI 消息总线虚函数实现入口（x1 = 消息 {u64 类型哈希, u64 动作哈希, char* 面板名, …}）：'
     'DM_UIMSG 消息流与 DM_BOOTPROF 启动分段的唯一来源；原指令 sub sp, sp, #0x180'),
    (0x5d3bc0, 2, 'd10503ff', '服务端拍入口（x0 = 游戏对象 G）：DM_TICKS 逐拍 6 格状态哈希（dm/verify/tick_state_format.md，与真值 swtick.py 同一采集点）；'
     '原指令 sub sp, sp, #0x140'),
    (0x6e5b40, 3, 'd10103ff', '客户端命令分派入口 (G, 单位, 包, 长度)：DM_C2S_LOG 命令包序列（dm/verify/command_packet_format.md，与真值同一采集点）；'
     '原指令 sub sp, sp, #0x40'),
    (0x6310b0, 4, 'a9bb67fc', '系统类客户端消息处理入口 (buf = u32 客户端号 + 包, 包长)：包号 ≥ 0x67（加入 / 心跳 0x6d / 0x6f 等），收包泵先抽干此队列；'
     'DM_C2S_LOG 记为 "q":"sys"；原指令 stp x28, x25, [sp, #-0x50]!'),
    (0x317298, 5, 'd10183ff', 'SetLanguage 入口（x1 → u32 新语言号，语言名表 main+0x20ae478）：配合上方语言重载剪口，宿主断言新语言为 zhCN；'
     '原指令 sub sp, sp, #0x60'),
]}

PARAMS = {'db0507f36b84200ac05f75d76ed5026f': [
    (0xe12d8c, 'd280c813', 0xe12d90, 'f2c07093', 19, 'KRES1', '启动初始化：主机模式输出分辨率 1600x900（{u32 宽, u32 高} 打包）'),
    (0xe12e1c, 'd280a008', 0xe12e20, 'f2c05a08', 8, 'KRES0', '启动初始化：掌机模式输出分辨率 1280x720'),
    (0xe10d60, 'd280c818', 0xe10d80, 'f2c07098', 24, 'KRES1', '运行模式变化通知（0x1f）处理：主机模式 1600x900'),
    (0xe10d5c, 'd280a016', 0xe10d74, 'f2c05a16', 22, 'KRES0', '运行模式变化通知（0x1f）处理：掌机模式 1280x720'),
    # 宽高比上限 19:9（float 0x40071c72）：宽于 19:9 的屏幕两侧留黑柱。宿主给 max(19/9, 输出宽 / 高) 的浮点位模式（低 32 位）；
    # 原版输出 16:9（1600x900 / 1280x720）⇒ 仍为 19/9，行为不变；iPhone 1952x900（2.17:1）⇒ 2.169，黑柱宽 0
    # Aspect ratio cap 19:9 (float 0x40071c72): screens wider than 19:9 get black bars on both sides. The host supplies the float bit pattern (low 32 bits) of max(19/9, output width / height);
    # the original game outputs 16:9 (1600x900 / 1280x720) ⇒ still 19/9, behavior unchanged; iPhone 1952x900 (2.17:1) ⇒ 2.169, black bar width 0
    (0x1663ee8, '52838e49', 0x1663eec, '72a800e9', 9, 'KASPECTUI', '界面常量缓冲 0x15 号槽 [屏宽, 屏高, 宽高比上限]：界面精灵着色器按它在上限以外画两侧黑柱（带约 20–30 像素向内的柔边，宿主另加余量把柔边推出屏幕）'),
    (0xb152bc, '52838e48', 0xb152c0, '72a800e8', 8, 'KASPECT', '新画面模式视口：边距 = (屏宽 − 屏高 × 上限) / 2，经 0x85bc90 设居中视口'),
    (0x23e888, '52838e48', 0x23e88c, '72a800e8', 8, 'KASPECT', '新画面模式坐标钳制（0x23e760）：x 夹进 [边距, 边距 + 屏高 × 上限]，宽屏两侧的指针 / 触摸不再被夹回中间'),
]}


def cuts_for(build_id, ruleset, name, private_gates):
    RULE_RUN = ruleset == "touch-run-v4" and name == "main"
    if RULE_RUN and build_id not in CUTS:
        raise ValueError(f'unsupported D2R touch build: {build_id}')
    PRIVATE_GATES = private_gates
    PRIVATE_AIM = RULE_RUN and PRIVATE_GATES
    cuts = list(CUTS.get(build_id, []))
    if PRIVATE_AIM:
        cuts.append((0x2faf04, 'fde7fe97', 'c11000d4',
          'skill-aim-v1-private: skill ground direction call only; svc86 delegates original2b4ef8 then sampled-right/original rotation+grid; zero/nonplayer/non-skill retain original; busy/town/skill/server untouched', '仅宿主'))
    if RULE_RUN:
        # 方向光阴影图边长（画质档位 1，0x168a520 填 +0x48 → 建 2 层数组 0x1697394）：2100 → 1024。授权「视觉无差别即可」；
        # mini 108 张 + 连续 16 帧：营地 / 野外 / 战斗各档到 1024 的 FLIP 与同设置重跑的噪声分不开、影子区相邻帧变化量 0.71 → 0.75；真机两张阴影合计约 0.7–1.4 ms / 帧。
        # 游戏传给着色器的阴影尺寸常量与视口随同一配置值变（转储核过）。native 规则域（Mac 对原版真值的闸门）保持 2100
        # Directional light shadow map edge length (quality level 1, 0x168a520 fills +0x48 → creates the 2-layer array 0x1697394): 2100 → 1024. Authorization: "visually indistinguishable is enough";
        # on the second Mac, 108 images + 16 consecutive frames: for camp / wilderness / combat, at every level down to 1024 the FLIP cannot be told apart from the noise of a rerun with the same settings, and the change between adjacent frames in shadow regions is 0.71 → 0.75; on a real device the two shadow maps total about 0.7–1.4 ms / frame.
        # The shadow size constants and the viewport that the game passes to the shader change with the same config value (verified by dump). The native rule domain (the Mac gate check against the original game's ground truth) stays at 2100
        SHADOW_SIDE = 1024
        movz_w14 = lambda n: struct.pack('<I', 0x52800000 | (n << 5) | 14).hex()   # movz w14,#n——字节由表达式算，不手写（手算成 #32768，阴影图 32768² 分配失败启动即崩） | movz w14,#n -- the bytes are computed by an expression, not written by hand (a hand calculation came out as #32768, and the shadow map allocation of 32768² failed, crashing at startup)
        cuts.append((0x168a580, movz_w14(2100), movz_w14(SHADOW_SIDE), f'阴影图边长 mov w14,#2100 → mov w14,#{SHADOW_SIDE}（档位 1）', '仅宿主'))
    if RULE_RUN:
        # 纯提示的系统通知不吞功能键：手柄版通知面板处理函数 0x3d84a0 遇「动作 9（PLUS）按下」就执行通知带的确认动作并吞键（给「升级通知按 PLUS 直达技能树」用）。
        # 系统通知（事件队列第 12 类，0xab5c08；如改难度后的「游戏难度设为 N…」）带的确认动作只是关掉自己，结果是通知停留的 10 秒里第一下红十字没反应（真机 + Mac 复现）。
        # 改法：系统通知不带确认动作（类别哈希存 0）；没有确认动作的通知不处理 PLUS（走原函数的「转给子控件」出口 0x3d880c），到 notificationSeconds 自己关。带确认动作的通知不变
        # Purely informational system notifications do not swallow the function key: the controller version's notification panel handler 0x3d84a0, on "action 9 (PLUS) pressed", executes the confirm action carried by the notification and swallows the key (meant for "press PLUS on a level-up notification to go straight to the skill tree").
        # The confirm action carried by a system notification (event queue class 12, 0xab5c08; e.g. 「游戏难度设为 N…」(game difficulty set to N...) after changing the difficulty) only closes itself, and the result is that during the 10 seconds the notification stays up the first press of the red cross does nothing (reproduced on a real device + Mac).
        # Fix: system notifications carry no confirm action (the category hash is stored as 0); notifications without a confirm action do not handle PLUS (they take the original function's "forward to child widgets" exit 0x3d880c) and close by themselves at notificationSeconds. Notifications with a confirm action are unchanged
        cbz_x8 = lambda at, to: struct.pack('<I', 0xB4000008 | (((to - at) >> 2) & 0x7ffff) << 5).hex()
        str_x19_178 = lambda rt: struct.pack('<I', 0xF9000000 | (0x178 // 8) << 10 | 19 << 5 | rt).hex()
        cuts.append((0xab5c28, str_x19_178(8), str_x19_178(31), '系统通知的确认动作类别 str x8,[x19,#0x178] → str xzr（不带确认动作）', '仅宿主'))
        cuts.append((0x3d853c, cbz_x8(0x3d853c, 0x3d8560), cbz_x8(0x3d853c, 0x3d880c), '通知面板：确认动作类别为空 → 不处理 PLUS', '仅宿主'))
        cuts.append((0x3d8544, cbz_x8(0x3d8544, 0x3d8560), cbz_x8(0x3d8544, 0x3d880c), '通知面板：确认动作哈希为空 → 不处理 PLUS', '仅宿主'))
    if RULE_RUN:
        # 技能说明框出在技能树侧边、不盖树：技能树面板每帧更新（0xc356b0）在 0xc35fd4 调说明框入口 0xb16660，位置参数 x1 = 图标绝对坐标（低 32 位 x、高 32 位 y + 3），
        # 手柄版说明框贴着图标右下展开，触屏上正好盖住手指周围的几个技能。0xc35fb0 的 bfxil x1,x8,#0,#32 是把 x1 的低半拷回 x1 的空操作（x8 = x1），
        # 换成 movk x1,#3000：横坐标给到屏幕外，由说明框入口自己的回夹（0xb0d560，按 TooltipsPanel 的 screenEdgePadding）贴到右安全区边；纵坐标不动
        # The skill tooltip appears beside the skill tree and does not cover the tree: the skill tree panel's per-frame update (0xc356b0) calls the tooltip entry 0xb16660 at 0xc35fd4, with position argument x1 = the icon's absolute coordinates (low 32 bits x, high 32 bits y + 3);
        # the controller version's tooltip expands against the icon's lower right, which on a touchscreen covers exactly the few skills around the finger. The bfxil x1,x8,#0,#32 at 0xc35fb0 is a no-op that copies the low half of x1 back into x1 (x8 = x1),
        # and is replaced with movk x1,#3000: the x coordinate is put off screen, and the tooltip entry's own clamp-back (0xb0d560, by TooltipsPanel's screenEdgePadding) snaps it to the right safe-area edge; the y coordinate is untouched
        cuts.append((0xc35fb0, struct.pack('<I', 0xB3407D01).hex(), struct.pack('<I', 0xF2800000 | 3000 << 5 | 1).hex(), '技能说明框横坐标给到屏幕外，由原回夹贴右边（不盖技能树）', '仅宿主'))
    if RULE_RUN and PRIVATE_GATES:
        # 只登记精力资格/移动选择/走跑偏好；不改方向、真实幅度、合法mode与零轴分支。
        # 两个成本/选择器通用点的type0防护在下方原生小跳板，非player保原行为。
        # Only stamina eligibility / movement selection / walk-run preference are registered; direction, real magnitude, legal mode and the zero-axis branch are not changed.
        # The type0 guard for the two generic cost / selector points is in the small native trampoline below; non-player keeps the original behavior.
        run_words = [
            (0x2c620c, 0xb90e511f, 0x52800029, '偏好初始化：mov w9,1（x8仍为2d20000）'),
            (0x2c6210, 0xd00152c8, 0xb90e5109, '偏好初始化：str w9,[x8,e50]；替代同页重复adrp，保原x8'),
            (0x369a90, 0xd100c3ff, None, '客户端RunOff入口尾跳原RunOn；保其player0/合法mode/环境门'),
            (0x369980, 0x340006a0, 0xd503201f, '客户端RunOn仅取消既有player0门之后的零精力拒绝'),
            (0x2b2c74, 0x340000a0, 0xd503201f, '控制器d29资格仅取消stat10零值限制；幅度比较与停杆保原'),
            (0x2b2dc0, 0x3900031f, 0xd503201f, '控制器d29仅取消stat10零值清除；幅度归一化保原'),
            (0x2b30fc, 0x0a090101, 0x52800021, '已通过合法动作/零轴门的移动编码选择位固定Run；幅度不改'),
            (0x2b3104, 0x2a1f03e1, 0x52800021, '无用户走跑偏好时亦固定Run编码位；方向/幅度不改'),
            (0x6dc2c4, 0x1a880502, 0x52800062, 'server42合法移动请求固定Run3，原type0/死亡/锁定/幅度/方向门保留'),
            (0x6dce90, 0x340000e0, 0xd503201f, 'server49仅取消既有player0/mode2门之后的零精力拒绝'),
            (0x6dcec0, 0xa9bf7bfd, None, 'server4A入口尾跳原49；长度/type0/mode门保留'),
        ]
        for off, old, new, why in run_words:
            if new is None:
                target = 0x369900 if off == 0x369a90 else 0x6dce50
                new = 0x14000000 | (((target - off) >> 2) & 0x3ffffff)
            cuts.append((off, struct.pack('<I', old).hex(), struct.pack('<I', new).hex(), 'touch-run-v4：' + why))
    return cuts
