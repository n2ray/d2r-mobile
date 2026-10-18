# SDK module-init service contract — phase 1 evidence note (#73)

# SDK 模块初始化服务契约：阶段 1 证据说明（#73）

Status: **bounded evidence only.** One root is selected and traced until the first unproven indirection. No service is
implemented, no port/command/reply is guessed, and no startup reachability is claimed. No game, SDK, original rtld or
phone was executed. Parent #7 and this issue stay open.

状态：**仅限有界证据。** 选定一个根，追踪到第一个未证明的间接点即停止；不实现服务、不猜端口/命令/回复、不声称启动
可达。未执行游戏、SDK、原始 rtld 或手机。父 #7 与本 Issue 保持打开。

Base: accepted `main` `d7739cb` (includes #74 and #71). Repository anchors are `path:line` at that commit.

## 证据标签 | Evidence tags

| Tag | Meaning |
|---|---|
| **A** | Repository code/docs/tests at the stated commit. |
| **B** | Existing read-only static metadata report from the private SDK image. Summarized only; no bytes, addresses or symbol dumps are republished. A direct edge is not execution or reachability proof. |
| **U** | Unknown; no evidence in the existing reports. |

## 1. Selected root

**`nninitInitializeSdkModule`** — the SDK module-initialization stage already listed in the public lifecycle inventory
(`dm/android/LIFECYCLE.md:15`, `docs/android/startup-services-inventory.md:40`). This is the one root for phase 1.

- [B] An existing private bounded report (unpublished; SDK image identity recorded inside it) decodes the exact function
  body: 60 bytes, 15 instructions, **9 direct `BL` edges**, no `SVC`, no direct string references, and two `mov w0, #1`
  calls.
- [B] All nine `BL` targets are 16-byte import thunks of the shape `adrp` / `ldr` / `add` / `br x17`: the call leaves the
  module through a GOT slot. The existing report records the thunk bodies, not the resolved import names.
- [A] The public inventory records that this stage has named direct references to the `oe`, `aoc`, `err`, `fs`, `pctl` and
  diagnostic initialization families (`startup-services-inventory.md:118`), but only as a set of families.
- [A] Current runtime transport: `ConnectToNamedPort` resolves only locally registered names (`dm/android/runtime/ipc.c:572`
  dispatch, `:163` comment), `sm: GetServiceHandle` returns only an already registered local port
  (`dm/android/runtime/sm.c:30` handler), and `lm` OpenLogger/Log are bounded local implementations
  (`dm/android/runtime/lm.c:20`, `:52`). None of this is an SDK service session.

## 2. What is actually proven

| Fact | Evidence | Strength |
|---|---|---|
| The root exists and is a normal function with a return path | [B] body size/instructions | static only |
| It performs nine direct calls, all through GOT import thunks | [B] nine `BL` targets, thunk shape | static edges |
| It contains no `SVC` and no direct string reference | [B] decoded ops/literals | static only |
| Two of the calls pass constant `1` in `w0` | [B] `mov w0, #1` before two calls | static only |
| The stage is associated with `oe`/`aoc`/`err`/`fs`/`pctl`/diagnostic initialization | [A] inventory set-level statement | family set only |
| Local `sm:`/`lm` behavior is implemented and tested | [A] `ipc.c:572`, `sm.c:30`, `lm.c:20,52`, `tests/ipc.c:235` | owned host tests |

Not proven: which thunk corresponds to which family (nine edges vs six named families already shows the mapping is not
one-to-one), whether any call opens a session, and every command/reply shape.

## 3. First unproven indirection — stop point

The nine `BL` sites leave through GOT/import slots (`adrp`/`ldr`/`add`/`br x17`). The existing bounded report records the
thunks but **not the dynamic relocation resolution**, so the following cannot be derived from existing evidence:

1. per-edge import symbol or module (hence no per-edge family assignment);
2. whether an initializer opens `sm:`/named-port sessions, and the exact port name;
3. command IDs, request layouts, reply layouts or reply consumption;
4. error paths, handle ownership and rollback of any session/domain child;
5. whether any edge executes during a real startup.

This is the first unproven indirection, and phase 1 stops here. No command ID or service name beyond the already-published
family set is asserted.

## 4. Concrete blocker and unblock evidence

**Blocker (once, not repeated):** per-edge dynamic-relocation resolution for the nine thunks is absent from the existing
reports, and no existing report contains the body of any resolved initializer. Without either, session/command/reply
cannot be identified.

Unblock (either path, subject to the project's real-input authorization rules; no bytes or private logs published):

- an existing or newly generated **static** report that maps each of the nine GOT slots to its import symbol/module and
  gives the bounded body of one chosen initializer (direct `BL`/`SVC`/string/literal edges); or
- an approved controlled run that captures the same per-edge identity without executing game code.

After that, the next bounded step is one initializer's dataflow to the first session acquisition (`sm:`/named port) or a
documented stop; only then can commands/replies be evidenced. Phase 2 implementation is explicitly out of scope until
root review.

## 5. Ownership and failure acceptance plan (contingent, not implemented)

The following is a plan for a future proven service. It is **not** an implementation and does not assert current behavior.

- Host owns service registration, every session/domain handle and all rollback. A failed stage retires only the handles,
  sessions and domain children it created; pre-existing registrations are untouched. No duplicate root/port ownership.
- Unknown/unregistered service names stay unsupported. `0x4201` is the current local unsupported code; it is not a
  substitute for a proven Horizon ABI.
- Unknown command, malformed frame and unsupported version must fail explicitly, never return a success placeholder.
- Negative acceptance: no service may be registered merely to make initialization pass; a failed stage leaves no live
  handle/session; repeated acquisition/close remains bounded.
- Positive acceptance (future): one real startup path reaches the local endpoint and receives the proven reply — only after
  the blocker above is closed and reviewed.

## 6. Coordination and explicit unknowns

- #72 (virtual core mask) and #32 (touch parity) are separate owners; this note does not touch their files or claims.
- #8 lifecycle/rollback, #10 real-input acceptance, #16 log sink, #20 filesystem are linked only if a traced root proves
  those commands; none is claimed here.
- Unknowns: per-edge thunk resolution; port/service names; session acquisition; command/reply layouts; reply consumption;
  error mapping; initializer ordering and execution; whether any of the nine edges is required for startup.
- This note does not repeat the whole SDK inventory and does not claim that the family set is complete.

## 7. Reproduction boundary

The private report was read only; no SDK bytes, addresses, symbol dumps, logs, saves or credentials are included here.
The repository-side facts can be checked at the cited `path:line`. No new test was added because this slice is docs-only
and no behavior changed.
