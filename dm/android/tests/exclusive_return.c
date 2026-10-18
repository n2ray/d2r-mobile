// #4 自有反例：普通函数返回（RET）不清理独占监视器，只有 CLREX／异常返回边界才清理。
// 因此调用者不能凭“辅助函数已正常返回”或 AAPCS 推断预留状态安全；这是一个合成 ARM64 夹具，无游戏数据。
// #4 owned counterexample: a normal function return (RET) does NOT clear the exclusive monitor; only CLREX or an
// exception-return boundary does. A caller therefore cannot infer reservation safety from a normal return or AAPCS.
// Synthetic ARM64 only; no game data.
#include <stdint.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

// 保留返回结果，防止调用被优化掉；不影响监视器。
// Consume the result so the call cannot be optimised away; this does not touch the monitor.
static volatile uint32_t sink;

#if defined(__aarch64__)
// 读取独占后直接返回：没有 STXR、没有 CLREX，预留状态跨函数返回仍然有效。
// Load-exclusive then return: no STXR and no CLREX, so the reservation survives the function return.
__attribute__((naked, noinline)) static uint32_t load_then_return(void *p) {
    __asm__ volatile("ldxr w0, [x0]\nret");
}

// 读取独占后显式 CLREX 再返回：对照组，预留必须被清除。
// Load-exclusive then explicit CLREX before returning: the control that must clear the reservation.
__attribute__((naked, noinline)) static uint32_t load_then_clear(void *p) {
    __asm__ volatile("ldxr w0, [x0]\nclrex\nret");
}

// 返回 0 表示存储成功（预留仍在），1 表示失败；失败时不得写内存。
// Returns 0 when the store succeeded (reservation still live), 1 when it failed; a failed store must not write.
static int store_exclusive(uint32_t *p, uint32_t value) {
    uint32_t status;
    __asm__ volatile("stxr %w0, %w2, [%1]" : "=&r"(status) : "r"(p), "r"(value) : "memory");
    return (int)status;
}
#endif

int main(void) {
#if !defined(__aarch64__)
    puts("SKIP: exclusive-monitor counterexample requires AArch64");
    return 0;
#else
    enum { ATTEMPTS = 64 };
    uint32_t word = 0;
    int preserved = 0, failed_stores = 0;
    for (int i = 0; i < ATTEMPTS; i++) {
        uint32_t before = 0x11111111u + (uint32_t)i;
        word = before;
        sink = load_then_return(&word);
        int status = store_exclusive(&word, 0xa5a5a5a5u);
        if (status == 0) {
            preserved++;
            CHECK(word == 0xa5a5a5a5u);
        } else {
            failed_stores++;
            CHECK(status == 1);
            CHECK(word == before);          // 失败存储不得写入 | a failed store must not write
        }
    }
    // 至少一次观察到“返回后预留仍有效”；中断/上下文切换可能偶发清除，所以不断言每次。
    // Observe at least one "reservation live after return"; interrupts may clear it occasionally, so not every run.
    CHECK(preserved > 0);
    printf("exclusive_return: ret_preserved=%d/%d (spurious-fail=%d)\n", preserved, ATTEMPTS, failed_stores);

    int blocked = 0, unchanged = 0;
    for (int i = 0; i < ATTEMPTS; i++) {
        uint32_t before = 0x01010101u + (uint32_t)i;
        word = before;
        sink = load_then_clear(&word);
        int status = store_exclusive(&word, 0x5a5a5a5au);
        if (status == 1) blocked++;
        if (word == before) unchanged++;
    }
    // 对照组：同一函数形状，仅多一条 CLREX，预留必须每次都被清除。
    // Control: same function shape plus one CLREX; the reservation must be cleared every time.
    CHECK(blocked == ATTEMPTS);
    CHECK(unchanged == ATTEMPTS);
    printf("exclusive_return: clrex_blocked=%d/%d unchanged=%d/%d\n", blocked, ATTEMPTS, unchanged, ATTEMPTS);
    puts("PASS: RET preserves the exclusive reservation; CLREX clears it");
    return 0;
#endif
}
