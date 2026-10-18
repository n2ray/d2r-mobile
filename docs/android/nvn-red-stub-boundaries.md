# Four statically-called NVN red stubs: boundaries, evidence and proposed mappings (G0, issue #41)

中文要点：本文只做**源码级边界**与**可独立领取的实现任务**。四个入口的静态调用点存在（各 1/2/2/2），
运行时是否可达**未知**；NVN 没有公开规范，凡名字不能证明的行为一律标为 blocked，不写猜测映射、不做 no-op。
每个入口对应一个子 Issue：#53（SetPatchSize）、#54（MultiDrawArraysIndirectCount）、
#55（MultiDrawElementsIndirectCount）、#56（DownsampleTextureView，语义未定，先取证）。

This document is source-level. It says what the repository can prove about four NVN command-buffer entries the game
looks up and calls statically, what it cannot prove, and what a separately claimable implementation slice would have to
own. It is **not** a compatibility, reachability or playability claim, and it does not implement any of the four.

## Evidence rules used here

1. **Repository facts** are cited by file and line. The numbers come from `dm/gfx/nvn/nvn_slots.tsv`, produced by
   `dm/gfx/tools/nvn_slots.py` from `main` (columns: name, slot address, static call-site count, first call site),
   and were already used by #11 (`docs/android/nvn-inventory.md`).
2. **NVN protocol facts** are only asserted where the repository's own headers or prototypes show them
   (`dm/gfx/nvn/include/nvn.h`, `dm/gfx/nvn/nvn_protos.h`). NVN has no public specification to cite; where a fact is
   not in the repository, it is marked **blocked**, not inferred from the name.
3. **Foreign API facts** (OpenGL / Vulkan) are cited to primary Khronos pages, and are used only as *candidate*
   mappings — never as proof that NVN behaves the same way.
4. **Runtime reachability is unknown** for all four entries. The only runtime note in the tree is a comment in
   `dm/gfx/nvn/hle_nvn.c:929-933` stating that ~42,000 frames across four host sessions produced zero calls; that is an
   anecdote from an earlier Apple-host session, not a reproducible measurement in this repository, and it says nothing
   about Android or other content.

## Baseline: what the four entries are today

| Entry | Static call sites | Slot | First call site (as recorded) | Current behaviour |
|---|---:|---|---:|---|
| `nvnCommandBufferSetPatchSize` | 1 | `320f548` | `1746780` | red stub |
| `nvnCommandBufferMultiDrawArraysIndirectCount` | 2 | `320f5c8` | `1747614` | red stub |
| `nvnCommandBufferMultiDrawElementsIndirectCount` | 2 | `320f5d0` | `1747784` | red stub |
| `nvnCommandBufferDownsampleTextureView` | 2 | `320f780` | `1745e54` | red stub |

Source: `dm/gfx/nvn/nvn_slots.tsv:394,410,411,465` — identical to the #11 inventory table
(`docs/android/nvn-inventory.md:56-66`). These counts are **static call sites in `main`**, not call counts at run time.

"Red stub" has an exact meaning here: `nvn_unimpl_c()` (`dm/gfx/nvn/hle_nvn.c:59-63`) logs
`【红】NVN 未实现被调用：<name> (x0=… x1=… x2=…)` on the **first** call of that entry (a per-entry `seen` flag), writes
nothing, and returns 0 to the caller of the thunk. It is loud once per entry, not fatal, and it never applies the
command's effect.

The four entries are deliberately **not** in the `g_nops` list (`hle_nvn.c:935-955`); the comment at `hle_nvn.c:929-933`
explains that they were kept as red stubs precisely so a real call cannot pass silently.

### Documentation drift found while checking this (not edited by this task)

- `dm/gfx/nvn_metal_mapping.md:54` and `dm/gfx/README.md:183` list `SetPatchSize`, `MultiDraw*IndirectCount` and
  `DownsampleTextureView` among the **empty implementations** (`g_nops`). They are not in `g_nops` and never were after
  the change recorded at `hle_nvn.c:929-966`: they are red stubs. The mapping/README sentence is stale.
- `docs/android/nvn-inventory.md:62-65` describes the risk as "silently skipped / wrong pixels rather than a crash".
  That understates the current behaviour in one direction and overstates it in another: the first call is logged red,
  and no work is recorded, so a *reachable* call is visible once in the log but still produces no pixels.

## Surface facts the repository does establish

- Draw primitives include patches: `NVN_DRAW_PRIMITIVE_PATCHES = 0xE` (`nvn.h:735`); index types are
  `UNSIGNED_SHORT = 0x1`, `UNSIGNED_INT = 0x2` (`nvn.h:742-743`).
- Device queries relevant to these entries exist: `NVN_DEVICE_INFO_INDIRECT_DRAW_ALIGNMENT = 0xD` (`nvn.h:245`) and
  `NVN_DEVICE_INFO_MAX_PATCH_SIZE = 0x4F` (`nvn.h:311`).
