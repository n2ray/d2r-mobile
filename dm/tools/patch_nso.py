# 通用 NSO 改写器：Switch aarch64 模块 → 可在苹果系统原生执行的映像。
# 用法：patch_nso.py <NSO 文件> <输出目录> <模块名>
# 改写（苹果内核会改 x18 与 tpidr_el0；Horizon 的线程区、svc、计时器在苹果上没有对应物）：
#   x18 读写            → 线程槽 K18（TSD，经 tpidrro_el0）
#   mrs tpidr_el0       → 读线程槽 KTLS；msr tpidr_el0 → 写 KTLS
#   mrs tpidrro_el0     → 读线程槽 KTLR（Horizon ThreadLocalRegion 指针）
#   svc #n、mrs cntpct_el0 → 宿主门（门地址取自线程槽 KGATE，进 C 后按返回点查 gates.tsv）
#   已登记剪口（CUTS，按 NSO 构建号限定）：改写前逐条断言原字节，不符即中止；结果列在 <名>.cuts.tsv
#   已登记观测门（OBS，按构建号限定）：入口一条指令换成「进宿主观测回调 → 执行原指令 → 跳回」，诊断零运行时改代码；列在 <名>.obs.tsv
# 键页只在数据段末尾保存“实际键号 × 8”，补丁代码通过键页间接取线程槽：装载器可使用任意键号。
# 输出：<名>.patched.bin（键页与补丁区接在映像后 PATCH_OFF）、<名>.fixups.tsv（布局与键页下标表头）、<名>.gates.tsv、<名>.sites.tsv、<名>.cuts.tsv、<名>.obs.tsv
# Generic NSO rewriter: Switch aarch64 module → an image that can execute natively on Apple systems.
# Usage: patch_nso.py <NSO file> <output dir> <module name>
# Rewrites (the Apple kernel modifies x18 and tpidr_el0; Horizon's thread area, svc and timers have no counterpart on Apple):
#   x18 read/write      → thread slot K18 (TSD, via tpidrro_el0)
#   mrs tpidr_el0       → read thread slot KTLS; msr tpidr_el0 → write KTLS
#   mrs tpidrro_el0     → read thread slot KTLR (Horizon ThreadLocalRegion pointer)
#   svc #n, mrs cntpct_el0 → host gate (gate address taken from thread slot KGATE; once in C, gates.tsv is looked up by return point)
#   Registered cuts (CUTS, restricted by NSO build ID): before rewriting, the original bytes are asserted one by one and any mismatch aborts; results are listed in <name>.cuts.tsv
#   Registered observation gates (OBS, restricted by build ID): one instruction at the entry is replaced with "enter the host observation callback → execute the original instruction → jump back", so diagnostics modify zero code at runtime; listed in <name>.obs.tsv
# The key page only stores "actual key number × 8" at the end of the data segment, and patch code fetches thread slots indirectly through the key page: the loader may use any key number.
# Output: <name>.patched.bin (key page and patch area appended after the image at PATCH_OFF), <name>.fixups.tsv (layout and key page index header), <name>.gates.tsv, <name>.sites.tsv, <name>.cuts.tsv, <name>.obs.tsv
import struct, re, sys, os
from itertools import combinations
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "games"))
import game as game_profiles
GAME_PROFILE = game_profiles.load()
GAME_PATCHES = GAME_PROFILE.patches()
import lz4.block
from capstone import *

src, outdir, NAME = sys.argv[1], sys.argv[2], sys.argv[3]
d = open(src, 'rb').read()
assert d[:4] == b'NSO0'
flags = struct.unpack_from('<I', d, 0xc)[0]
segs = [struct.unpack_from('<III', d, 0x10 + i * 0x10) for i in range(3)]
bss = struct.unpack_from('<I', d, 0x3c)[0]
csz = struct.unpack_from('<III', d, 0x60)
IMG = bytearray(segs[2][1] + segs[2][2] + bss)
for i, (fo, mo, sz) in enumerate(segs):
    raw = d[fo:fo + csz[i]]
    if flags >> i & 1: raw = lz4.block.decompress(raw, uncompressed_size=sz)
    IMG[mo:mo + sz] = raw
TEXT_END = segs[0][2]
RO_OFF, DATA_OFF = segs[1][1], segs[2][1]
KEY_PAGE_OFF = (len(IMG) + 0x3FFF) & ~0x3FFF
IMG.extend(bytes(0x4000))
PATCH_OFF = (len(IMG) + 0xFFFF) & ~0xFFFF

# ---- 已登记剪口：{NSO 构建号: [(偏移, 原字节, 新字节, 理由)]} ----
# 联网剪口（排除范围：战网 / 联机）：取自社区离线修复（LFS Mod Offline Fix for v2228224，与官方 94012 main 只差这 7 个字）。
# 实测系统服务层（nifm 无网 / DNS 黑洞 / 账号服务）绕不过：游戏自己直接发战网请求、按「上次在线」时间放行离线。
# ---- Registered cuts: {NSO build ID: [(offset, original bytes, new bytes, reason)]} ----
# Networking cuts (excluded scope: Battle.net / online play): taken from a community offline fix (LFS Mod Offline Fix for v2228224, which differs from the official 94012 main in only these 7 words).
# Measured: the system service layer (nifm offline / DNS black hole / account service) cannot get around it: the game itself sends Battle.net requests directly and allows offline play according to the "last online" time.
CUTS = GAME_PATCHES.CUTS
# ---- 已登记观测门：{NSO 构建号: [(偏移, 编号, 原指令字, 理由)]}。编号与宿主 hle.h 的 OBS_* 一致；
# 原指令须与位置无关（不得 PC 相对 / 分支 / 涉 x18·tpidr），离线断言；宿主未挂回调时门块进出一次即返回 ----
# ---- Registered observation gates: {NSO build ID: [(offset, number, original instruction word, reason)]}. Numbers match OBS_* in the host's hle.h;
# the original instruction must be position independent (no PC-relative / branch / anything involving x18 or tpidr), asserted offline; when the host has no callback attached the gate block goes in and out once and returns ----
OBS = GAME_PATCHES.OBS
BUILD_ID = d[0x40:0x50].hex()
# 构建期产品规则：旧原资源/对拍域默认不变；触屏版单独出新版本，不在运行时写客体代码。
# Build-time product rules: the old original-resource / comparison domain is unchanged by default; the touchscreen version is issued separately as a new version, and guest code is not written at runtime.
RULESET = os.environ.get('DM_RULESET', 'native')
assert RULESET in ('native', 'touch-run-v4'), f'未知规则域 {RULESET}'
RULE_BUILD = GAME_PROFILE.builds["main"]
RULE_RUN = RULESET == 'touch-run-v4' and NAME == 'main'
if RULE_RUN:
    import hashlib
    assert BUILD_ID == RULE_BUILD, f'Run规则构建号不符 {BUILD_ID}'
    RULE_ORIG_SHA = hashlib.sha256(IMG).hexdigest()
# 触屏版规则域默认同时带宿主私有门；规则闸门可显式关闭，保留同一出包入口和规则域。
# The touchscreen rule domain by default also carries the host-private gates; the rule gate check can turn them off explicitly, keeping the same packaging entry point and rule domain.
PRIVATE_GATES = os.environ.get("DM_PRIVATE_GATES", "1") != "0"
PRIVATE_AIM = RULE_RUN and PRIVATE_GATES
cuts = GAME_PATCHES.cuts_for(BUILD_ID, RULESET, NAME, PRIVATE_GATES)
cuts = [c if len(c) == 5 else (*c, '全') for c in cuts]          # 第 5 项作用域：全 | 仅宿主（仅宿主 = 真值模拟器不应用） | 5th item is the scope: 全 (all) | 仅宿主 (host only) (仅宿主 = not applied by the ground-truth emulator)
for off, ob, nb, why, scope in cuts:
    ob, nb = bytes.fromhex(ob), bytes.fromhex(nb)
    assert len(ob) == len(nb) and IMG[off:off + len(ob)] == ob, f'剪口原字节不符 {off:#x}：{bytes(IMG[off:off + len(ob)]).hex()}'
    IMG[off:off + len(nb)] = nb
# 键页下标固定；键页内容由装载器写入“实际键号 × 8”。
# Key page indices are fixed; the key page content is written by the loader as "actual key number × 8".
KEYS = {'K18': 508, 'KTLS': 509, 'KTLR': 510, 'KGATE': 511, 'KRES0': 506, 'KRES1': 507, 'KASPECT': 505, 'KASPECTUI': 504}   # KRES*：分辨率参数位；KASPECT：画面宽高比上限（见 PARAMS） | KRES*: resolution parameter slots; KASPECT: picture aspect ratio cap (see PARAMS)
KEY_INDEX = {k: i for i, k in enumerate(('K18', 'KTLS', 'KTLR', 'KGATE', 'KRES0', 'KRES1', 'KASPECT', 'KASPECTUI'))}

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); md.detail = True
patch = bytearray(); fix = []; gates = []; stats = {}
def stat(k): stats[k] = stats.get(k, 0) + 1
def here(): return PATCH_OFF + len(patch)
def emit(w, kind=None):
    if kind: fix.append((len(patch), kind))
    patch.extend(struct.pack('<I', w & 0xFFFFFFFF))

def emit_key_addr(reg, kind):
    fix.append((len(patch), kind)); emit(ADRP(reg, here(), KEY_PAGE_OFF)); emit(LDRX(reg, reg, KEY_INDEX[kind]))

def emit_slot_addr(base, key_reg, kind):
    emit_key_addr(key_reg, kind); emit(ADDR(base, base, key_reg))

# ---- 编码器 ----
# ---- Encoders ----
def B(frm, to):
    off = to - frm; assert -(1 << 27) <= off < (1 << 27) and off % 4 == 0
    return 0x14000000 | ((off >> 2) & 0x3FFFFFF)
