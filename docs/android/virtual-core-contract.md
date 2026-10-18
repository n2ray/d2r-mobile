# Virtual core mask contract: GetInfo ID 0 and SetThreadCoreMask

Issue: [#72](https://github.com/n2ray/d2r-mobile/issues/72) (A5.1), phase 1.
Status: **evidence and contract only. No runtime implementation, no execution,
no mask constant and no physical-affinity mapping is proposed here.**

Repository anchors use accepted main `73c2c2f`. Private SDK observations are
aggregate static metadata only: no game/SDK bytes, raw logs, saves, keys or
private paths are published.

Evidence levels:

- **P — public ABI/reference**: libnx and Atmosphère at the pinned commits
  below. Atmosphère is a reference mechanism, not the proprietary kernel.
- **S — static SDK metadata**: direct wrapper/callsite evidence; it does not
  prove that the call executes or returns.
- **R — repository**: current code at `73c2c2f`.
- **U — unknown**: no protocol, reachability or value proof.

## 1. Public ABI

The SVC boundary is fixed by the public wrapper and reference implementation.

| Operation | Input registers | Output | Width / rule |
|---|---|---|---|
| GetInfo SVC `0x29` | `x0` = out pointer, `w1` = info id, `w2` = handle, `x3` = sub-id | `w0` = Result, `x1` = value | `x1` is stored as a 64-bit value by the wrapper; the caller's `x0` pointer is preserved separately |
| ID 0 / `InfoType_CoreMask` | `w1 = 0`, `w2 = CUR_PROCESS_HANDLE`, `x3 = 0` | `x1` = allowed core mask | Bitmask of allowed virtual core IDs; the kernel reads the process core mask |
| SetThreadCoreMask SVC `0x0f` | `x0` = thread handle, `w1` = preferred core (`s32`), `w2` = affinity mask (`u32`) | `w0` = Result | No output pointer; handle validation and mask validation precede state change |
| CreateThread SVC `0x08` | `x0` = out handle pointer, `x1` = entry, `x2` = argument, `x3` = stack top, `w4` = priority, `w5` = core id (`s32`) | `w0` = Result, `w1` = handle | Takes one ideal core id, not a mask |

`CUR_PROCESS_HANDLE` is `0xFFFF8001`
([libnx `svc.h:10`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L10)).
The GetInfo wrapper stores the 64-bit `x1` result
([libnx `svc.s:248-255`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/source/kernel/svc.s#L248-L255)).
`InfoType_CoreMask = 0` is described as a bitmask of allowed core IDs
([libnx `svc.h:186`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L186)).
The SetThreadCoreMask signature is
`(Handle handle, s32 preferred_core, u32 affinity_mask)`
([libnx `svc.h:588-592`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L588-L592));
its SVC has no output pointer
([libnx `svc.s:100-102`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/source/kernel/svc.s#L100-L102)).

### 1.1 GetInfo ID 0 rules

The public reference implementation:

```text
InfoType_CoreMask -> *out = process->GetCoreMask()
```

([Atmosphère `kern_svc_info.cpp:43-44`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_info.cpp#L43-L44)).

The selected handler:

- requires `info_subtype == 0`; otherwise `ResultInvalidCombination`
  ([Atmosphère `kern_svc_info.cpp:155`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_info.cpp#L155));
- resolves the supplied handle to a `KProcess`; a missing object is
  `ResultInvalidHandle`
  ([Atmosphère `kern_svc_info.cpp:182-185`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_info.cpp#L182-L185));
- returns the process's allowed mask, not the current thread's affinity mask.

For this contract the supported handle is the current-process pseudo handle.
Using an arbitrary process handle, a non-zero sub-id, or an unowned process
handle is **U** for this runtime and must fail rather than be guessed.

### 1.2 SetThreadCoreMask rules

The public reference implementation takes a thread handle owned by the
current process and validates the preferred core and affinity mask before
setting state
([Atmosphère `kern_svc_thread.cpp:161-190`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_thread.cpp#L161-L190)):

- invalid/foreign handle: `ResultInvalidHandle`;
- `affinity_mask` must be non-zero and a subset of the process core mask;
  a mask outside it is `ResultInvalidCoreId`, zero is
  `ResultInvalidCombination`;
- when `core_id` is a valid virtual core (`0 <= core_id <
  cpu::NumVirtualCores`), its bit must be present in `affinity_mask`;
  otherwise `ResultInvalidCombination`;
- when `core_id` is not a valid virtual core, only the public special values
  below are accepted; other values are `ResultInvalidCoreId`.

The public special values at the pinned reference are:

| Value | Name | Acceptance rule |
|---|---|---|
| `-1` | `IdealCoreDontCare` | accepted only in the non-valid-core branch with a valid non-zero subset mask |
| `-2` | `IdealCoreUseProcessValue` | selects the process ideal core and a mask of that one core; the ordinary subset check is bypassed by the reference implementation |
| `-3` | `IdealCoreNoUpdate` | accepted only in the non-valid-core branch with a valid non-zero subset mask |

Source: [Atmosphère `svc_types_common.hpp:379-381`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libvapours/include/vapours/svc/svc_types_common.hpp#L379-L381).
The exact kernel-side selection behavior of `DontCare` and `NoUpdate` after
acceptance remains **U** for this contract; only the validation and state
ownership rules are claimed.

The SVC-level mask is `u32`. `GetInfo ID 0` returns a `u64` process mask. Do
not silently widen the SetThreadCoreMask input or assume that high bits above
31 exist on the target.

### 1.3 CreateThread relation

`CreateThread` accepts one `s32` core id and validates it against the process
core mask; `IdealCoreUseProcessValue` selects the process default. A core
outside the valid range or outside the process mask is `ResultInvalidCoreId`
([Atmosphère `kern_svc_thread.cpp:28-37`](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/svc/kern_svc_thread.cpp#L28-L37)).
It has no affinity-mask argument
([libnx `svc.h:544`](https://github.com/switchbrew/libnx/blob/20cbcfd90567f7bf84df7725f27e345e2164fa6b/nx/include/switch/kernel/svc.h#L544)).

Therefore the coherent model is:

- `GetInfo ID 0` reports the process-level allowed mask;
- `CreateThread` chooses one ideal core that must be in that mask;
- `SetThreadCoreMask` may later set a per-thread subset mask and preferred
  core, subject to the same process mask;
- none of these imply host/physical CPU affinity. Mapping a guest virtual
  mask to host scheduling is a separate, unapproved contract.

## 2. Static SDK evidence

The following are direct static references from the bounded SDK metadata. They
are **S**, not execution or reachability proof.

| Static observation | Evidence |
|---|---|
| `nn::os::detail::ThreadManagerImplByHorizon::GetThreadAvailableCoreMask` callsite `0x350764` | resolved wrapper `0x39fbec`, SVC `0x29`; straight-line constants `w1=0`, `w2=0xffff8001`, `x3=0` |
| `nn::os::detail::ThreadManagerImplByHorizon::SetThreadCoreMask(ThreadType*, int, unsigned long)` callsite `0x350698` | resolved wrapper `0x39fae4`, SVC `0x0f`; no suffix constant set is available for the arguments |
| main `nnMain` references | `0x1355d0` → `GetThreadAvailableCoreMask`; `0x1355e0` → `SetThreadCoreMask`; exact argument values are not in the bounded metadata |
| SDK wrapper symbols | `_ZN2nn2os26GetThreadAvailableCoreMaskEv`; `_ZN2nn2os17SetThreadCoreMaskEPNS0_10ThreadTypeEim`; `_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mii` |
| Current process handle constant | `0xffff8001` in the selected GetInfo path, matching `CUR_PROCESS_HANDLE` |

The static path is sufficient to prove the selector (`ID 0`), the SVC selection
(`0x29`/`0x0f`), the wrapper argument types, and the current-process handle
value. It does **not** prove:

- that `nnMain` executes either call;
- the actual preferred-core or mask arguments at `0x1355e0`;
- whether the wrapper's abort path is reached on any error;
- the process's actual mask value or virtual-core count.

The existing SDK metadata reports no execution of a real initializer or game.

## 3. Current runtime state at `73c2c2f`

| Item | Current state | Consequence |
|---|---|---|
| GetInfo ID 0 | `dm/android/runtime/memory.c:130-161` has no `case 0`; the switch default returns `DM_SVC_NOT_IMPLEMENTED` (`0x4201`) | No virtual-core query exists; a success value must not be fabricated |
| SVC `0x0f` | `dm/android/runtime/svc.c:15-49` has no `case 0x0f`; the default returns `0x4201` | No thread mask state exists |
| CreateThread | `dm/android/runtime/sync.c:204-211` accepts only `-2` or core ids `0..3` and stores no process-mask relation | It rejects public special values `-1`/`-3` and cannot enforce the mask contract |
| Legacy host | `dm/host/hle_svc.c:225-228` hardcodes GetInfo 0 to `7`; `dm/host/hle_thread.c:481-486` returns success for `0x0f` with loose updates | Legacy macOS host behavior only; not Android production evidence and must not be reused as the contract |

The hardcoded legacy `7` is not adopted as a mask here. No initial mask value
is proven by public ABI or current static metadata.

## 4. Unknowns and precise blocker

The protocol and ownership shape are proven at the public/reference and static
edge levels. The following are still **U** and block a runtime implementation
until resolved or explicitly chosen by the owner:

1. **Initial allowed mask.** No value is proven for the game process. The
   current host's `7` is legacy bookkeeping, not evidence. A host policy must
   define a non-zero mask without claiming a physical core set.
2. **Virtual core count / process ideal core.** The reference exposes
   `cpu::NumVirtualCores` and a process ideal core, but the SDK's actual values
   are not established by this inventory. Do not hardcode four cores merely
   because the reference has a bounded range.
3. **Main callsite arguments.** `0x1355e0` is a direct reference; the exact
   core and mask arguments, and whether the path executes, are unknown.
4. **`DontCare`/`NoUpdate` post-validation behavior.** Acceptance rules are
   public; exact selection/update behavior in the target kernel is not proven.
5. **Result mapping.** Public names are known (`ResultInvalidHandle`,
   `ResultInvalidCoreId`, `ResultInvalidCombination`); the current runtime
   uses local numeric results. Do not equate the current default `0x4201`
   with the final Horizon result until the mapping is reviewed.
6. **Host affinity.** No permission to call host affinity APIs or claim a
   physical-core mapping is implied by this contract.

This is the single phase-1 blocker list; it is not evidence of implementation
or a request to broaden the audit.

## 5. Ready owned negative-acceptance plan (phase 2, after root review)

One owned test target, using actual rewritten SVC gates and a host-owned
virtual mask `M` selected by the reviewed policy:

1. **GetInfo 0 positive:** call with `id=0`, current-process handle and
   `subid=0`; expect `w0=0`, 64-bit `x1=M`, and no host affinity call.
2. **GetInfo 0 negatives:** wrong process handle → explicit invalid-handle
   result; non-zero subid → invalid-combination; both leave the output storage
   unchanged.
3. **SetThreadCoreMask positive:** valid thread handle, valid core in `M`,
   non-zero subset mask; expect Result 0 and visible per-thread state only.
4. **SetThreadCoreMask negatives:** zero mask → invalid-combination; mask
   outside `M` → invalid-core; valid core absent from mask → invalid-combination;
   invalid core values → invalid-core; wrong handle → invalid-handle.
5. **Special values:** `-1` and `-3` accepted only with a valid non-zero
   subset mask; `-2` selects the process default; no unsupported special value
   silently succeeds.
6. **CreateThread relation:** core outside `M` fails; a valid core in `M`
   succeeds; `-2` uses the process default; no physical affinity is observed.
7. **Failure atomicity:** every negative case leaves the previous virtual
   state and host scheduling unchanged; a failed call allocates/retains no
   handle or mask record.
8. **Sanitizer/build:** ASAN/UBSAN test through the real gate; Android
   cross-build for changed runtime units; no device/game claim.

This plan is ready for review but is **not implemented** in this phase.

## 6. Source anchors

Repository:

- `73c2c2f:dm/android/runtime/memory.c:130-161` — current GetInfo switch.
- `73c2c2f:dm/android/runtime/svc.c:15-49` — current SVC dispatch.
- `73c2c2f:dm/android/runtime/sync.c:204-211` — current CreateThread core check.
- `73c2c2f:dm/host/hle_svc.c:225-228` and `dm/host/hle_thread.c:481-486` —
  legacy host behavior, not Android production.
- `73c2c2f:docs/android/startup-services-inventory.md:143` — G1 ownership.

Public reference (pinned):

- libnx `20cbcfd90567f7bf84df7725f27e345e2164fa6b`:
  `nx/include/switch/kernel/svc.h:10,186,544,588-592,855`;
  `nx/source/kernel/svc.s:55-60,100-102,248-255`.
- Atmosphère `6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8`:
  `libraries/libmesosphere/source/svc/kern_svc_info.cpp:43-44,127-185`;
  `libraries/libmesosphere/source/svc/kern_svc_thread.cpp:24-37,150-190`;
  `libraries/libvapours/include/vapours/svc/svc_types_common.hpp:379-381`.
