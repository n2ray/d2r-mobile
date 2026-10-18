# APM shim helper: stack out-param write-set is established (Issue #73)

Status: bounded read-only static evidence; no SDK/game/rtld/phone execution. Refs #73 (parent #7). This note continues
`docs/android/sdk-apm-blr-provenance.md` at its first blocker: whether the selected APM shim helper writes the caller's
stack out-param. It does; the write-set is proved for this call. The conditional vtable mapping is not promoted to fact.

## 1. Method and owned checks

- Input is the same accepted raw ELF metadata path (raw `r_info` indices retained, 4096-byte bounded mangled-name window).
- A new bounded linear decoder inspects one helper body and one caller block. It decodes `STR`/`STRB`/`STUR`/`STP` stores,
  `LDR`/`ADD`/`MOV`/`MOVZ`/`ADR`/`ADRP`, direct `BL` thunk targets and `BLR`; no instruction is executed.
- New method has owned positive/negative vectors (`helper_dump.py --selftest`, 5/5 pass): `str x1,[x0]`, `str x1,[x0,#8]`,
  `stp x1,x2,[x0]`, `strb x1,[x0,#3]`, and the negative `str x1,[x1]` (base-register mismatch must not match).
- Limits: linear, single-helper, no general CFG; indirect calls are assumed to return; no alias analysis beyond the exact
  base register; unvisited code and other services remain unknown.

## 2. Helper identity

The call in `nn::apm::Initialize` (236-byte extent, 59 instructions) targets the `1026` GOT slot already accepted in PR #87:
defined local symbol, **nonzero raw `r_info` index**, **281-byte** mangled name, the lambda of
`nn::sf::SimpleAllInOneHipcClientManager<3>::InitializeShimLibraryHolder<nn::apm::IManager, ...>::operator()(SharedPointer<nn::apm::IManager>*)`.
Its symbol size is **408 bytes / 102 instructions**, ending in one `RET`. The body has nine direct `BL` calls and four
`BLR` calls; among the direct calls are `os::LockMutex`, `os::UnlockMutex`, `lmem::CreateUnitHeap`,
`hipc::InitializeHipcServiceResolution`, **`hipc::ConnectToHipcService`**, `hipc::FinalizeHipcServiceResolution`,
`lmem::DestroyUnitHeap`.

## 3. Inputs and the out-param store

The caller forms a temporary holder on its own stack, zero-initializes a second stack slot, and calls the helper with
`x0` = temporary holder and `x1` = address of the zeroed slot. Inside the helper:

- `x1` is moved into `x19` at entry (member function: `x0` is `this`, `x1` is the `SharedPointer<IManager>*` parameter);
  no other `x19` assignment precedes the store below;
- the object is allocated/constructed on the success block; its pointer is placed in `x20`;
- the **only** store to `[x19]` in the whole 102-instruction body is the store of that constructed pointer;
- the failure path leaves the caller slot at its pre-call zero value.

The caller then reloads the slot and null-checks it before loading `[vptr]`, `[vptr+8]` and executing `BLR`. Therefore the
slot is either zero (no helper store, no `BLR`) or the helper-written object pointer (store executed, `BLR` taken). The
previous blocker — "the stack out-param write is an ABI assumption" — is resolved for this call: **the write-set is
proven statically**, and the `BLR` receiver is the helper-written object, not an uninitialized slot.

## 4. What the helper actually does

Inside the same body the helper calls `hipc::ConnectToHipcService` with a local `svc::Handle` out-parameter and a name
pointer taken from the holder; it also performs a virtual allocation (`40`/`8` argument constants), initializes the new
object's vtable fields from a static table, and stores the object into the caller's slot. This is a genuine HIPC service
connection inside the shim initialization path, not a mere allocation.

The connected service **name** is not proved here: the name pointer comes from the holder, and the holder's initialization
(including the relation of the earlier `apm` literal to that name field) is not decoded in this slice. No port name is
asserted. `ConnectToHipcService` establishes a handle, not an `apm` command/reply contract.

## 5. Remaining boundary

- The vtable candidate from PR #87 stays **conditional**: the object type and the address-point convention are still not
  proved at the `BLR`; this note does not claim `Release` or `OpenSession` as fact.
- No `IManager` `OpenSession` command ID, request layout, reply parser or reply consumer is proved.
- Next bounded frontier: the helper's static vtable construction site / the `IManager` object type, or the holder's name
  field; not a command/reply endpoint.

No bytes, addresses, raw logs, private paths or keys are published. No runtime stubs, scanner/quarantine or global
settings changes. Issues #73 and #7 remain open.
