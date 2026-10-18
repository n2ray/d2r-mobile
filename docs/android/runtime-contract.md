# Android runtime contract: async events, TLR preemption state and SVC 0x36

Issue: [#3](https://github.com/n2ray/d2r-mobile/issues/3) (A1). Status: **design and
falsifiable acceptance plan only.** Nothing in this document is implemented or verified
on Android. No game, SDK, original rtld or phone was executed to write it. The
production scanner, diagnostic quarantine and SDK admission block stay unchanged.

Evidence base: repository commit `9bb2fa7de18d61c22bb4a25c161ea97eb547bbd3`
(paths below are relative to the repository root, `file:line` at that commit), plus
open-source reference implementations pinned to exact commits:

- libnx `20cbcfd90567f7bf84df7725f27e345e2164fa6b`
- Atmosphère `6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8`

Atmosphère's mesosphere kernel is a clean-room *reference mechanism*. It is not the
proprietary kernel, and this contract does not require proprietary source. Where only
the reference describes a behavior, the claim is tagged **[ref]**. Claims read from
this repository are tagged **[repo]**. Proposals are tagged **[design]**, and open
questions are tagged **[unknown]**.

## 1. Observable events per platform

"Recoverable" means execution can return to the interrupted guest instruction stream
with guest-visible state intact. Every entry not listed fails closed.

| Event | Android ARM64 (`dm/android/runtime`) | macOS host owned tests (same runtime, `tpidrro_el0` key) | Linux host | Upstream macOS game host (`dm/host`) |
|---|---|---|---|---|
| Gate round trip (rewritten SVC/TLS/TLR/x18 site) | **[repo]** Recoverable. `dm_bridge_gate_entry` looks up the TP key, validates domain, active frame, SP alignment and stack bounds, saves a 160-byte frame below the RUN frame, runs the service on the native stack and restores x18/x29/FPCR/FPSR/LR (`dm/android/runtime/bridge.S:16-157`). Unknown gate return address aborts (`dm/android/runtime/dispatch.c:26-28`). Not executed on device for the current tree. | **[repo]** Same code path. The owned bridge/gate tests are the only execution evidence (`dm/android/BASELINE.md:16-21`). | **[unknown]** Same source compiles with `tpidr_el0`. Linux execution is Issue #64's scope and not claimed here. | **[repo]** Different mechanism (`dm/host/hle.c:85-99`). Out of scope for Android admission. |
| Gate reentry (gate called while `domain` is SERVICE or EXITING, or `active != 0`) | **[repo]** Not recoverable. `brk #0xd18` (`bridge.S:43-47,186-187`). This covers a signal handler or callback that reaches the gate during a service. | **[repo]** Same. | **[unknown]** | n/a |
| Asynchronous signal while guest code runs (GUEST domain) | **[unknown]** No handler is installed by the runtime. A host handler without `SA_ONSTACK` would run on the *guest* stack, whose production minimum is 4096 bytes (`bridge.c:33`), using the native TP. bionic allocates a per-thread alternate signal stack, but only `SA_ONSTACK` handlers use it ([bionic `pthread_create.cpp`](https://android.googlesource.com/platform/bionic/+/refs/heads/main/libc/bionic/pthread_create.cpp), `__init_alternate_signal_stack`). Whether ART, sigchain or vendor handlers fire on game threads is unverified. | **[repo]** Only the separate diagnostic child installs handlers, and it terminates with exit 80/81 (`dm/android/startup_diagnostic_svc.c:50-56`). That is a stop, not a resume. | **[unknown]** | **[repo]** `dm/host/crash.c:134-184` handles crashes, and `dm/host/diag.c:525` samples with SIGUSR2. This is not an Android contract. |
| Asynchronous signal while a service runs (SERVICE domain) | **[repo]** The handler runs on the native stack. Reentry into the gate traps. Any other handler effect on guest state is outside the contract. | **[repo]** Same. | **[unknown]** | n/a |
| Host → guest callback (native code calling guest code) | **[repo]** Not supported. There is no callback entry in the bridge, and a nested entry during SERVICE traps (row 2). | Same. | Same. | n/a |
| ExitThread (SVC 0x0a) from a guest thread | **[repo]** Recoverable for the *host* only. `dm_bridge_request_exit` accepts only a registered SVC 0x0a gate site and sets `action=1` (`bridge.c:207-212`). The terminal path returns to RUN via `dm_thread_stack_leave` (`bridge.S:158-176`, `thread_stack.S` LEAVE). Then the host detaches TLS and publishes completion (`dm/android/runtime/sync.c:169-175`). External or registered threads get `BAD_STATE` (`sync.c:257`). | Same, plus the sanitized retire path. | **[unknown]** | **[repo]** `dm/host/hle_thread.c:463`. |
| Guest entry function returns without ExitThread | **[repo]** Not recoverable. Production RUN sets LR to `brk #0` (`thread_stack.S` RUN `1:`), and the sanitized path calls `dm_thread_returned` → abort (`dispatch.c:48`). | Same. | **[unknown]** | n/a |
| Native thread exit while attached | **[repo]** The TSD destructor runs `release`, which aborts if the bridge is active or the thread is off the native stack (`context.c:21-30`, `bridge.c:77-82`). | Same. | **[unknown]** | n/a |
| Synchronous exception in guest code (SIGSEGV/SIGBUS/SIGILL/BRK) | **[repo]** Not recoverable. No handler is installed, so the process terminates with the platform default (debuggerd on Android). There is no guest-visible exception delivery (no Horizon user exception handler emulation). | Same. Only the diagnostic child reports it. | **[unknown]** | **[repo]** `crash.c` logs and terminates. |
| Preemption of a guest thread by the host scheduler | **[repo]** Always possible. Guest threads are plain pthreads (`sync.c:240-245`). Mapping virtual priority and core affinity is explicitly *not* done (`sync.c:209-210`). | Same. | Same. | Same. |
| Horizon "pin on interrupt while `disable_count != 0`" | **[unknown]**/not emulated. See §2. | Same. | Same. | Not emulated. |
| SVC 0x36 SynchronizePreemptionState | **[repo]** Falls into `default`, which writes `0x4201` to x0 and returns (`dm/android/runtime/svc.c:49-52`). **This is not the target contract**; see §3. | Same. | Same. | **[repo]** The `default` case logs once and returns *success* (`dm/host/hle_svc.c:248-250`). This placeholder success must not be ported. |
| Any other unknown SVC number | **[repo]** `0x4201` in x0, guest resumes (`svc.c:16,49-52`). | Same. | Same. | Placeholder success (same lines). |

## 2. TLR+0x100 / TLR+0x102: producer, consumer, nesting, unwind

### Layout (source-backed)

**[ref]** `ams::svc::ThreadLocalRegion` (arm64) places `volatile u16 disable_count` at
0x100, `volatile u16 interrupt_flag` at 0x102 and `cache_maintenance_flag` at 0x104,
with `static_assert`s on all three offsets
([`svc_thread_local_region.hpp:24-36`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libvapours/include/vapours/svc/arch/arm64/svc_thread_local_region.hpp#L24-L36)).

**[repo]** Android places the TLR at `tls + 8192` inside an `8192+0x600` allocation
(`dm/android/runtime/context.c:60-70`), with the thread object at TLR+0x200
(`context.c:71-72`). The `calloc` path zero-fills it. The accounting path
(`dm_host_accounting_local_create`) is assumed zero. **[unknown]** That assumption was
not re-proved here. The runtime never reads or writes TLR+0x100..0x103. The guest reaches
the TLR only through the `DM_GATE_TLR_READ` gate (`dispatch.c:35`).

### Roles in the reference mechanism

| Field | Producer | Consumer | Source |
|---|---|---|---|
| `disable_count` (+0x100) | **User code.** It increments before a "must not be preempted" section and decrements after it. Nesting is a counter, not a flag. | **Kernel**, on an interrupt or user-mode exception entry. If `disable_count != 0` and no thread is pinned on this core, the kernel pins the current thread and sets `interrupt_flag`. | **[ref]** [`kern_k_interrupt_manager.cpp:187-194`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/arch/arm64/kern_k_interrupt_manager.cpp#L187-L194), [`kern_exception_handlers.cpp:609-616`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/arch/arm64/kern_exception_handlers.cpp#L609-L616), [`kern_k_thread.hpp:648`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/include/mesosphere/kern_k_thread.hpp#L648) |
| `interrupt_flag` (+0x102) | **Kernel**: `SetInterruptFlag()` writes 1 when pinning, and `ClearInterruptFlag()` writes 0 in SVC 0x36. | **User code**: after decrementing `disable_count` to zero, it checks the flag. If set, it calls SVC 0x36 to unpin. | **[ref]** [`kern_k_thread.hpp:649-650`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/include/mesosphere/kern_k_thread.hpp#L649-L650), [`kern_svc_synchronization.cpp:119-132`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_synchronization.cpp#L119-L132). For user-side gating on `InfoType_IsSvcPermitted`, see [`os_disable_counter.os.horizon.hpp:24-26`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libstratosphere/source/os/impl/os_disable_counter.os.horizon.hpp#L24-L26). |

**[unknown]** The exact user-side instruction sequence inside *this* SDK (where it
increments, the order of the decrement and flag check, and whether it re-reads the flag
after SVC 0x36) has not been traced from the guest binary. Reading game or SDK dumps
was out of scope for this trial. Issue #5 must establish it from the admitted input,
without publishing bytes.

### Mechanism versus required observable behavior

The mechanism (kernel pinning a thread to a core) is not required on Android. These
guest-observable properties are required, and are the only behavior a design may
claim:

1. **[design] O1 (count ownership).** `disable_count` is guest-owned. The runtime never
   writes it, and its value survives every gate round trip, signal and host
   preemption unchanged. This is falsifiable by the round-trip counterexample C1.
2. **[design] O2 (flag only by host).** `interrupt_flag` changes only through runtime
   action. It is set only while `disable_count != 0`, and cleared only by SVC 0x36.
   The guest never observes `interrupt_flag == 1` while `disable_count == 0` unless it
   wrote that itself.
3. **[design] O3 (progress).** A guest section with `disable_count != 0` must not wait
   indefinitely on a thread that the same section starves. On Horizon, pinning makes
   the section run to completion on its core. With Android pthreads the host may
   preempt it at any time (§1). The guest's correctness argument therefore rests on
   *other* threads not observing a half-finished section. That is **[unknown]** for this
   SDK and is the real risk Issue #5 must evaluate.

### Proposed Android realization

- **[design] Option A (minimal, recommended first).** Never set `interrupt_flag`.
  Under this model the flag stays zero forever, and the guest takes its "not
  interrupted" path. This is honest only if O3 holds for the specific SDK sequences
  that use the counter. **This option rests on an assumption.** It must not be
  argued as "the field is zero at start, so the pending path never exists"; see C2
  and C3.
- **[design] Option B (emulated pinning).** At gate entry with `disable_count != 0`,
  record that a host preemption *could* have occurred, and set `interrupt_flag = 1`
  before returning to the guest. Implement SVC 0x36 as "clear the flag and unpin", with
  no-op unpinning. This exercises the guest's pending path without claiming real
  pinning. It still does not provide O3.
- **Nesting.** The guest counts nesting itself. The runtime must neither saturate nor
  reset the counter, and gate frames never save or restore it (`bridge.S:98-110`
  saves only registers).
- **Unwind and thread exit.** On ExitThread or a fatal stop with `disable_count != 0`,
  **[design]** the runtime must treat the state as a guest contract violation: a
  diagnostic stop on Android, never a silent clear. On Horizon the reference unpins a
  terminating pinned thread through the scheduler, which is a kernel detail and not a
  guest-observable value.

## 3. SVC 0x36 (SynchronizePreemptionState) ABI

- **[ref]** The SVC number is 0x36. It takes no input and has no output
  (`void svcSynchronizePreemptionState(void)`, introduced in 8.0.0, and described as
  "used during abort/panic") ([libnx `svc.h:1000-1004`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L1000-L1004)).
  The kernel side is `void SynchronizePreemptionState64()` with no `Result`
  ([`kern_svc_synchronization.cpp:154-156`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_synchronization.cpp#L154-L156)).
  Its use is permission-gated (`InfoType_IsSvcPermitted = 26`,
  [libnx `svc.h:212`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L212)).
- **Consequence.** The guest cannot observe failure, because it reads no Result. A
  returned error code is therefore not a failure report. It is silent continuation, and
  it clobbers x0, which the caller is entitled to assume is unspecified anyway.
  **[unknown]**: which registers Horizon preserves across a void SVC. The current
  `dm_host_svc` writes x0 unconditionally (`svc.c:51`), and only the gate frame's saved
  registers are restored.
- **[design] Required behavior.**
  1. Until Option A or Option B is admitted, SVC 0x36 must **fail-stop**: a
     diagnostic trap with SVC number and site, the same class as an unknown gate. It
     must not return `0x4201`, and it must not return success (the upstream macOS
     `hle_svc.c:248-250` behavior).
  2. Once admitted, it clears `interrupt_flag` (+0x102), leaves `disable_count` (+0x100)
     and all general registers except those the ABI marks clobbered unchanged, and
     returns to the next instruction.
  3. If the process did not report the SVC as permitted through GetInfo(26), any call
     is a contract violation and must stop. **[unknown]**: whether the Android runtime's
     GetInfo currently answers ID 26. The observed real-stage GetInfo IDs did not
     include 26 (`dm/android/README.md:402`).

## 4. Finite support envelope and unknown-event failures

Supported (claimable only after the listed counterexamples pass on the stated platform):

- Synchronous gate round trips for registered sites only, on an attached thread, with
  the stack bounds in `bridge.S:48-91`.
- ExitThread from a started guest thread at a registered SVC 0x0a site.
- Host preemption of guest pthreads, with TLR+0x100/+0x102 semantics per §2
  Option A or B once admitted.

Everything else is **outside the envelope and must stop, not continue**. This covers
callbacks into the guest, guest-visible exception delivery, signal-driven guest
resumption, unregistered gates, guest entry return, SVC 0x36 before admission, any
SVC whose ABI has no Result, and **[design]** any unknown SVC. The current code
returns `0x4201` for unknown SVCs (`svc.c:16,49-52`). That is acceptable only for
SVCs whose ABI returns a Result in x0 *and* whose caller is known to check it. Issue #5
must either prove that per SVC number or change the default to a fail-stop. This
document does not change runtime behavior.

## 5. Counterexamples (falsification plan)

Each counterexample is an owned, game-free test. A design claim is refuted if the
test observes the stated outcome. None of these tests is implemented in this change.
Running them on macOS or Linux hosts does not prove guest compatibility. Android
results require a device run with the review material that `dm/android/BASELINE.md`
requires.

| ID | Refutes | Setup | Refuting observation |
|---|---|---|---|
| C1 | O1 | An owned guest fragment writes `disable_count = 3` (a nonzero, non-1 value), performs N gate round trips (SVC 0x1e, TLR read) with forced host preemption (a competing thread plus `sched_yield`), then reads the field back. | Any value other than 3, or `interrupt_flag` changing under Option A. |
| C2 | "zero-initialized ⇒ the pending path never runs" | The guest pre-sets `interrupt_flag = 1` before entering a counted section, which is legal guest memory. It then decrements `disable_count` to 0 and calls SVC 0x36. | The run continues with the flag still 1, or SVC 0x36 returns `0x4201` or 0 instead of the designed fail-stop or clear. |
| C3 | "zero-initialized TLR" itself | Attach a thread through the *accounting* allocation path (`context.c:62-65`) after another thread used and released the same local. Read TLR+0x100..0x103 before guest entry. | A nonzero byte. That would show that zero is not guaranteed on that path. |
| C4 | §3 rule 1 | Guest executes `svc #0x36` at a registered gate site with no admission. | Execution reaches the next guest instruction. |
| C5 | O2 with nesting | `disable_count` goes 0→1→2→1→0 across gate calls, with Option B active. | The flag is set while the count is 0, or cleared by anything except SVC 0x36. |
| C6 | Envelope (signal) | Raise a non-`SA_ONSTACK` signal on a guest thread running on a 4 KiB guest stack. | The guest resumes with corrupted guest stack below SP, instead of a contained stop. |
| C7 | Envelope (exit) | ExitThread with `disable_count != 0`. | A silent clean exit with no diagnostic. |
| C8 | Unknown SVC | Guest issues an SVC with no Result ABI (0x36 now, others as found) through the default path. | The guest continues with a fabricated x0. |

## 6. Assumption register

| # | Assumption | Status | Closes when |
|---|---|---|---|
| A1 | The mesosphere TLR layout (0x100/0x102) matches the console kernel this SDK targets. | Reference-backed, not proven for the target. | The field offsets are confirmed from the SDK's own access sequence in Issue #5, without proprietary kernel source. |
| A2 | This SDK uses the counter/flag pair only around short critical sections. | Unknown. | The SDK sequence is traced, and O3 is argued per site. |
| A3 | The accounting-path TLR is zero-filled. | Unproved. | C3 passes. |
| A4 | No host signal handler runs on guest stacks on Android. | Unknown. | C6, plus a device survey of installed handlers (`SA_ONSTACK` flags). |
| A5 | Horizon register preservation for void SVCs. | Unknown. | The libnx/mesosphere SVC entry path is reviewed, and the gate restore set is matched. |
| A6 | Android scheduling (big.LITTLE, no core pinning) does not violate O3. | Unknown. This is the main risk. | An argument per SDK site, plus a stress run on the Xiaomi 15 Ultra. |

Nothing here unblocks production admission by itself. It defines what Issue #5 must
implement and what evidence refutes it.

## 中文摘要

本文件是 #3 的设计与可证伪验收方案，**未实现、未在 Android 上验证**，没有执行游戏、SDK、原始 rtld 或手机。
事件表（§1）区分了 Android、macOS、Linux 与上游 macOS 宿主。在 Android 上，只有已登记门往返和 ExitThread 可以恢复；
信号、回调、客体异常和入口函数直接返回都必须停止。
TLR+0x100（disable_count）归客体所有，客体负责递增、递减和嵌套计数；TLR+0x102（interrupt_flag）只由宿主置位，只由 SVC 0x36 清除。
参考机制来自 Atmosphère（固定提交），不以专有源码作为完成条件。
SVC 0x36 没有 Result 输出，客体无法察觉失败，所以在准入前必须失败停止，不能返回 0x4201，更不能返回成功。
上游 `hle_svc.c` 的占位成功不得移植。
§5 的反例 C1–C8 用于证伪；其中 C2 和 C3 专门反驳“TLR 初始全零就说明待处理路径不存在”的推断。
§6 逐项登记了剩余假设。