- `NVNtextureView` is a **0x28-byte value type** (`nvn.h:127`) with setters for levels, layers, format, swizzle,
  depth-stencil mode and target (`nvn_protos.h:167-179`). This fork mirrors those fields in `view_t` and statically
  asserts it fits (`dm/gfx/nvn/nvn_obj.h:39`); command recording copies the fields rather than holding caller pointers.
- The downsample family has four members: `Downsample`, `TiledDownsample`, `DownsampleTextureView`,
  `TiledDownsampleTextureView` (`nvn.h:1686-1694`). Only the two view variants have static call sites; the tiled
  variants have none in `nvn_slots.tsv`.
- There is **no** implementation of any of these four in `hle_nvn.c` or `nvn_mtl.m`, and no HLE reference inside the
  bundled translator sources.

## Entry 1 — `nvnCommandBufferSetPatchSize` (child: #53)

- **Signature**: `void(nvnCommandBuffer*, int)` (`nvn_protos.h:368`; same public prototype `nvn.h:1573`).
- **Data units**: the integer is taken to be *vertices per patch*, i.e. the analogue of GL `GL_PATCH_VERTICES`
  ([glPatchParameter](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glPatchParameter.xhtml)); the repository
  does not prove this equivalence. Candidate range is `1..NVN_DEVICE_INFO_MAX_PATCH_SIZE`, which is a query that exists
  (`nvn.h:311`); NVN's own validation and error behaviour are **blocked**.
- **Ownership / lifetime**: no pointers, no GPU address; state is recorded into the command stream and would be read at
  replay. Whether the value is per command buffer, per draw, or reset by render-target changes is **blocked**.
- **Feature / limit queries**: `NVN_DEVICE_INFO_MAX_PATCH_SIZE`; on the Vulkan side `tessellationShader` and
  `maxTessellationPatchSize`. The current translator contract only models `vs/ps/cs`
  (`dm/gfx/tools/dmsh_spirv_contract.py:39`) with target ABI `vulkan1.1` (`:44`), and the Android backend has no
  hull/domain pipeline — so a truthful implementation is **blocked on tessellation stages**, not on this entry alone.
- **Proposed mapping (hypothesis)**: record the value; set `VkPipelineTessellationStateCreateInfo::patchControlPoints`
  for pipelines with `PATCHES` topology. Not adopted.
- **Unsupported behaviour**: red diagnostic naming the entry, the requested value and the missing capability; no state
  change, and a following `PATCHES` draw must never silently use a default.
- **Owned tests**: recorder-level positive (value 4 observed in the record / pipeline descriptor), negatives (0,
  negative, above the queried maximum, feature off), and a discrimination case where a following `PATCHES` draw must
  fail rather than draw with an assumed default. No device needed.
- **Reachability note**: one static call site, while `SetInner/OuterTessellationLevels` have **zero** static call sites
  (`nvn_slots.tsv:395-396`). Whether the single call is setup-only, and whether patches are ever drawn, is unknown.

## Entry 2 — `nvnCommandBufferMultiDrawArraysIndirectCount` (child: #54)

- **Signature**: `void(cmd, NVNdrawPrimitive, NVNbufferAddress indirect, NVNbufferAddress drawCount,
  int maxDrawCount, ptrdiff_t stride)` (`nvn_protos.h:384`).
- **Data units**: `indirect` is a GPU address of an array of 16-byte elements
  `{count, instanceCount, first, baseInstance}`; `drawCount` is a GPU address of one 32-bit count; `stride` is in bytes
  between elements; `maxDrawCount` clamps. The element layout is the OpenGL 4.6 counterpart
  ([glMultiDrawArraysIndirectCount](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glMultiDrawArraysIndirectCount.xhtml )),
  and the NVN equivalence is a **hypothesis**: whether NVN's `drawCount` is an address (not a byte offset into a bound
  parameter buffer), whether the count is read on the GPU, and whether `stride = 0` means "tightly packed" are all
  **blocked**.
- **Ownership / address bounds**: buffers belong to the game; the GPU address is the CPU address in this fork
  (`hle_nvn.c:16`). The implementation must validate `indirect` against `INDIRECT_DRAW_ALIGNMENT` (`nvn.h:245`),
  `stride` (multiple of 4, at least one element when non-zero) and the 4-byte alignment of `drawCount`. What NVN does
  with an out-of-range `count` is **blocked**; the stub must not invent a clamp.
