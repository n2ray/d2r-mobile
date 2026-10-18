# D2R Mobile — Android fork

中文：[README.zh-CN.md](README.zh-CN.md)

**Android is not playable yet.** This fork targets smooth offline Diablo II: Resurrected play on **Xiaomi 15 Ultra / Snapdragon 8 Elite, Android ARM64**, with the complete game-specific touch system from [jhaizhou-ops/d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) (called “r2d” in this project's goals). Sustained **60 fps at normal quality is a target awaiting measurement**, not an achieved result or a delivery promise.

The upstream project is by **jhaizhou** and is licensed under [MIT](LICENSE). Its Apple gameplay, 60 fps, power and loading-time results belong to upstream Apple builds. They do not establish Android compatibility or performance. The [upstream Apple introduction and instructions](https://github.com/jhaizhou-ops/d2r-mobile/blob/21cf97b91823111ead237858ea79929afacf71a5/README.md) remain available at the source baseline used by this fork; the [current upstream README](https://github.com/jhaizhou-ops/d2r-mobile#readme) may have changed since then.

## Current evidence and gaps

This summary describes the merged baseline reviewed for this documentation update (2026-10-18). Follow [the project checklist #1](https://github.com/n2ray/d2r-mobile/issues/1) and linked issues for later changes. A merged implementation slice does not complete its parent task; open PRs are pending review and acceptance.

| Area | What exists | What remains unverified or incomplete |
|---|---|---|
| Runtime and services | Offline module inspection/rewriting, owned runtime fixtures, bounded IPC and resource/toolchain foundations | Production synchronization/preemption, TLR/SVC36 contracts and strict admission remain open in [#3](https://github.com/n2ray/d2r-mobile/issues/3), [#4](https://github.com/n2ray/d2r-mobile/issues/4), [#5](https://github.com/n2ray/d2r-mobile/issues/5); full controlled SDK initialization and normal exit require [#10](https://github.com/n2ray/d2r-mobile/issues/10) |
| Restricted SDK diagnostic | A separate restricted macOS diagnostic returned from `_init_libc0` and one `nnosInitialize` stage with cleanup | Production preparation still rejects the original SDK input. A private diagnostic quarantine is not production admission, complete SDK startup or Android game execution |
| Graphics | Synthetic Vulkan compute/draw/readback checks; RGBA8 UNORM/SRGB resource slices; shader contract checks ([PR #47](https://github.com/n2ray/d2r-mobile/pull/47)) | Complete NVN → Vulkan resources, shader reflection/binding, drawing and continuous presentation: [#12](https://github.com/n2ray/d2r-mobile/issues/12), [#13](https://github.com/n2ray/d2r-mobile/issues/13), [#18](https://github.com/n2ray/d2r-mobile/issues/18). The shader build contract gate is opt-in (`DMSH_CONTRACT=1`), not proof of a complete Android shader path |
| Touch | Upstream portable C gesture core reused with Android pointer-ID/timing adapter and owned tests in merged [PR #46](https://github.com/n2ray/d2r-mobile/pull/46) | Real NativeActivity event delivery, game-state/hit/HID bridges, HUD, IME and whole-device touch acceptance remain open; see below |
| Device diagnostic | An earlier synthetic APK passed ARM64 gate, Vulkan pixel and window-post checks on the Xiaomi phone; details in [the diagnostic guide](dm/android/app/README.md) | Later runtime changes need new device evidence. These fixtures do not validate game scenes, full lifecycle, audio, saves or playable delivery |

Keep strict scanning, quarantine boundaries, W^X, ownership and failure rollback intact. Do not replace missing contracts or services with success stubs. Unknown gaps may lead to more tasks; there is no evidence-based completion percentage or completion date.

## The complete touch target

The goal is the upstream D2R interaction system, including its game state and layout behavior. A generic virtual gamepad does not satisfy this goal. The [touch parity matrix](docs/android/touch-parity.md), delivered by [#31](https://github.com/n2ray/d2r-mobile/issues/31), freezes source behavior and dependencies; it is an inventory, not completed Android functionality. Its gap columns describe that frozen checkpoint; PR #46 supplies a later adapter slice.

| Work | Tracking |
|---|---|
| Pointer ownership, multi-touch timing, button pulses, cancellation and focus loss | [#32](https://github.com/n2ray/d2r-mobile/issues/32) |
| Game/scene identity, panel state, UI hit testing and actual guest HID integration | [#33](https://github.com/n2ray/d2r-mobile/issues/33), parent [#19](https://github.com/n2ray/d2r-mobile/issues/19) |
| Vulkan HUD, layout, density, safe areas and user-derived art | [#34](https://github.com/n2ray/d2r-mobile/issues/34) |
| Movement, drag-to-aim, two skill groups/ZL timing, player and mercenary belts | [#35](https://github.com/n2ray/d2r-mobile/issues/35) |
| Item tap/double-tap/long-press/drag/drop; shop, stash and cube operations | [#36](https://github.com/n2ray/d2r-mobile/issues/36) |
| World interactions, menus, navigation and custom pages | [#37](https://github.com/n2ray/d2r-mobile/issues/37) |
| System keyboard, Chinese/English IME and composition handling | [#38](https://github.com/n2ray/d2r-mobile/issues/38) |
| Settings, persistent layout and touch/controller handoff | [#39](https://github.com/n2ray/d2r-mobile/issues/39) |
| Complete Xiaomi 15 Ultra touch parity and touch-to-picture latency acceptance | [#40](https://github.com/n2ray/d2r-mobile/issues/40) |

## Start developing without game files

Run from the repository root on an ARM64 development host. See [the Android developer guide](dm/android/README.md) for dependencies, offline dependency preparation, output overrides and historical evidence. Set `DM_ROOT` explicitly to an external-disk workspace (T2 in the maintainers' setup), especially in a worktree; the default is computed relative to the checkout and may point somewhere else.

```sh
# Replace this example with your external-disk workspace.
export DM_ROOT=/path/to/external-disk/d2r-work

# Owned host fixtures; no game files. Disable GPU dependencies for this CPU entry.
./dm/android/test.sh -DDM_ANDROID_VULKAN=OFF

# Cross-compile Android ARM64 tools; requires an installed NDK, no phone execution.
./dm/android/build.sh -DDM_ANDROID_VULKAN=OFF
```

CMake 3.22+, a C compiler and Capstone 5 are needed for the host entry; native execution cases require ARM64. Install NDK r28+ (r29 is the default lookup) for cross-compilation. The first configuration fetches pinned LZ4/Mbed TLS dependencies unless prepared locally; “offline” describes game-free local development, not an automatic air-gapped bootstrap.

Building the [synthetic diagnostic APK](dm/android/app/README.md) is a separate step:

```sh
# Requires GPU tooling plus Android SDK platform/build-tools 35, JDK/keytool and zip.
bash dm/android/app/build.sh
```

This builds a debug-signed engineering APK with owned fixtures. It does not install or launch it, and it does not build a playable game. `setup.sh` prepares Apple artifacts and is not the Android entry point.

## Game files, acceptance and contribution

Use your own legally owned Switch release **1.0.34.0 with Reign of the Warlock** (repository version **3.3.94012**). [Game-file preparation](docs/GAME_FILES.md) and [Android storage/checking instructions](dm/android/README.md#storage-and-game-files) explain the inputs. Already-exported ExeFS/RomFS need no keys. No game content is supplied by this repository.

Before a playable delivery, the project still needs safe startup/shutdown, full rendering, actual HID/touch, audio, durable saves, resource import and lifecycle integration, then real game/device acceptance. [#23](https://github.com/n2ray/d2r-mobile/issues/23) defines sustained-performance measurement: reproducible town/combat/traversal scenes, cold and at least 30-minute warm runs, frame-time distributions, thermal/power conditions and touch-to-picture latency. Numerical acceptance thresholds follow a measured device baseline. [#22](https://github.com/n2ray/d2r-mobile/issues/22) tracks full game/lifecycle acceptance; [#24](https://github.com/n2ray/d2r-mobile/issues/24) tracks installable delivery and known limitations. An installable diagnostic is not that delivery.

Contributors should start from [#1](https://github.com/n2ray/d2r-mobile/issues/1), claim an issue with a bounded scope, work on a topic branch, and submit reproducible evidence for independent review. Distinguish design, code, owned-fixture, SDK and device/game acceptance. Keep large files, builds and caches in the external workspace. Do not publish games, keys, saves, credentials, private paths/IPs or raw local logs; share only reviewed, sanitized diagnostics. General upstream guidance is in [CONTRIBUTING.md](CONTRIBUTING.md); its Apple commands are not Android acceptance commands.

## Source, credits and license

This fork retains upstream Apple code and documentation for reference and reuse: [architecture](dm/README.md), [graphics](dm/gfx/README.md), [iOS shell](dm/ios/README.md) and [verification](dm/verify/README.md). Those guides describe their respective platforms and do not certify Android support.

Credit to **jhaizhou** and the [upstream contributors](https://github.com/jhaizhou-ops/d2r-mobile/graphs/contributors). The [upstream acknowledgements](https://github.com/jhaizhou-ops/d2r-mobile/blob/21cf97b91823111ead237858ea79929afacf71a5/README.md#acknowledgements) and [dependency/license inventory](docs/DEPENDENCIES.md) credit Ryujinx, deko3d, nvn.h, libdeflate, bcdec, FLIP and other tools and community work. See also [derived interface information](docs/DERIVED.md). The repository remains [MIT licensed, copyright (c) 2026 jhaizhou](LICENSE); bundled third-party licenses remain in their directories.

Diablo and Blizzard Entertainment are Blizzard Entertainment trademarks; Nintendo Switch is a Nintendo trademark. This is an independent, non-commercial project, unaffiliated with and not endorsed or sponsored by those companies.
