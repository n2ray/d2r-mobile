from pathlib import Path
import ast
import re
import struct
import sys
from itertools import combinations

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn import arm64_const as arm


def emitter(source: Path, target: str):
    tree = ast.parse(source.read_text())
    nodes = [node for node in tree.body if isinstance(node, ast.FunctionDef)]
    nodes += [node for node in tree.body if isinstance(node, ast.Assign)
              and any(isinstance(t, ast.Name) and t.id in {"SPOP", "NOP", "NOREDZONE"} for t in node.targets)]
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = True
    values = dict(re=re, struct=struct, combinations=combinations, TARGET=target,
                  PATCH_OFF=0x20000, KEY_PAGE_OFF=0x40000, patch=bytearray(), fix=[],
                  stats={}, gates=[], md=md, KEY_INDEX={"K18": 0, "KTLS": 1, "KTLR": 2, "KGATE": 3})
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(source), "exec"), values)
    return values


REGS = [getattr(arm, f"UC_ARM64_REG_X{i}") for i in range(29)] + [arm.UC_ARM64_REG_X29, arm.UC_ARM64_REG_X30]


def execute(words: bytes, patched: bool, signal_clobber: bool, x18: int, end: int = 0x10004):
    cpu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    cpu.mem_map(0x10000, 0x90000)
    initial = bytes(range(256)) * 8
    cpu.mem_write(0x8fc00, initial)
    for i, reg in enumerate(REGS):
        cpu.reg_write(reg, x18 if i == 18 else 0x70000 + i * 16)
    cpu.reg_write(arm.UC_ARM64_REG_SP, 0x90000)
    cpu.reg_write(arm.UC_ARM64_REG_NZCV, 0x60000000)
    cpu.reg_write(arm.UC_ARM64_REG_TPIDR_EL0, 0x71000)
    cpu.reg_write(arm.UC_ARM64_REG_TPIDRRO_EL0, 0x72000)
    cpu.mem_write(0x40000, struct.pack("<4Q", 0, 8, 16, 24))
    cpu.mem_write(0x40040, struct.pack("<Q", 0x60000))
    cpu.mem_write(0x50000, struct.pack("<3Q", x18, 0x71000, 0x72000))
    start = 0x20000 if patched else 0x10000
    cpu.mem_write(start, words)
    stopped = []
    writes = []

    def wrote(uc, access, address, size, value, data):
        writes.append((address, size))

    def step(uc, address, size, data):
        if address == 0x60000:
            uc.reg_write(arm.UC_ARM64_REG_X16, 0x50000)
            uc.reg_write(arm.UC_ARM64_REG_PC, uc.reg_read(arm.UC_ARM64_REG_X30))
            return
        if address == end or address in (0x10100, 0x10104, 0x10108):
            stopped.append(address)
            uc.emu_stop()
            return
        if patched and signal_clobber:
            sp = uc.reg_read(arm.UC_ARM64_REG_SP)
            uc.mem_write(sp - 128, b"\xa5" * 128)

    cpu.hook_add(UC_HOOK_CODE, step)
    cpu.hook_add(UC_HOOK_MEM_WRITE, wrote)
    cpu.emu_start(start, 0x9ffff, count=200)
    assert stopped, "rewrite did not return"
    regs = [cpu.reg_read(reg) for reg in REGS]
    if patched:
        regs[18] = struct.unpack("<Q", cpu.mem_read(0x50000, 8))[0]
    sp = cpu.reg_read(arm.UC_ARM64_REG_SP)
    return (regs, sp, cpu.reg_read(arm.UC_ARM64_REG_NZCV), stopped), cpu, writes


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parents[2] / "tools/patch_nso.py"
    checks = 0
    for target in ("android",):
        env = emitter(source, target)
        cases = [(0x91000652, "site_plain", None), (0xf90003f2, "site_plain", None),
                 (0xa95fcff2, "site_plain", None), (0xa9be4ff2, "site_plain", None),
                 (0xa8c24ff2, "site_plain", None), (0xf80003f2, "site_plain", None),
                 (0x90000012, "site_pcrel", None), (0xb4000812, "site_cond", None),
                 (0xd53bd052, "site_sysreg_read", "KTLS"), (0xd53bd050, "site_sysreg_read", "KTLS"),
                 (0xd51bd052, "site_tpidr_write", None), (0xd51bd050, "site_tpidr_write", None)]
        for word, name, kind in cases:
            ins = env["dis"](word, 0x10000)
            for x18 in (0, 0x70080):
                env["patch"].clear()
                env[name](ins, kind) if kind else env[name](ins)
                original = execute(struct.pack("<I", word), False, False, x18)
                for clobber in (False, True):
                    rewritten = execute(bytes(env["patch"]), True, clobber, x18)
                    assert rewritten[0] == original[0], (ins.mnemonic, ins.op_str, clobber, rewritten[0], original[0])
                    live = max(0x90000, original[0][1])
                    for address, size in original[2] + [(live, 0x90400 - live)]:
                        assert rewritten[1].mem_read(address, size) == original[1].mem_read(address, size), (ins.op_str, clobber, hex(address))
                    checks += 1
        for x18 in (0x10100, 0x10104, 0x10108):
            env["patch"].clear()
            word = 0xd61f0240
            env["site_br"](env["dis"](word, 0x10000), 0x10100, 3)
            original = execute(struct.pack("<I", word), False, False, x18)
            rewritten = execute(bytes(env["patch"]), True, True, x18)
            assert rewritten[0] == original[0], "jump-table landing pad corrupted state"
            checks += 1
        words = struct.pack("<3I", 0xc85ffc32, 0x91000652, 0xc8007c32)
        segment = list(env["md"].disasm(words, 0x10000))
        env["patch"].clear()
        env["site_window"](segment)
        original = execute(words, False, False, 0, 0x1000c)
        rewritten = execute(bytes(env["patch"]), True, True, 0, 0x1000c)
        assert rewritten[0] == original[0], "exclusive window corrupted state"
        for address, size in original[2]:
            assert rewritten[1].mem_read(address, size) == original[1].mem_read(address, size)
        checks += 1
        try:
            env["site_plain"](env["dis"](0xf81f83f2, 0x10000))
        except RuntimeError as error:
            assert "unsafe Android stack instruction" in str(error)
        else:
            raise AssertionError("unsupported below-SP access must be rejected")
    print(f"PASS Android rewrite stack: {checks} register/SP/flags/memory/branch comparisons with per-instruction signal clobber")


if __name__ == "__main__":
    main()