- **Feature / limit queries**: Vulkan `VkPhysicalDeviceVulkan12Features::drawIndirectCount` or
  `VK_KHR_draw_indirect_count`, `VkPhysicalDeviceLimits::maxDrawIndirectCount`, and `multiDrawIndirect`
  ([Vulkan spec, feature structs](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#VkPhysicalDeviceVulkan12Features)).
  The repository's consumer ABI is `vulkan1.1`, so this must be gated explicitly; no device is assumed.
- **Proposed mapping (hypothesis)**: `vkCmdDrawIndirectCount` with the indirect buffer, count buffer, `maxDrawCount`
  and `stride`. A CPU-readback fallback changes semantics (the count is known on the GPU) and is only acceptable with
  explicit evidence that the buffer is host-visible plus the necessary barrier; otherwise red.
- **Unsupported behaviour**: red diagnostic with entry, required feature, stride and maxDrawCount; zero draws recorded.
- **Owned tests**: count=2 draws both triangles (independently derived pixel colours), count=1 draws only the first,
  count above `maxDrawCount` truncates, stride 16 vs 32 agree, stride/misalignment negatives, feature-off negative, and
  a discrimination case where treating the count as a CPU constant fails on count=1.

## Entry 3 — `nvnCommandBufferMultiDrawElementsIndirectCount` (child: #55)

- **Signature**: adds `NVNindexType` and an explicit index-buffer GPU address:
  `void(cmd, NVNdrawPrimitive, NVNindexType, NVNbufferAddress indirect, NVNbufferAddress drawCount,
  NVNbufferAddress indices, int maxDrawCount, ptrdiff_t stride)` (`nvn_protos.h:385`).
- **Data units**: 20-byte elements `{count, instanceCount, firstIndex, baseVertex, baseInstance}`
  ([glMultiDrawElementsIndirectCount](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glMultiDrawElementsIndirectCount.xhtml ));
  NVN passes the index buffer as an address instead of relying on a bound element buffer, which is an ABI difference
  that must not be flattened to the GL shape. `UNSIGNED_SHORT`/`UNSIGNED_INT` map to `VK_INDEX_TYPE_UINT16`/`UINT32`
  (`nvn.h:742-743`); NVN's index-address alignment and `baseVertex` negative handling are **blocked**.
- **Ownership / bounds**: all three addresses belong to the game; the same alignment/stride rules as entry 2, plus
  `count × index width` staying inside the index buffer — NVN's own bounds checks are **blocked**.
- **Feature / limit queries**: same as entry 2, plus index type support and index buffer offset alignment.
- **Proposed mapping (hypothesis)**: `vkCmdBindIndexBuffer(indices, 0, type)` + `vkCmdDrawIndexedIndirectCount`.
- **Unsupported behaviour / tests**: same shape as entry 2; the discriminating negative must show that ignoring the
  passed `indices` address (using a previously bound buffer) produces wrong pixels.

## Entry 4 — `nvnCommandBufferDownsampleTextureView` (child: #56, **blocked on semantics**)

- **Signature**: `void(cmd, const NVNtexture *src, const NVNtextureView *srcView, const NVNtexture *dst,
  const NVNtextureView *dstView)` (`nvn_protos.h:439`).
- **What is proven**: the view type is a 0x28-byte value with levels/layers/format/swizzle/target setters
  (`nvn.h:127`, `nvn_protos.h:167-179`), mirrored by `view_t` (`nvn_obj.h:39`); recording must copy those fields.
- **What is not proven**: the sampling rule (point / 2×2 average / sRGB-decoded average), the required size relation
  between src and dst, format conversion and depth/stencil handling, aliasing between src and dst views, and the
  required ordering relative to render passes. The name only says "downsample", and the sibling `Downsample` /
  `TiledDownsample` entries have no static call sites to compare against.
- **Why no mapping is proposed**: `vkCmdBlitImage` (nearest/linear), `vkCmdCopyImage`, and a compute reduction all fit
  the name and differ in pixels; choosing one without evidence would be inventing protocol. No public NVN
  specification is available to cite, and no HLE reference exists in the tree.
- **Unsupported behaviour**: a call must produce an explicit "semantics unestablished (G0.4)" red diagnostic and no GPU
  work — not a no-op that looks like success.
- **Owned tests now**: assert the diagnostic and that pixels are unchanged across repeated calls. Pixel expectations are
  deliberately deferred until the sampling rule is established from a permitted instrumented run.

## What remains blocked (and what would unblock it)

| Question | Blocking evidence needed |
|---|---|
| Do any of the four call sites execute at run time? | permitted instrumented run with per-entry call counts; static sites alone prove nothing |
| NVN's argument units/validation for the two count variants | SDK-visible documentation or captured argument behaviour across content |
| Whether `SetPatchSize` is meaningful without tessellation stages | shader inventory showing patch/tess stages, plus the argument value from a captured call |
| `DownsampleTextureView` sampling rule and size/format constraints | captured arguments (formats, levels, layers, dimensions) and output comparison |

## Reproduce the static facts

```
python3 dm/gfx/tools/nvn_inventory.py --check     # exits 1 today: exactly these four static-called red stubs
awk -F'\t' '$1 ~ /SetPatchSize|MultiDraw(Arrays|Elements)IndirectCount|DownsampleTextureView/ {print $1,$3,$4}' \
    dm/gfx/nvn/nvn_slots.tsv                      # the four rows quoted above
```

`--check` exiting 1 is the intended state: it is an inventory completeness signal, not a test failure to be hidden.
