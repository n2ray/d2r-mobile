# SDK service-path trace: oe → apm / applet (#73 phase 1 continuation)

# SDK 服务路径追踪：oe → apm / applet（#73 阶段 1 续）

Status: **bounded static evidence, no protocol proven.** A selected path from `nninitInitializeSdkModule` through
`nn::oe::Initialize` was traced with read-only GOT/relocation analysis and per-function CFG scans until the first
unsupported indirection. No SDK/game/rtld/phone was executed; no endpoint or command is implemented or guessed.

状态：**有界静态证据，未证明协议。** 从 `nninitInitializeSdkModule` 经 `nn::oe::Initialize` 选了路径，用只读
GOT/重定位与逐函数 CFG 扫描追踪到第一个不支持的间接点即停止。未执行 SDK/游戏/rtld/手机；未实现或猜测端点与命令。

Base: accepted `main` `8a18a23` (includes #76 and the corrected #73 note).

## 1. Method and limits

- Read-only static input: the user-owned dumped SDK module (same image identity as the corrected #73 note). No bytes,
  addresses, symbol dumps, private paths, logs, saves or credentials are published here.
- For each thunk reached by a `BL`: validate the full `adrp x16` / `ldr x17,[x16,#imm]` / `add x16,x16,#imm` / `br x17`
  chain, take the GOT slot, resolve it through `RELA`/`RELA-JMPREL` (`DT_HASH` nchain used for the symbol table), and map
  the symbol by name. `R_AARCH64_JUMP_SLOT` (1026) with a defined symbol record gives a local body; 1027 is a relative
  target; a 1026 slot with symbol index 0 is an unresolvable lazy slot.
- Per-function scan: CFG over direct `B`, `B.cond`, `CBZ/CBNZ`, `TBZ/TBNZ` within the symbol's extent, capped at 1200
  instructions, stopping at `RET` / `SVC` / extent end. Strings are only taken from `adrp`+`add` pointers whose bytes are
  printable ASCII. This is not whole-program analysis: absence inside scanned bodies is not global absence, and an
  unresolved call is a stop, not a proof of no edge.
- No other SDK family was enumerated.

## 2. Selected path and what is proven

| Step | Body facts (bounded) |
|---|---|
| `nninitInitializeSdkModule` | 9 `BL` + one final `B`; no `RET`; resolves to the nine initializers recorded in the corrected #73 note (oe, aoc, err, fs×3, pctl, os VAM, diag startup) |
| `nn::oe::Initialize` | calls `os::SdkMutexType::Lock`, `apm::Initialize`, `oe::InitializeApplet`, `am::GetApplicationFunctions`; no `SVC`/service string |
| `nn::apm::Initialize` (selected candidate) | 35-instruction CFG over the 236-byte body: only `os::LockMutex` / `os::UnlockMutex`; no `SVC`, no string, no session/CMIF call; returns. It is **not** a service path. |
| `nn::oe::InitializeApplet` | calls `am::InitializeApplicationInterfaces`, `am::GetCommonStateGetter`, `am::GetSelfController`, `os::AttachReadableHandleToSystemEvent`, `oe::SetupGpuErrorHandler`, `os::WaitSystemEvent`, os mutex; no `SVC`/service string |
| `nn::am::InitializeApplicationInterfaces` | mutex lock/unlock, `am::InitializeCommonInterfaces(proxy)`, diag `AbortImpl`, `os::GetCurrentProcessHandle`, `os::CloseNativeHandle`, `os::SleepThread`, and one `BL` through a **JUMP_SLOT (1026) with symbol index 0**; no `SVC`/GetServiceHandle/CMIF/name string |
| `nn::am::InitializeCommonInterfaces(IStackableAppletProxy*)` | 359-instruction CFG (full 1436-byte symbol scanned): mutex operations and diag `AbortImpl` only; it consumes a proxy argument; no session call or command in the body |

## 3. First unsupported indirection — stop point

The traced bodies contain no `sm:` `GetServiceHandle`, no `ConnectToNamedPort`, no CMIF request builder/consumer, and no
service-name string. The first unsupported indirection is one of:

1. `nn::am::InitializeCommonInterfaces` operates on an `IStackableAppletProxy*` whose construction/session is not in the
   traced bodies: it is passed in, and the traced callers do not create it; its provenance is outside this bounded
   subtree; or
2. the `BL` in `nn::am::InitializeApplicationInterfaces` through a `JUMP_SLOT` (1026) with symbol index 0, whose target
   cannot be resolved from static metadata (unsupported lazy slot).

Either way no port name, session acquisition, command number, request/reply layout or reply consumer is proven. Phase 1
stops here; nothing is guessed.

## 4. Concrete blocker / next bounded step

**Blocker (recorded once):** resolve the symbol-index-0 JUMP_SLOT through loader/rtld relocation metadata, or locate the
constructor that creates the `IStackableAppletProxy` passed to `InitializeCommonInterfaces`. Without one of those, the
session and command cannot be attached to this path.

Unblock options, static first: (a) an rtld/relocation artifact that names that slot; (b) a bounded caller/argument
dataflow report for the proxy (who writes the global/argument and which session backs it). Only after that can a port and
command be traced. A controlled run is not requested here and would need separate authorization.

## 5. Explicit unknowns

- Port/service name, session acquisition, domain conversion, command IDs, request layout, reply layout, reply consumer.
- Whether `apm::Initialize`, `oe::InitializeApplet` or the `am` interface calls execute at startup at all.
- Whether the symbol-0 slot is an ifunc/lazy resolver or an undefined weak import.
- Behaviour of the other eight initializers and of all other SDK families (not enumerated).

## 6. Ownership/failure acceptance plan (contingent, unchanged)

If a service is later proven: host owns registration, sessions/domain handles and rollback; a failed stage retires only
what it created; unknown service stays unsupported (`0x4201` is the current local code, not an ABI substitute); unknown
command/malformed frame/unsupported version fail explicitly; no service is registered merely to make init pass; repeated
acquire/close stays bounded. No implementation is proposed before root review.

## 7. Reproduction boundary

The read-only tracer lives in the private job workspace and reads the dumped module directly; it executes no instruction
from it. Only derived symbol names and counts appear above. Repository anchors for the local transport remain the ones in
the corrected #73 note. No production admission, scanner or quarantine change is included.