def STP(t, t2, n, off): return 0xA9000000 | (((off // 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t
def LDP(t, t2, n, off): return 0xA9400000 | (((off // 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t
def STP_PRE(t, t2, n, off): return 0xA9800000 | (((off // 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t
def LDP_POST(t, t2, n, off): return 0xA8C00000 | (((off // 8) & 0x7F) << 15) | (t2 << 10) | (n << 5) | t
def STUR(t, n, off): return 0xF8000000 | ((off & 0x1FF) << 12) | (n << 5) | t   # stur xt,[xn,#off]（off 可负） | stur xt,[xn,#off] (off may be negative)
def LDUR(t, n, off): return 0xF8400000 | ((off & 0x1FF) << 12) | (n << 5) | t   # ldur xt,[xn,#off]
def LDRX(t, n, imm12=0): return 0xF9400000 | (imm12 << 10) | (n << 5) | t
def STRX(t, n, imm12=0): return 0xF9000000 | (imm12 << 10) | (n << 5) | t
def MRS_TPIDRRO(t): return 0xD53BD060 | t
def MRS_TPIDR(t): return 0xD53BD040 | t
# 线程槽基址：苹果 = tpidrro_el0（pthread TSD 数组，槽 = 基址 + 键号 × 8）；
# 安卓（DM_TARGET=android）= bionic 的 pthread 键表：tpidr_el0 指向 TCB，TCB[-1]（TLS_SLOT_BIONIC_TLS）是 bionic_tls，
# 键页里放「键表偏移 + 键号 × 16 + 8」（装载器实测后写入）；其余补丁代码两边逐字相同，只多一条 ldur。
# 鸿蒙（DM_TARGET=ohos）= musl：tpidr_el0 − 272 处是 struct pthread 的 tsd 指针，槽 = tsd + 键号 × 8（真机实测，装载器复核）。
# Thread slot base: Apple = tpidrro_el0 (the pthread TSD array, slot = base + key number × 8);
# Android (DM_TARGET=android) = bionic's pthread key table: tpidr_el0 points to the TCB, TCB[-1] (TLS_SLOT_BIONIC_TLS) is bionic_tls,
# the key page holds "key table offset + key number × 16 + 8" (measured and written by the loader); the rest of the patch code is word-for-word the same on both, with just one more ldur.
# HarmonyOS (DM_TARGET=ohos) = musl: tpidr_el0 - 272 holds struct pthread's tsd pointer, slot = tsd + key number × 8 (measured on device, re-checked by the loader).
TARGET = os.environ.get('DM_TARGET', 'apple')
assert TARGET in ('apple', 'android', 'ohos'), f'DM_TARGET 只能是 apple / android / ohos：{TARGET}'
OHOS_TSD_OFF = 272
def emit_tp(t):
    if TARGET == 'android': emit(MRS_TPIDR(t)); emit(LDUR(t, t, -8))
    elif TARGET == 'ohos': emit(MRS_TPIDR(t)); emit(SUBI(t, t, OHOS_TSD_OFF)); emit(LDRX(t, t))   # −272 超出 ldur 的 ±256 | −272 is outside ldur's ±256
    else: emit(MRS_TPIDRRO(t))
# 安卓的改写块不把寄存器暂存在 sp 下方：Linux / 安卓的 AAPCS64 没有红区，处理函数没设 SA_ONSTACK 的信号（libcore 34、heapprofd 36、
# ART 38 等）在线程栈上建信号帧，正好盖掉 sp-8 / sp-16。所以先下移 sp 再存（[sp, #-16]!），恢复时连 sp 一起收回（[sp], #16）；
# 用到 sp 的原指令把偏移加上这 16 字节，写回 sp 的原指令另排栈位（见 sp_frame）。编不出来的个别站点保持旧排法，计入 below_sp。
# 苹果目标不变（arm64 苹果 ABI 有 128 字节红区）。
# Android rewrite blocks do not stash registers below sp: Linux / Android AAPCS64 has no red zone, and a signal whose handler lacks
# SA_ONSTACK (libcore 34, heapprofd 36, ART 38 and others) builds its frame on the thread stack, right over sp-8 / sp-16. So sp moves
# down first ([sp, #-16]!) and comes back with the restore ([sp], #16); an original instruction that uses sp gets those 16 bytes added
# to its offset, and one that writes sp back gets its own stack layout (see sp_frame). The odd site that cannot be encoded keeps the
# old layout and is counted as below_sp. The Apple target is unchanged (Apple's arm64 ABI has a 128-byte red zone).
NOREDZONE = TARGET == 'android'   # 鸿蒙（ohos）暂沿用旧排法：现有鸿蒙改写产物都按它生成并实测过；musl 同样没有红区，改用新排法需重出产物再实测 | HarmonyOS (ohos) keeps the old layout for now: every HarmonyOS module so far was generated and tested with it; musl has no red zone either, so switching needs regenerated modules and a new device test
def STR_PRE(t, n, off): return 0xF8000C00 | ((off & 0x1FF) << 12) | (n << 5) | t
def LDR_POST(t, n, off): return 0xF8400400 | ((off & 0x1FF) << 12) | (n << 5) | t
def stash(S, T, nrz=None):
    emit(STP_PRE(S, T, 31, -16) if (NOREDZONE if nrz is None else nrz) else STP(S, T, 31, -16))
def unstash(S, T, nrz=None):
    emit(LDP_POST(S, T, 31, 16) if (NOREDZONE if nrz is None else nrz) else LDP(S, T, 31, -16))
def uses_sp(ins): return re.search(r'\bw?sp\b', ins.op_str) is not None
def ADRP(dd, pc, target):
    imm = ((target & ~0xFFF) - (pc & ~0xFFF)) >> 12; assert -(1 << 20) <= imm < (1 << 20)
    return 0x90000000 | ((imm & 3) << 29) | (((imm >> 2) & 0x7FFFF) << 5) | dd
def ADDI(dd, n, imm12, sh=0): return 0x91000000 | (sh << 22) | (imm12 << 10) | (n << 5) | dd
def ADDR(dd, n, m): return 0x8B000000 | (m << 16) | (n << 5) | dd
def SUBI(dd, n, imm12, sh=0): return 0xD1000000 | (sh << 22) | (imm12 << 10) | (n << 5) | dd
def BR(n): return 0xD61F0000 | (n << 5)
def BLR(n): return 0xD63F0000 | (n << 5)
NOP = 0xD503201F

def dis(w, addr):
    l = list(md.disasm(struct.pack('<I', w), addr)); assert len(l) == 1, hex(w)
    return l[0]
def regset(ins):
    r, w = ins.regs_access()
    return {ins.reg_name(x) for x in r}, {ins.reg_name(x) for x in w}
def num(name):
    m = re.fullmatch(r'[xw](\d+)', name or ''); return int(m.group(1)) if m else None
def has18(ins): return re.search(r'\b[xw]18\b', ins.op_str) is not None
def regs_in(ins):
    s = {int(n) for n in re.findall(r'\b[xw](\d+)\b', ins.op_str)}
    try:
        rd, wr = regset(ins); s |= {num(n) for n in rd | wr} - {None}
    except Exception: pass
    return s
def pick(avoid, n=2):
    out = [r for r in range(0, 16) if r not in avoid and r != 18][:n]; assert len(out) == n; return out

def subst(ins, S):
    """x18/w18 → xS/wS：只改 4 个寄存器字段位置，用反汇编文本核对。 | x18/w18 → xS/wS: only the 4 register field positions are changed, verified against the disassembly text."""
    w = struct.unpack('<I', ins.bytes)[0]
    want = re.sub(r'\b([xw])18\b', lambda m: f'{m.group(1)}{S}', ins.op_str)
    pos = [p for p in (0, 5, 10, 16) if (w >> p) & 31 == 18]
    for k in range(len(pos), 0, -1):
        for c in combinations(pos, k):
            nw = w
            for p in c: nw = (nw & ~(31 << p)) | (S << p)
            try: dd = dis(nw, ins.address)
            except AssertionError: continue
            if dd.mnemonic == ins.mnemonic and dd.op_str == want: return nw
    raise RuntimeError(f'subst fail {ins.address:x} {ins.mnemonic} {ins.op_str}')

# ---- 各类改写块 ----
# ---- Rewrite blocks of each kind ----
def sp_delta(ins):
    """指令对 sp 的写回量（前变址 [sp, #i]! / 后变址 [sp], #i）；不写回 sp 返回 0。 | The amount an instruction writes back to sp (pre-index [sp, #i]! / post-index [sp], #i); returns 0 if sp is not written back."""
    m = re.search(r'\[sp, #(-?(?:0x)?[0-9a-f]+)\]!', ins.op_str)
    if m: return int(m.group(1), 0)
    m = re.search(r'\[sp\], #(-?(?:0x)?[0-9a-f]+)$', ins.op_str)
    if m: return int(m.group(1), 0)
    assert not re.match(r'(sp|wsp),', ins.op_str), f'writes sp: {ins.address:x} {ins.op_str}'
    return 0

# 安卓：sp 先下移 F 字节后，原指令（x18 已换成 S 的字 w）要照原样访问同一地址、把 sp 写回到原值，得怎么写。返回 (F, 指令字列表)，
# 块尾 ldp xS, xT, [sp], #(F + dlt) 一并把 sp 调到原指令执行后的值；编不出来返回 None（该站点保持旧排法）。
# 不碰 sp：F = 16，原样。用到 sp 但不写回：偏移 +16（无符号偏移 ldr/str 各位宽、非缩放 ldur/stur、成对有符号偏移 ldp/stp、
# add xd, sp, #imm，lsl #12 的那种后面补一条 add xd, xd, #16）；原访存落在 sp 下方的会和暂存区重叠，不改。
# 成对前 / 后变址：改成有符号偏移形，F 取到暂存区落在原访存之下。
# Android: how the original instruction (word w, x18 already replaced with S) must be written so that, after sp has moved down F bytes,
# it accesses the same address as before and sp is written back to the original's value. Returns (F, list of words); the block's
# closing ldp xS, xT, [sp], #(F + dlt) also brings sp to its value after the original instruction. None when it cannot be encoded
# (the site keeps the old layout).
# sp not used: F = 16, unchanged. sp used without write-back: offset + 16 (unsigned-offset ldr/str of every width, unscaled ldur/stur,
# signed-offset ldp/stp pairs, add xd, sp, #imm; the lsl #12 form gets an extra add xd, xd, #16); an original access below sp would
# overlap the stash and is left alone. Pre- / post-index pairs: rewritten in the signed-offset form, with F large enough that the
# stash lies below the original access.
SPOP = r'\[sp(?:, #-?(?:0x)?[0-9a-f]+)?\](?:!|, #-?(?:0x)?[0-9a-f]+)?|sp(?:, #(?:0x)?[0-9a-f]+(?:, lsl #12)?)?$'
def sp_frame(ins, w, dlt):
    if not uses_sp(ins): return 16, [w]
    F, rn = 16, (w >> 5) & 31
    # 反汇编核对：助记符相同（mov xd, sp 变成 add），除 sp 那一项外操作数逐字相同，新偏移确实是 off。
    # Checked by disassembly: the same mnemonic (mov xd, sp becomes add), operands identical apart from the sp one, and the new offset is off.
    def same(nw, off):
        d, o = dis(nw, ins.address), dis(w, ins.address)
        if d.mnemonic != o.mnemonic and (o.mnemonic, d.mnemonic) != ('mov', 'add'): return False
        return re.sub(SPOP, 'SP', d.op_str) == re.sub(SPOP, 'SP', o.op_str) and (f'#{off:#x}]' in d.op_str or d.op_str.endswith(f'sp, #{off:#x}'))
    if (w >> 27) & 7 == 0b101 and not (w >> 26) & 1 and rn == 31:
        opc, mode, L = w >> 30, (w >> 23) & 7, (w >> 22) & 1
        if opc == 3 or (opc == 1 and not L): return None
        sc = 8 if opc == 2 else 4
        imm = (((w >> 15) & 0x7F) ^ 0x40) - 0x40
        if mode == 2:
            a = imm * sc
            if a < 0: return None
        elif mode in (1, 3):
            a = dlt if mode == 3 else 0
            while a < 16 - F: F += 16
        else: return None
        off = a + F
        if off % sc or (F + dlt) % 8 or not -512 <= F + dlt <= 504: return None
        if mode == 2 and off // sc > 63:
            # 偏移 +16 后超出成对指令的范围（32 位对 252、64 位对 504）：拆成两条无符号偏移的单寄存器 ldr / str（ldpsw → ldrsw）。
            # Offset + 16 is out of the pair's range (252 for 32-bit pairs, 504 for 64-bit ones): split it into two unsigned-offset
            # single-register ldr / str (ldpsw -> ldrsw).
            o = dis(w, ins.address); regs = o.op_str.split(', [')[0].split(', ')
            size, opc = (3 if opc == 2 else 2), ((2 if opc == 1 else 1) if L else 0)
            out = []
            for k, rt in enumerate((w & 31, (w >> 10) & 31)):
                nw = 0x39000000 | (size << 30) | (opc << 22) | (((off + k * sc) // sc) << 10) | (31 << 5) | rt
                d = dis(nw, ins.address)
                if not d.mnemonic.startswith(('ldr', 'str')) or d.op_str != f'{regs[k]}, [sp, #{off + k * sc:#x}]': return None
                out.append(nw)
            return F, out
        if not -64 <= off // sc <= 63: return None
        nw = (w & ~(0x7F << 15) & ~(7 << 23)) | (((off // sc) & 0x7F) << 15) | (2 << 23)
        return (F, [nw]) if same(nw, off) else None
    if dlt: return None
    if (w >> 27) & 7 == 0b111 and not (w >> 26) & 1 and rn == 31:
        size, sc = w >> 30, 1 << (w >> 30)
        if (w >> 24) & 3 == 1:
            if size == 3 and (w >> 22) & 3 == 2: return None
            off = ((w >> 10) & 0xFFF) * sc + F
            if off // sc > 0xFFF: return None
            nw = (w & ~(0xFFF << 10)) | ((off // sc) << 10)
            return (F, [nw]) if same(nw, off) else None
        if (w >> 24) & 3 == 0 and not (w >> 21) & 1 and (w >> 10) & 3 == 0:
            off = ((((w >> 12) & 0x1FF) ^ 0x100) - 0x100) + F
            if not F <= off <= 255: return None
            nw = (w & ~(0x1FF << 12)) | ((off & 0x1FF) << 12)
            return (F, [nw]) if same(nw, off) else None
        return None
    if w & 0xFF800000 == 0x91000000 and rn == 31 and (w & 31) != 31:
        if (w >> 22) & 1: return F, [w, ADDI(w & 31, w & 31, F)]
        off = ((w >> 10) & 0xFFF) + F
        if off > 0xFFF: return None
        nw = (w & ~(0xFFF << 10)) | (off << 10)
        return (F, [nw]) if same(nw, off) else None
    return None

def site_plain(ins):
    S, T = pick(regs_in(ins))
    dlt = sp_delta(ins)
    fr = sp_frame(ins, subst(ins, S), dlt) if NOREDZONE else None
    if fr:
        F, words = fr
        start = here()
        emit(STP_PRE(S, T, 31, -F)); emit_tp(T); emit_slot_addr(T, S, 'K18'); emit(LDRX(S, T))
        for x in words: emit(x)
        emit(STRX(S, T))
        emit(LDP_POST(S, T, 31, F + dlt)); emit(B(here(), ins.address + 4))
        return start
    if NOREDZONE: stat('below_sp')
    save = -16 if dlt == 0 else -112          # 改 sp 的指令会写到 sp 下方，暂存放到保留区深处 | Instructions that modify sp write below sp, so the scratch save is placed deep in the reserved area
    assert dlt == 0 or dlt >= -96, f'sp delta {dlt} at {ins.address:x}'
    start = here()
    emit(STP(S, T, 31, save)); emit_tp(T); emit_slot_addr(T, S, 'K18'); emit(LDRX(S, T))
    emit(subst(ins, S))
    emit(STRX(S, T))                 # 一律写回：capstone 会漏报写入（adds w18 实例） | Always write back: capstone can miss reporting a write (the adds w18 instance)
    emit(LDP(S, T, 31, save - dlt)); emit(B(here(), ins.address + 4))
    return start

def site_cond(ins):
    w = struct.unpack('<I', ins.bytes)[0]
    S, T = pick(regs_in(ins))
    tgt = ins.operands[-1].imm
    start = here()
    stash(S, T); emit_tp(T); emit_slot_addr(T, S, 'K18'); emit(LDRX(S, T))
    m = ins.mnemonic
    taken = here() + 4 * 3
    off = (taken - here()) >> 2
    nw = ((w & 0xFF000000) | ((off & 0x7FFFF) << 5) | S) if m in ('cbz', 'cbnz') else ((w & 0xFFF80000) | ((off & 0x3FFF) << 5) | S)
    pc = here(); emit(nw)
    dd = dis(nw, pc); assert dd.mnemonic == m and dd.operands[-1].imm == taken
    unstash(S, T); emit(B(here(), ins.address + 4))
    assert here() == taken
    unstash(S, T); emit(B(here(), tgt))
    return start

def site_pcrel(ins):
    tgt = ins.operands[1].imm
    S, T = pick(set())
    start = here()
    stash(S, T); emit_tp(T); emit_slot_addr(T, S, 'K18')   # S 先作键页偏移暂存，下一条再装目标值 | S first serves as a temporary for the key page offset; the next instruction then loads the target value
    pc = here(); emit(ADRP(S, pc, tgt))
    if ins.mnemonic == 'adr': emit(ADDI(S, S, tgt & 0xFFF))
    emit(STRX(S, T))
    unstash(S, T); emit(B(here(), ins.address + 4))
    return start

def site_br(ins, base, count):
    """跳转表 br x18：目标 = base + 4i。x16 暂存红区 → 等距落地垫 → 恢复 x16 → 原目标。 | Jump table br x18: target = base + 4i. Stash x16 in the red zone → equally spaced landing pads → restore x16 → original target."""
    start = here()
    if NOREDZONE: emit(STP_PRE(16, 17, 31, -16))
    else: emit(STUR(16, 31, -16)); emit(STUR(17, 31, -24))   # x16 / x17 是函数内可存活的临时寄存器，借用前存红区 | x16 / x17 are temporary registers that can stay live within a function; save them to the red zone before borrowing
    emit_tp(16); emit_slot_addr(16, 17, 'K18'); emit(LDRX(16, 16)); emit(LDRX(17, 31, 1) if NOREDZONE else LDUR(17, 31, -24))
    padbase = start + 4 * 24
    delta = padbase - base
    op = ADDI if delta >= 0 else SUBI; dl = abs(delta)
    ops = []; hi, lo = dl >> 12, dl & 0xFFF
    while hi: h = min(hi, 0xFFF); ops.append(op(16, 16, h, 1)); hi -= h
    if lo: ops.append(op(16, 16, lo, 0))
    assert len(ops) <= 12
    for o in ops: emit(o)
    emit(BR(16))
    while here() < padbase: emit(NOP)
    tails = padbase + 4 * count
    for i in range(count): emit(B(here(), tails + 8 * i))
    for i in range(count):
        emit(LDR_POST(16, 31, 16) if NOREDZONE else 0xF85F0000 | (31 << 5) | 16)      # ldr x16,[sp],#16 | ldur x16,[sp,#-16]
        emit(B(here(), base + 4 * i))
    return start

def site_sysreg_read(ins, kind):
    """mrs xN, tpidr_el0 / tpidrro_el0 → xN = 线程槽值。 | mrs xN, tpidr_el0 / tpidrro_el0 → xN = thread slot value."""
    N = ins.bytes[0] & 31
    tmp = 16 if N != 16 else 17
    start = here()
    emit(STR_PRE(tmp, 31, -16) if NOREDZONE else STUR(tmp, 31, -16)); emit_tp(N); emit_slot_addr(N, tmp, kind); emit(LDRX(N, N)); emit(LDR_POST(tmp, 31, 16) if NOREDZONE else LDUR(tmp, 31, -16))   # tmp 存红区借用 | tmp is saved by borrowing the red zone
    emit(B(here(), ins.address + 4))
    return start

def site_tpidr_write(ins):
    N = struct.unpack('<I', ins.bytes)[0] & 31
    (T,) = pick({N}, 1)
    start = here()
    tmp = next(x for x in (16, 17) if x not in (N, T))
    if NOREDZONE:
        emit(STP_PRE(T, tmp, 31, -16)); emit_tp(T); emit_slot_addr(T, tmp, 'KTLS'); emit(STRX(N, T)); emit(LDP_POST(T, tmp, 31, 16))
        emit(B(here(), ins.address + 4))
        return start
    emit(0xF81F0000 | (31 << 5) | T)           # stur xT,[sp,#-16]
    emit(STUR(tmp, 31, -24)); emit_tp(T); emit_slot_addr(T, tmp, 'KTLS'); emit(STRX(N, T)); emit(LDUR(tmp, 31, -24))
    emit(0xF85F0000 | (31 << 5) | T)           # ldur xT,[sp,#-16]
    emit(B(here(), ins.address + 4))
    return start

def emit_gate_call():
    """门块公共段（紧跟 stp x29,x30,[sp,#-16]! 之后、blr x30 之前）：x30 = 线程槽 KGATE 的门地址。
    键页取槽要借第二个寄存器，借 x29 后立即从栈上恢复：_dm_gate 存下的 x29 必须是客体原值——
    宿主按 [x29+8] 认调用者（技能绑定观测门 6、私有瞄准门 svc86）、崩溃回溯也走 x29 链
    （r24 方案 C 起借了不还，宿主拿到的 x29 是键页里的槽偏移，技能图标退回占位字）。
    Common section of a gate block (right after stp x29,x30,[sp,#-16]! and before blr x30): x30 = the gate address in thread slot KGATE.
        Fetching the slot through the key page needs a second register to be borrowed; x29 is borrowed and then immediately restored from the stack: the x29 saved by _dm_gate must be the guest's original value --
        the host identifies the caller by [x29+8] (skill binding observation gate 6, private aiming gate svc86), and the crash backtrace also walks the x29 chain
        (since r24 plan C it was borrowed and not returned, the x29 the host got was the slot offset from the key page, and skill icons fell back to placeholder text)."""
    emit_tp(30); emit_slot_addr(30, 29, 'KGATE'); emit(LDRX(30, 30)); emit(LDRX(29, 31))

def site_gate(ins, kind, arg):
    """svc / mrs cntpct_el0 → 宿主门。门地址取自线程槽 KGATE；门按返回点（本块 ldp 的地址）查 gates.tsv。 | svc / mrs cntpct_el0 → host gate. The gate address is taken from thread slot KGATE; the gate looks up gates.tsv by return point (the address of this block's ldp)."""
    start = here()
    emit(STP_PRE(29, 30, 31, -16))
    emit_gate_call()
    emit(BLR(30))
    ret = here()
    emit(LDP_POST(29, 30, 31, 16))
    emit(B(here(), ins.address + 4))
    gates.append((ret, kind, arg, ins.address))
    return start

def site_obs(off, oid, word):
    """观测门：入口原指令 → 进宿主门（arg = 观测编号）→ 恢复 x29/x30 → 执行原指令 → 跳回 off+4。
    门块前半与 site_gate 逐字同构；宿主只读寄存器、不改（dm_gate 回写的 x0–x8 即原值）。
    Observation gate: original entry instruction → enter the host gate (arg = observation number) → restore x29/x30 → execute the original instruction → jump back to off+4.
        The first half of the gate block is word-for-word isomorphic with site_gate; the host only reads registers and does not modify them (the x0–x8 written back by dm_gate are the original values)."""
    ins = dis(word, off)
    assert ins.mnemonic not in ('adr', 'adrp', 'b', 'bl', 'br', 'blr', 'ret', 'cbz', 'cbnz', 'tbz', 'tbnz') and not ins.mnemonic.startswith('b.') \
        and not (ins.mnemonic.startswith('ldr') and '[' not in ins.op_str) and not has18(ins), f'观测门原指令不可搬 {off:#x} {ins.mnemonic} {ins.op_str}'
    start = here()
    emit(STP_PRE(29, 30, 31, -16))
    emit_gate_call()
    emit(BLR(30))
    ret = here()
    emit(LDP_POST(29, 30, 31, 16))
    emit(word)
    emit(B(here(), off + 4))
    gates.append((ret, 'obs', oid, off))
    return start

# ---- 独占窗口 ----
# ---- Exclusive windows ----
EXCL_LD = ('ldxr', 'ldaxr', 'ldxp', 'ldaxp', 'ldxrb', 'ldaxrb', 'ldxrh', 'ldaxrh')
EXCL_ST = ('stxr', 'stlxr', 'stxp', 'stlxp', 'stxrb', 'stlxrb', 'stxrh', 'stlxrh')
def find_windows(insl):
    out = []
    for i, ins in enumerate(insl):
        if ins.mnemonic not in EXCL_LD: continue
        j = i
        while j < len(insl) and insl[j].mnemonic not in EXCL_ST: j += 1
        if j >= len(insl) or j - i >= 16: continue
        if j + 1 < len(insl) and insl[j + 1].mnemonic in ('cbz', 'cbnz'): j += 1
        seg = insl[i:j + 1]
        if any(has18(x) for x in seg): out.append(seg)
    return out

def site_window(seg):
    used = set()
    for x in seg: used |= regs_in(x)
    S, T = pick(used)
    # 窗口里的原指令用到 sp 的（本映像没有），保持旧排法。
    # A window whose original instructions use sp (none in this image) keeps the old layout.
    nrz = NOREDZONE and not any(uses_sp(x) for x in seg)
    if NOREDZONE and not nrz: stat('below_sp')
    lo, hi = seg[0].address, seg[-1].address + 4
    start = here()
    stash(S, T, nrz); emit_tp(T); emit_slot_addr(T, S, 'K18'); emit(LDRX(S, T))
    copy0 = here(); n = len(seg)
    exits = copy0 + 4 * n
    ex_targets = []; words = []
    for k, x in enumerate(seg):
        w = struct.unpack('<I', x.bytes)[0]
        if has18(x): w = subst(x, S)
        m = x.mnemonic
        if m in ('cbz', 'cbnz', 'tbz', 'tbnz') or m.startswith('b.'):
            tgt = x.operands[-1].imm
            if lo <= tgt < hi: newt = copy0 + (tgt - lo)
            else: ex_targets.append(tgt); newt = exits + 16 * len(ex_targets)   # 出口桩第 0 个留给顺序落出（见下），窗口外的目标从第 1 个起 | Exit stub 0 is reserved for the sequential fall-out (see below); targets outside the window start from stub 1
            pc = copy0 + 4 * k; off = (newt - pc) >> 2
            if m in ('tbz', 'tbnz'): w = (w & 0xFFF8001F) | ((off & 0x3FFF) << 5)
            else: w = (w & 0xFF00001F) | ((off & 0x7FFFF) << 5)
            dd = dis(w, pc); assert dd.operands[-1].imm == newt and dd.mnemonic == m
        else:
            assert m not in ('adr', 'adrp', 'b', 'bl', 'br', 'blr', 'ret') and not (m == 'ldr' and '[' not in x.op_str), hex(x.address)
        words.append(w)
    for w in words: emit(w)
    # 出口桩的次序：副本最后一条指令执行完是「顺序落出」，落进紧跟在副本后面的第 0 个出口桩——所以第 0 个必须是回 hi 的落出口，窗口外的跳转目标排在其后。
    # r40 及以前落出口排在最后：窗口里只要有跳到窗口外的条件跳转（比较交换的「比较不等」出口），写成功后就落进第一个外部出口 = 当成比较失败处理
    # （一次操作做两遍 / 无锁表被多弹）。暗黑有 6 个这样的窗口（main 0x1848eb8 / 0x1848f28 / 0x1848f84，sdk 0x2ea0fc / 0x2ea4cc / 0x2ea644），
    # 只有跳回窗口内的 cbnz 的窗口不受影响。等价验证原先随机抽不到「比较相等」那一向，所以一直是绿的（equiv.py 已补窗口分支双向强制）。
    # Order of the exit stubs: when the last instruction of the copy has executed, control "falls out sequentially" into exit stub 0, which immediately follows the copy -- so stub 0 must be the fall-out exit back to hi, and jump targets outside the window come after it.
    # Up to r40 the fall-out exit was placed last: whenever a window contained a conditional jump to outside the window (the "compare not equal" exit of a compare-and-swap), a successful write fell into the first external exit = handled as a failed compare
    # (one operation done twice / a lock-free list popped more than once). The game has 6 such windows (main 0x1848eb8 / 0x1848f28 / 0x1848f84, sdk 0x2ea0fc / 0x2ea4cc / 0x2ea644);
    # windows whose only branch is a cbnz back into the window are not affected. The equivalence check used to never draw the "compare equal" direction at random, so it stayed green (equiv.py now forces both directions of window branches).
    for t in [hi] + ex_targets:
        emit(STRX(S, T)); unstash(S, T, nrz); emit(B(here(), t)); emit(NOP)
    return start, lo, hi

# ---- 函数边界（eh_frame_hdr）----
# ---- Function boundaries (eh_frame_hdr) ----
m0 = struct.unpack_from('<I', IMG, 4)[0]
assert IMG[m0:m0 + 4] == b'MOD0'
ehs = m0 + struct.unpack_from('<i', IMG, m0 + 16)[0]
def rd(o, enc):
    fmt, app = enc & 0xF, enc & 0x70
    v, sz = {0x3: (struct.unpack_from('<I', IMG, o)[0], 4), 0xB: (struct.unpack_from('<i', IMG, o)[0], 4),
             0x4: (struct.unpack_from('<Q', IMG, o)[0], 8)}[fmt]
    if app == 0x10: v += o
    if app == 0x30: v += ehs
    return v, sz
_, penc, cenc, tenc = IMG[ehs:ehs + 4]
o = ehs + 4
_, s = rd(o, penc); o += s
nfde, s = rd(o, cenc); o += s
starts = set()
for _ in range(nfde):
    a, s = rd(o, tenc); o += s
    _, s = rd(o, tenc); o += s
    starts.add(a)
starts = sorted(x for x in starts if x < TEXT_END)

# ---- 线性扫描找全部改写点（不靠寄存器读写报告）----
# ---- Linear scan to find all rewrite points (not relying on register read/write reports) ----
kinds = {}
for o in range(0, TEXT_END, 4):
    w = struct.unpack_from('<I', IMG, o)[0]
    if w & 0xFFE0001F == 0xD4000001: kinds[o] = ('svc', (w >> 5) & 0xFFFF); continue
    if w & 0xFFFFFFE0 == 0xD53BD040: kinds[o] = ('tpidr_r', None); continue
    if w & 0xFFFFFFE0 == 0xD51BD040: kinds[o] = ('tpidr_w', None); continue
    if w & 0xFFFFFFE0 == 0xD53BD060: kinds[o] = ('tpidrro_r', None); continue
    if w & 0xFFFFFFE0 == 0xD53BE020: kinds[o] = ('cntpct', w & 31); continue
    if w & 0xFFFFFFE0 == 0xD53BE040: kinds[o] = ('cntvct', w & 31); continue
    # x18/w18 只会占这些寄存器字段；无匹配的字无需反汇编 | x18/w18 can only occupy these register fields; skip words without a match
    # x18/w18 can only occupy these register fields; skip words without a match
    if not any((w >> p) & 31 == 18 for p in (0, 5, 10, 16)):
        continue
    for ins in md.disasm(IMG[o:o + 4], o):
        if has18(ins): kinds[o] = ('x18', None)

# 按函数取指令表，找窗口与跳转表
# Take the instruction table per function, find the windows and jump tables
import bisect
def func_of(a):
    i = bisect.bisect_right(starts, a) - 1
    return (starts[i], starts[i + 1] if i + 1 < len(starts) else TEXT_END) if i >= 0 else (a, a + 4)
done = set(); windows = []; jumps = {}
funcs = sorted({func_of(a) for a, (k, _) in kinds.items() if k == 'x18'})
for fa, fb in funcs:
    insl = list(md.disasm(bytes(IMG[fa:fb]), fa))
    ws = find_windows(insl)
    for wseg in ws:
        windows.append(wseg); done |= {x.address for x in wseg}
    for j, ins in enumerate(insl):
        if ins.mnemonic == 'br' and ins.op_str == 'x18':
            base = width = None
            ctx = insl[max(0, j - 8):j]
            for p in reversed(ctx):
                if p.mnemonic == 'adr' and p.op_str.startswith('x18,'): base = p.operands[1].imm; break
            for p in reversed(ctx):
                if p.mnemonic == 'add' and p.op_str.startswith('x18, x18,') and 'lsl #2' in p.op_str:
                    idx = num(p.reg_name(p.operands[2].reg))
                    for q in reversed(ctx):
                        if q.mnemonic in ('ldrb', 'ldrsb') and num(q.reg_name(q.operands[0].reg)) == idx: width = 256
                        if q.mnemonic in ('ldrh', 'ldrsh') and num(q.reg_name(q.operands[0].reg)) == idx: width = 65536
                        if width: break
                    break
            assert base and width, f'jump table {ins.address:x}'
            jumps[ins.address] = (base, width)
    # 不许有跳转落进窗口中间
    # No jump may land in the middle of a window
    inwin = {x.address for wseg in ws for x in wseg}; heads = {wseg[0].address for wseg in ws}
    for x in insl:
        if x.mnemonic in ('b', 'bl', 'cbz', 'cbnz', 'tbz', 'tbnz') or x.mnemonic.startswith('b.'):
            t = x.operands[-1].imm
            assert not (t in inwin and t not in heads), f'branch into window {x.address:x}'

def put_branch(at, to): struct.pack_into('<I', IMG, at, B(at, to))
for wseg in windows:
    st, lo, hi = site_window(wseg); stat('window'); put_branch(lo, st)
for a in sorted(kinds):
    if a in done: continue
    k, arg = kinds[a]
    ins = dis(struct.unpack_from('<I', IMG, a)[0], a)
    if k == 'x18':
        m = ins.mnemonic
        if m == 'br': st = site_br(ins, *jumps[a]); stat('br')
        elif m in ('cbz', 'cbnz', 'tbz', 'tbnz'): st = site_cond(ins); stat('cond')
        elif m in ('adr', 'adrp'): st = site_pcrel(ins); stat('pcrel')
        elif m in ('blr', 'ret', 'b', 'bl') or (m == 'ldr' and '[' not in ins.op_str): raise RuntimeError(f'{a:x} {m} {ins.op_str}')
        else: st = site_plain(ins); stat('plain')
    elif k == 'tpidr_r': st = site_sysreg_read(ins, 'KTLS'); stat(k)
    elif k == 'tpidrro_r': st = site_sysreg_read(ins, 'KTLR'); stat(k)
    elif k == 'tpidr_w': st = site_tpidr_write(ins); stat(k)
    elif k == 'svc': st = site_gate(ins, 'svc', arg); stat(k)
    elif k in ('cntpct', 'cntvct'): st = site_gate(ins, k, arg); stat(k)
    put_branch(a, st)

# 私有视觉分支；非OBS，0x85只读查询。只有本实验构建显式opt-in；原Qty/children和Update不变。
# Private visual branch; not OBS, 0x85 is a read-only query. Only this experimental build explicitly opts in; the original Qty/children and Update are unchanged.
if RULE_RUN and PRIVATE_GATES:
    off, word = 0x3e443c, 0x3969c268
    assert struct.unpack_from('<I', IMG, off)[0] == word
    assert off < TEXT_END and off not in kinds and not any(seg[0].address <= off < seg[-1].address + 4 for seg in windows)
    start = here()
    emit(STP_PRE(8, 31, 31, -16))  # w8查询结果仅用于cbz；两路恢复原x8。 | The w8 query result is used only for cbz; both paths restore the original x8.
    emit(STP_PRE(29, 30, 31, -16))
    emit_gate_call(); emit(BLR(30))
    ret = here(); emit(LDP_POST(29, 30, 31, 16))
    # cbz w8, nonmatch；不改NZCV。match恢复寄存器后保children。
    # cbz w8, nonmatch; NZCV is not changed. On match, restore the registers and then preserve children.
    emit(0x34000068)
    emit(LDP_POST(8, 31, 31, 16)); emit(B(here(), 0x3e4460))
    emit(LDP_POST(8, 31, 31, 16)); emit(word); emit(B(here(), 0x3e4440))
    gates.append((ret, 'svc', 0x85, off))
    put_branch(off, start); stat('private_skill_render')
    cuts.append((off, struct.pack('<I', word).hex(), struct.pack('<I', B(off,start)).hex(),
                 'private confirmed six HUD only: own image/text skip ->4460; unknown original ->4440; svc85 full800', '仅宿主'))
elif RULE_RUN:
    patch.extend(bytes(0x80))
    stat('private_gates_reserved')

obs = list(OBS.get(BUILD_ID, []))
if RULE_RUN and PRIVATE_GATES:
    obs.append((0x43ecc8, 6, "b8686953", "private touch-run-v4 PRE-ldr; full800 host ABI"))
    obs.append((0x1237c60, 8, "d10143ff", "ui world anchor: camera world->screen entry (camera, point, out); host swaps x0 to a copy whose projection is shifted by the UI rect y (iPad UI kept 16:9 inside a taller screen), game camera untouched"))
    # 面板开着时冻结世界（宿主 diag.c freeze_tick，缺省关，DM_PANEL_FREEZE=1 开）。四个入口门，都只换入口参数。
    # Freeze the world while a panel is open (host diag.c freeze_tick, off by default, DM_PANEL_FREEZE=1 turns it on). Four entry gates, all of which only swap entry arguments.
    obs.append((0x5eb2c0, 10, "a9bc5ff8", "server world-sim entry (game G): while frozen the host swaps x0 to a stand-in whose event queue is empty, so no unit / timer callback runs this tick; the real queue is left untouched"))
    obs.append((0x4b7230, 11, "d10183ff", "client per-unit tick update entry (unit): while frozen the host swaps x0 to a stand-in unit carrying only the skip flag ([+0x124] bit 23), so the function takes its own early-out to the epilogue; all six call sites are inside the client tick simulation 0x461b80 / 0x461e60"))
    obs.append((0x4b74d0, 12, "f81e0ff3", "client per-unit per-frame update entry (unit): drives model animation / interpolation from the tick fraction; while frozen the host swaps x0 to a stand-in unit (type 9, no render component) so both the callee 0x2a18d0 and this function take their own early-outs"))
    obs.append((0x2c3c90, 13, "f81c0ff7", "render per-frame entry, called right after the main loop writes the world frame step [0x20cebb8] and world time [0x3114208]: while frozen the host rewrites the step to 0 and the world time to its value at freeze, the same thing the game's own pause path does at 0x2c1574"))
    obs.append((0xadc434, 14, "d10143ff", "TextBoxWidget text-changed entry (widget): reads the raw text at +0x88 and rebuilds the display; host rewrites +0x88 for strings our item-notes generator annotated with colour codes (TextBoxWidget draws U+E07E as '*'), exact-match table only, everything else untouched"))
    obs.append((0x442bc0, 15, "a9ba6ffc", "client event-message entry (x0 = packet; byte 1 = event subtype, jump table 0x1da6752): subtype 0x17 = offline difficulty scale changed -> 0x20f91c chat line + system notification; host rewrites byte 1 past the table (default branch, nothing shown) only for 0x17 and only in a rift game (dm_rift.c); otherwise untouched. orig stp x28, x27, [sp, #-0x60]!"))
    obs.append((0x634000, 16, "d10143ff", "server level change by level number (game, player, level, kind), used by waypoints / portals / quest moves: in a rift game, when the player leaves a rift map for a level that is neither town nor a rift map, the host swaps w2 to the rift's next map so the destination is replaced inside the single native transition"))
    obs.append((0x711040, 17, "d101c3ff", "server quest event 'monster killed' entry (game, monster, attacker), the only entry (callers 0x7d8550 kill, 0x7ab8dc); in a rift game, for a monster that died on a rift map, the host swaps x1 to a stand-in whose +0xc0 is null so the function takes its own early return (0x711064): bosses in the rift are combat only, no quest state / quest notifications"))
    obs.append((0x3d84a0, 18, "d10143ff", "NotificationPanel message handler entry (panel, message): on InputMessage / ControllerActionBegin with action id 9 (the menu key) the native handler re-sends the notice callback and marks the message bus consumed, so the first menu press only dismisses the notice. In touch mode the host swaps x1 to a copy of the message whose action id is not 9: the notice stays until it times out and the menu key opens the menu; controller mode keeps the native behaviour"))
    obs.append((0x463f40, 19, "f81c0ff7", "client per-frame position-error smoothing entry (s0 = frame length in ticks), called by the main loop right after all units were extrapolated in place and before the per-unit error is added back: the native error is only computed on start / stop (0x4639c0, which runs before the tick loop), so a sharp turn inside a tick snaps the extrapolated position back in one frame; the host adds that frame's jump to the local player's error fields (path +0x2a8 / +0x2ac, same clamp as native) and the native code decays it"))
    obs.append((0x9da5b0, 20, "d104c3ff", "model yaw component per-frame update entry (x1 = argument block: +8 component, +0x20 -> +8 unit): the body turn speed is instance data on the component (+0x10..+0x2c min / max degrees per second for controller / mouse, +0x34 spring parameter); the host only reads them for the local player when a test switch is set"))
    obs.append((0x2f7664, 7, "6dbb23e9", "touch target rank: candidate compare entry (closure, unit, kind, s0=score); host raises Boss/elite score, native eligibility unchanged"))
    obs.append((0xb0d560, 21, "fc1a0fe8", "TooltipsPanel add-tooltip entry (panel, text, target position x2 / x3, style, text2): tooltips are immediate-mode (re-submitted by the hovered widget every frame and cleared after drawing, so the host cannot reach the record at its own time); on touch, in panel state, for an item the player just tapped, the host swaps the target position so the game lays the tooltip out beside the panel instead of over the item grid; everything else passes through unchanged"))
    obs.append((0x7ce800, 22, "d100c3ff", "server place-one-preset-unit entry (game, room, record), only called by the room-activation preset loop 0x7ce930 (8 sites): in a rift game at tier 90+ on a rift map, for a monster record that is not a superunique and not scenery / NPC, the host swaps x2 to a copy whose class is -1 so 0x6910d0 takes its own early return (tbnz w2, #31) and nothing is spawned; the real record is untouched; rift guardian / invader / superunique spawns call 0x7ce630 / 0x69b310 directly and never pass here"))
for off, oid, ow, why in obs:
    w = struct.unpack_from('<I', IMG, off)[0]
    assert f'{w:08x}' == ow, f'观测门原指令不符 {off:#x}：{w:08x}'
    # 只许函数入口：dm_gate 只回写 x0–x8 / x30，宿主 C 代码可能改 x9–x17；入口处这些按 AAPCS64 不承载值（参数只在 x0–x7 / x8）
    # Function entries only: dm_gate writes back only x0–x8 / x30, and host C code may modify x9–x17; at entry these carry no values under AAPCS64 (arguments are only in x0–x7 / x8)
    assert off in set(starts) or (oid == 6 and off == 0x43ecc8), f'private obs point not approved {off:#x}'
    assert off < TEXT_END and off not in kinds and not any(lo <= off < hi for wseg in windows for lo, hi in [(wseg[0].address, wseg[-1].address + 4)]), f'观测门与改写点冲突 {off:#x}'
    put_branch(off, site_obs(off, oid, w)); stat('obs')

# 秘境出口改道（r36 起）：分支门 0x89，挂在「按出口砖的连接换关」main+0x7cc1e0(游戏, 玩家, 砖) 的入口。它的四个调用者（点出口砖 0x6dbea0、
# 入口物件 0x6aeaa0 / 0x6b0500、任务 0x7314c0）都在各自的判定通过之后才调它；目的地不是关卡号而是砖的对端房间（同一幕已生成的地图里），换不成别的幕。
# 宿主在秘境局里把 w2 / w3 换成（秘境下一张图, 11）并让 x8≠0 → 门块直接跳去「按关卡号换关」0x634000，同一个调用者、只发生一次换关；其余情形执行原指令回 0x7cc1e4。
# 门块与 0x85 分支门逐字同构（structck ⑪ 按 hle.h 登记表核）；与观测门 15 / 16 一样只在带宿主私有门的产物里（touch）
# Rift exit rerouting (since r36): branch gate 0x89, hooked at the entry of "change level by the exit tile's connection" main+0x7cc1e0(game, player, tile). Its four callers (clicking an exit tile 0x6dbea0,
# entrance objects 0x6aeaa0 / 0x6b0500, quest 0x7314c0) all call it only after their own checks have passed; the destination is not a level id but the tile's peer room (in the already generated map of the same act), so it cannot be switched to another act.
# In a rift run the host replaces w2 / w3 with (the rift's next map, 11) and makes x8≠0 → the gate block jumps straight to "change level by level id" 0x634000, same caller, exactly one level change; in all other cases the original instruction is executed and control returns to 0x7cc1e4.
# The gate block is word-for-word isomorphic with the 0x85 branch gate (structck ⑪ checks it against the hle.h registry); like observation gates 15 / 16 it exists only in build outputs with host-private gates (touch)
if RULE_RUN and PRIVATE_GATES:
    off, word, tgt = 0x7cc1e0, 0xd10183ff, 0x634000
    assert struct.unpack_from('<I', IMG, off)[0] == word and off in set(starts) and tgt in set(starts)
    assert off < TEXT_END and off not in kinds and not any(seg[0].address <= off < seg[-1].address + 4 for seg in windows)
    start = here()
    emit(STP_PRE(8, 31, 31, -16))
    emit(STP_PRE(29, 30, 31, -16))
    emit_gate_call(); emit(BLR(30))
    ret = here(); emit(LDP_POST(29, 30, 31, 16))
    emit(0x34000068)
    emit(LDP_POST(8, 31, 31, 16)); emit(B(here(), tgt))
    emit(LDP_POST(8, 31, 31, 16)); emit(word); emit(B(here(), off + 4))
    gates.append((ret, 'svc', 0x89, off))
    put_branch(off, start); stat('rift_exit')
    cuts.append((off, struct.pack('<I', word).hex(), struct.pack('<I', B(off, start)).hex(),
                 'rift exit redirect: tile-link level change entry -> host swaps (level, kind) and x8!=0 takes the branch to level-number change 0x634000; otherwise original ->0x7cc1e4; svc89', '仅宿主'))

# 秘境里存档时的当前幕（r38 起）：分支门 0x8c，挂在原版写存档头函数 0x6fa3e0 里「取难度」那条 main+0x6fa9d0（ldrb w10, [x21, #0x104]）。
# 它的上一条 0x6fa9cc 刚把「玩家当前在第几幕」读进 w9（ldrb w9, [x24, #0x2e8]），下面 0x6fa9e4 / 0x6fa9e8 写成难度字节 0x80 | 幕号（.d2s 0x98–0x9a），
# 下次进局落哪个城由它定。宿主在人处于秘境图里时把 w9 换成 4（第五幕 → 重进落哈洛加斯），只动写进存档的这一个值，运行中的幕号不碰。
# 用分支门而不是「替换一条指令」：structck 只认「bl 调用点」「入口」「分支门」三种登记形态。宿主永远让 x8 = 0（不走跳转那一路），
# 门块恢复 x8、执行原指令、回 0x6fa9d4；跳转目标填回点本身只为满足模板（走不到）。门块与 0x85 / 0x89 逐字同构
# Current act written to the save while in a Rift (since r38): branch gate 0x8c, hooked at the "read difficulty" instruction main+0x6fa9d0 (ldrb w10,
# [x21, #0x104]) inside the original save-header writer 0x6fa3e0.
# The preceding instruction 0x6fa9cc has just loaded "which act the player is currently in" into w9 (ldrb w9, [x24, #0x2e8]); below, 0x6fa9e4 / 0x6fa9e8
# write it as the difficulty byte 0x80 | act number (.d2s 0x98–0x9a),
# which decides which town the next session starts in. While the player is in a Rift map the host replaces w9 with 4 (Act 5 → re-enter in Harrogath);
# only this one value written to the save is changed, the running act number is untouched.
# A branch gate is used rather than "replace one instruction": structck only recognizes three registered forms, "bl call site", "entry" and "branch
# gate". The host always sets x8 = 0 (the jump path is never taken);
# the gate block restores x8, executes the original instruction and returns to 0x6fa9d4; the jump target is set to the return point itself only to
# satisfy the template (never reached). The gate block is word-for-word identical in structure to 0x85 / 0x89
if RULE_RUN and PRIVATE_GATES:
    off, word, tgt = 0x6fa9d0, 0x394412aa, 0x6fa9d4   # 函数中间的一条指令（同 0x85 那扇门），不要求是函数入口；原指令不依赖 pc | An instruction in the middle of a function (same as gate 0x85); it need not be a function entry; the original instruction does not depend on pc
    assert struct.unpack_from('<I', IMG, off)[0] == word
    assert off < TEXT_END and off not in kinds and not any(seg[0].address <= off < seg[-1].address + 4 for seg in windows)
    start = here()
    emit(STP_PRE(8, 31, 31, -16))
    emit(STP_PRE(29, 30, 31, -16))
    emit_gate_call(); emit(BLR(30))
    ret = here(); emit(LDP_POST(29, 30, 31, 16))
    emit(0x34000068)
    emit(LDP_POST(8, 31, 31, 16)); emit(B(here(), tgt))
    emit(LDP_POST(8, 31, 31, 16)); emit(word); emit(B(here(), off + 4))
    gates.append((ret, 'svc', 0x8c, off))
    put_branch(off, start); stat('rift_save_act')
    cuts.append((off, struct.pack('<I', word).hex(), struct.pack('<I', B(off, start)).hex(),
                 'rift save act: save-header writer, right after it loads the current act into w9 -> host swaps w9 to 4 (act V) while the player is in a rift map; x8 stays 0, original insn runs ->0x6fa9d4; svc8c', '仅宿主'))
# 佣兵加装备位（r39 起）：分支门 0x8d，挂在原版通用放置校验 main+0x6493a8 的怪物主人分支里 main+0x64a34c（mov w9, w25）。
# 这一段（0x64a314–0x64a354）对怪物类主人只放行两类物品：0x574bc0(物品, 3 护甲)、0x574bc0(物品, 0x25 头盔)，都不是就走武器那条（按 monstats 的左右手类型），再不中就拒。
# 门挂在「是不是头盔」返回之后、cbnz w0 之前：宿主在「主人是玩家的佣兵 + 秘境最高通关 ≥ 10 层 + 物品是腰带 / 鞋 / 手套 / 戒指 / 项链」时把 w0 改成 1，
# 走头盔同一条放行路径（后面的部位格子、部位空不空等通用检查照常）。宿主永远让 x8 = 0；门块与 0x85 / 0x89 / 0x8c 逐字同构
# Extra mercenary equipment slots (since r39): branch gate 0x8d, hooked in the monster-owner branch of the original game's generic placement check main+0x6493a8 at main+0x64a34c (mov w9, w25).
# This section (0x64a314–0x64a354) lets through only two kinds of items for monster-type owners: 0x574bc0(item, 3 armor) and 0x574bc0(item, 0x25 helm); if neither, it takes the weapon path (by the left / right hand types in monstats), and if that fails too it rejects.
# The gate is hooked after the "is it a helm" call returns and before cbnz w0: when "the owner is the player's mercenary + highest rift level cleared ≥ 10 + the item is a belt / boots / gloves / ring / amulet", the host changes w0 to 1,
# taking the same pass path as the helm (the generic checks that follow, such as the body location cell and whether the location is empty, run as usual). The host always sets x8 = 0; the gate block is word-for-word isomorphic with 0x85 / 0x89 / 0x8c
if RULE_RUN and PRIVATE_GATES:
    off, word, tgt = 0x64a34c, 0x2a1903e9, 0x64a350   # 原指令不依赖 pc | The original instruction does not depend on pc
    assert struct.unpack_from('<I', IMG, off)[0] == word
    assert off < TEXT_END and off not in kinds and not any(seg[0].address <= off < seg[-1].address + 4 for seg in windows)
    start = here()
    emit(STP_PRE(8, 31, 31, -16))
    emit(STP_PRE(29, 30, 31, -16))
    emit_gate_call(); emit(BLR(30))
    ret = here(); emit(LDP_POST(29, 30, 31, 16))
    emit(0x34000068)
    emit(LDP_POST(8, 31, 31, 16)); emit(B(here(), tgt))
    emit(LDP_POST(8, 31, 31, 16)); emit(word); emit(B(here(), off + 4))
    gates.append((ret, 'svc', 0x8d, off))
    put_branch(off, start); stat('merc_slot')
    cuts.append((off, struct.pack('<I', word).hex(), struct.pack('<I', B(off, start)).hex(),
                 'merc slots: placement check, monster-owner branch right after the helm type test -> host sets w0=1 for ring/amulet/boots/gloves/belt on a hireling (rift best >= 10 for new equips; always on load); x8 stays 0, original insn runs ->0x64a350; svc8d', '仅宿主'))
# 佣兵加装备位的客户端那一半（r39 同代）：分支门 0x8e，挂在客户端「佣兵能不能穿这件」函数 main+0x3bea44(佣兵, 物品) 里 main+0x3beb58（mov x0, x19）。
# 这个函数和服务端那段同一个规则：0x574bc0(物品, 3 护甲)、0x574bc0(物品, 0x25 头盔) 中一个就返回 1，都不是走武器那条。物品格控件拿它的结果画「不可放置」的红色、
# 决定松手时放不放——不改它，已解锁的新格拖着对的物品悬停也是红的、原版放下不成（原先靠触摸层兜底直接调 0x3bed60）。
# 门挂在「mov w1, #0x25」之后、这次类型判断的 bl 之前：宿主在「秘境最高通关 ≥ 10 层 + 物品是腰带 / 鞋 / 手套 / 戒指 / 项链」时把 w1 换成该物品自己的类型号，
# 类型判断自然成立、走头盔同一条返回 1 的路。宿主永远让 x8 = 0；门块与 0x8d 逐字同构
# Client half of the extra mercenary equipment slots (same generation as r39): branch gate 0x8e, hooked inside the client's "can the mercenary wear this" function main+0x3bea44(mercenary, item) at main+0x3beb58 (mov x0, x19).
# This function follows the same rule as the server-side section: if either 0x574bc0(item, 3 armor) or 0x574bc0(item, 0x25 helm) matches it returns 1, and if neither does it takes the weapon path. The item cell widget uses its result to draw the "cannot place" red and
# to decide whether to place on release -- without changing it, an unlocked new cell still shows red when the right item is dragged over it, and the original game's drop fails (previously the touch layer's fallback called 0x3bed60 directly).
# The gate is hooked after "mov w1, #0x25" and before the bl of this type check: when "highest rift level cleared ≥ 10 + the item is a belt / boots / gloves / ring / amulet", the host replaces w1 with the item's own type number,
# so the type check passes naturally and takes the same return-1 path as the helm. The host always sets x8 = 0; the gate block is word-for-word isomorphic with 0x8d
if RULE_RUN and PRIVATE_GATES:
    off, word, tgt = 0x3beb58, 0xaa1303e0, 0x3beb5c   # 原指令不依赖 pc | The original instruction does not depend on pc
    assert struct.unpack_from('<I', IMG, off)[0] == word
    assert off < TEXT_END and off not in kinds and not any(seg[0].address <= off < seg[-1].address + 4 for seg in windows)
    start = here()
    emit(STP_PRE(8, 31, 31, -16))
    emit(STP_PRE(29, 30, 31, -16))
    emit_gate_call(); emit(BLR(30))
    ret = here(); emit(LDP_POST(29, 30, 31, 16))
    emit(0x34000068)
    emit(LDP_POST(8, 31, 31, 16)); emit(B(here(), tgt))
    emit(LDP_POST(8, 31, 31, 16)); emit(word); emit(B(here(), off + 4))
    gates.append((ret, 'svc', 0x8e, off))
    put_branch(off, start); stat('merc_slot_client')
    cuts.append((off, struct.pack('<I', word).hex(), struct.pack('<I', B(off, start)).hex(),
                 'merc slots (client): hireling can-equip check, between "mov w1,#0x25" and the type test -> host swaps w1 to the item\'s own type for ring/amulet/boots/gloves/belt (rift best >= 10); x8 stays 0, original insn runs ->0x3beb5c; svc8e', '仅宿主'))

# ---- 参数位（仅宿主）：{NSO 构建号: [(偏移 A, 原字 A, 偏移 B, 原字 B, 寄存器, 键, 理由)]}。
# 原处两条造常数指令改成第一条跳补丁区参数跳板、第二条 nop；跳板间接取键页值后跳回第一条之后（A+4）：A、B 不相邻时中间的原指令照常执行
# （曾跳回 B+4，运行模式通知处理 0xe10d5c/0xe10d60 越过 adrp x19.. 与另一跳板，切掌机即崩；structck ⑩ 核跳板出口）。
# A、B 之间不得读写 xR（离线断言：中间指令不含 xR）。宿主按前端给值，Mac 给原常数 → 行为不变 ----
# ---- Parameter slots (host only): {NSO build ID: [(offset A, original word A, offset B, original word B, register, key, reason)]}.
# The two constant-building instructions at the original site are changed so that the first jumps to a parameter trampoline in the patch area and the second is a nop; the trampoline fetches the key page value indirectly and jumps back to just after the first (A+4): when A and B are not adjacent, the original instructions in between execute as usual
# (it used to jump back to B+4; in the operation-mode notification handler 0xe10d5c/0xe10d60 that skipped adrp x19.. and another trampoline, and switching to handheld crashed immediately; structck ⑩ checks the trampoline exit).
# xR must not be read or written between A and B (offline assertion: the instructions in between do not contain xR). The host supplies values according to the front end, Mac supplies the original constants → behavior unchanged ----
PARAMS = GAME_PATCHES.PARAMS
params = PARAMS.get(BUILD_ID, [])
for oa, wa, ob_, wb, r, key, why in params:
    for o, w0 in ((oa, wa), (ob_, wb)):
        w = struct.unpack_from('<I', IMG, o)[0]
        assert f'{w:08x}' == w0, f'参数位原指令不符 {o:#x}：{w:08x}'
        assert o not in kinds and not any(lo <= o < hi for wseg in windows for lo, hi in [(wseg[0].address, wseg[-1].address + 4)]), f'参数位与改写点冲突 {o:#x}'
    for o in range(oa + 4, ob_, 4):
        ins = dis(struct.unpack_from('<I', IMG, o)[0], o)
        assert r not in regs_in(ins), f'参数位中间用到 x{r}：{o:#x} {ins.mnemonic} {ins.op_str}'
    start = here()
    tmp = 16 if r != 16 else 17
    emit(STR_PRE(tmp, 31, -16) if NOREDZONE else STUR(tmp, 31, -16)); emit_tp(r); emit_slot_addr(r, tmp, key); emit(LDRX(r, r)); emit(LDR_POST(tmp, 31, 16) if NOREDZONE else LDUR(tmp, 31, -16)); emit(B(here(), oa + 4))
    struct.pack_into('<I', IMG, oa, B(oa, start)); struct.pack_into('<I', IMG, ob_, NOP); stat('param')

# 既有三块原生规则跳板均只使用后继覆盖的死临时w8，不改w0/stat返回、sp或NZCV。
# 6c6290位于原mode3门后；前方Blaze/引用计数与数据失效6c62dc分支、后方状态更新不动。
# The three existing native rule trampolines all use only the dead temporary w8 that is overwritten afterwards, and do not change the w0/stat return, sp or NZCV.
# 6c6290 is after the original mode3 gate; the Blaze / reference count and data invalidation 6c62dc branches before it and the state update after it are untouched.
rule_blocks = []
if RULE_RUN:
    def run_branch(off, old, body, why):
        assert struct.unpack_from('<I', IMG, off)[0] == old, f'Run跳板原字不符 {off:#x}'
        start = here()
        for word in body(start): emit(word)
        new = B(off, start)
        struct.pack_into('<I', IMG, off, new)
        cuts.append((off, struct.pack('<I', old).hex(), struct.pack('<I', new).hex(), why, '全'))
        rule_blocks.append((start, bytes(patch[start - PATCH_OFF:]).hex(), why))
    def cbz(at, target, r, nonzero=False):
        delta = target-at; assert delta%4==0 and -(1<<20)<=delta<(1<<20)
        return (0x35000000 if nonzero else 0x34000000) | (((delta>>2)&0x7ffff)<<5) | r
    run_branch(0x6c6648, 0x34000060, lambda a: [
        0xb9400268, cbz(a+4,a+12,8,True), B(a+8,0x6c664c),
        cbz(a+12,a+20,0), B(a+16,0x6c664c), B(a+20,0x6c6654)],
        'touch-run-v4：Run选择仅player0忽略精力门，非player按原stat返回；w8后继覆盖')
    run_branch(0x6c6290, 0x710006df, lambda a: [
        0xb9400268, cbz(a+4,a+12,8,True), B(a+8,0x6c62ec), 0x710006df, B(a+16,0x6c6294)],
        'touch-run-v4：原合法Run成本段仅player0跳stat10扣除/耗尽降走；非player保持原cmp')


    # client4abc60原mode3/当前受控unit门之后；实际GetStat已返回，保W0，只type0取消STA自动Stop。
    # After the original mode3 / currently controlled unit gate at client4abc60; the actual GetStat has already returned, W0 is preserved, and only for type0 is the STA auto Stop cancelled.
    run_branch(0x4ac368, 0x35000460, lambda a: [
        0xb9400268, cbz(a+4,a+12,8), B(a+8,a+16), B(a+12,0x4ac3f4),
        cbz(a+16,a+24,0), B(a+20,0x4ac3f4), B(a+24,0x4ac36c)],
        'touch-run-v4：client当前受控Run3仅player0跳STA零值自动Stop；原getter值与真实松杆保留，非player保原门')


    # 原unit null/type5门之后、w8仍为原unitType；只规范player请求2，原其它动作分派/资格完整执行。
    # After the original unit null/type5 gate, with w8 still the original unitType; only player request 2 is normalized, and the other original action dispatch / eligibility checks run in full.
    run_branch(0x6c6580, 0x51000849, lambda a: [
        cbz(a,a+16,8,True), 0x51000849, cbz(a+8,a+16,9,True),
        0x52800062, 0x51000849, B(a+20,0x6c6584)],
        'touch-run-v4：原通用合法移动仅player0请求Walk2规范为Run3，非player与其它请求保原，w8/原资格不改')

os.makedirs(outdir, exist_ok=True)
P = lambda ext: os.path.join(outdir, f'{NAME}.{ext}')
open(P('patched.bin'), 'wb').write(IMG + bytearray(PATCH_OFF - len(IMG)) + patch)
import hashlib
with open(os.path.join(outdir, 'build_src.sha256'), 'w') as f:
    f.write(GAME_PROFILE.rewrite_digest() + '  rewrite_inputs\n')
if PRIVATE_AIM:
    with open(P('aim.tsv'), 'w') as f:
        f.write('# RULESET\tskill-aim-v1-private\n# ORIGINAL_CALL\t0x2faf04\t97fee7fd\n# ORIGINAL_CALLEE\t0x2b4ef8\n# SKILL_CALLER\t0x2b5b80\n# SVC\t0x86\n# INPUT\tactual GetNpad sampled right; move axis unchanged\n')
        f.write('# IMAGE_SHA256\t'+hashlib.sha256(open(P('patched.bin'),'rb').read()).hexdigest()+'\n')
if RULE_RUN:
    with open(P('rules.tsv'), 'w') as f:
        f.write(f'# RULESET\t{RULESET}\n# BUILD_ID\t{BUILD_ID}\n# ORIGINAL_SHA256\t{RULE_ORIG_SHA}\n# IMAGE_SHA256\t{hashlib.sha256(open(P("patched.bin"), "rb").read()).hexdigest()}\n')
        for off, raw, why in rule_blocks: f.write(f'{off:#x}\t{raw}\t{hashlib.sha256(bytes.fromhex(raw)).hexdigest()}\t{why}\n')
elif os.path.exists(P('rules.tsv')):
    os.unlink(P('rules.tsv'))  # 重出native时清本模块旧规则登记，不让旧manifest混入原资源域 | When rebuilding native, clear this module's old rule registration so the old manifest does not leak into the original-resource domain
with open(P('fixups.tsv'), 'w') as f:
    f.write(f'# TARGET\t{ {"apple": 0, "android": 1, "ohos": 2}[TARGET]:#x}\n')   # 0 = 苹果，1 = 安卓，2 = 鸿蒙；装载器核对与自己相符 | 0 = Apple, 1 = Android, 2 = HarmonyOS; the loader checks it matches itself
    f.write(f'# PATCH_OFF\t{PATCH_OFF:#x}\n# KEY_PAGE_OFF\t{KEY_PAGE_OFF:#x}\n# KEY_PAGE_SIZE\t0x4000\n# TEXT_END\t{TEXT_END:#x}\n# RO_OFF\t{RO_OFF:#x}\n# DATA_OFF\t{DATA_OFF:#x}\n')
    for k in KEY_INDEX: f.write(f'# KEY_{k}_IDX\t{KEY_INDEX[k]}\n')
with open(P('gates.tsv'), 'w') as f:
    for ret, k, arg, orig in gates: f.write(f'{ret:#x}\t{k}\t{arg}\t{orig:#x}\n')
with open(P('sites.tsv'), 'w') as f:
    for a in sorted(kinds):
        if a not in done: f.write(f'{a:#x}\t{kinds[a][0]}\n')
    for wseg in windows: f.write(f'{wseg[0].address:#x}\tW\t{wseg[-1].address + 4:#x}\n')
    for a, (b, c) in jumps.items(): f.write(f'{a:#x}\tJ\t{b:#x}\t{c}\n')
with open(P('cuts.tsv'), 'w') as f:
    f.write(f'# BUILD_ID\t{BUILD_ID}\n')
    for off, ob, nb, why, scope in cuts: f.write(f'{off:#x}\t{ob}\t{nb}\t{scope}\t{why}\n')   # 列：偏移 原字节 新字节 作用域 理由 | Columns: offset, original bytes, new bytes, scope, reason
with open(P('params.tsv'), 'w') as f:
    f.write(f'# BUILD_ID\t{BUILD_ID}\n')
    for oa, wa, ob_, wb, r, key, why in params: f.write(f'{oa:#x}\t{ob_:#x}\tx{r}\t{key}\t{why}\n')
with open(P('obs.tsv'), 'w') as f:
    f.write(f'# BUILD_ID\t{BUILD_ID}\n')
    for off, oid, ow, why in obs: f.write(f'{off:#x}\t{oid}\t{ow}\t{why}\n')
print(NAME, stats, 'cuts', len(cuts), 'obs', len(obs), 'patch', len(patch), 'PATCH_OFF', hex(PATCH_OFF), 'slot_refs', len(fix), 'gates', len(gates))
# 启动块清单随 main 出：取仓库版本化数据 dm/data/bootlist_<main 构建号>.tsv 原样拷进产物目录并写 sha256（清单入库、不每版重录；
# 记录本身有竞态——同产物三次记录块集合 5292 / 5114 / 5281、发起线程亦有不同——重录只在构建号变化或主动改进时做，提交附 mini A/B）
# The boot block list ships with main: take the repository's versioned data dm/data/bootlist_<main build ID>.tsv, copy it as is into the build output directory and write its sha256 (the list is checked in, not re-recorded for every version;
# the recording itself is racy -- three recordings of the same build output gave block sets of 5292 / 5114 / 5281, and the initiating threads also differed -- re-record only when the build ID changes or for a deliberate improvement, and attach an A/B from the second Mac to the commit)
if NAME == 'main':
    import hashlib, shutil
    BL = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data', f'bootlist_{BUILD_ID}.tsv')
    if not os.path.isfile(BL):   # 公开仓库不带这份清单（它每行带原版资源块的开头字节）：没有就不预取，启动时少一步提前解码，别的不受影响 | the public repository does not ship this list (each line carries the first bytes of an original resource block): without it there is no prefetch, so launch skips one ahead-of-time decode step and nothing else is affected
        print('bootlist 没有启动块清单，跳过（不预取）。要用的话自己录一份：dm/tools/tact_bootlist.py')
        for n in ('bootlist.tsv', 'bootlist.sha256'):   # 产物目录里以前留下的也去掉：留着会被当成这一次的产物带进安装包和壳的清单 | Leftovers from earlier runs in the build output directory are removed too: if kept they would be taken as outputs of this run and carried into the install package and the shell manifest
            if os.path.exists(os.path.join(outdir, n)): os.remove(os.path.join(outdir, n))
    else:
        hd = open(BL, encoding='utf-8').readline().split()
        if hd[-1:] != [BUILD_ID]: sys.exit(f'【红】{BL} 头行构建号 {hd[-1:]} 与 main {BUILD_ID} 不符')
        shutil.copyfile(BL, os.path.join(outdir, 'bootlist.tsv'))
        sha = hashlib.sha256(open(BL, 'rb').read()).hexdigest()
        open(os.path.join(outdir, 'bootlist.sha256'), 'w').write(f'{sha}  bootlist.tsv\n')
        print('bootlist', os.path.relpath(BL), sha[:12])
