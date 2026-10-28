# D2R Mobile — Android customization

中文：[README.zh-CN.md](README.zh-CN.md)

An Android customization of [jhaizhou-ops/d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile). We follow upstream's game runtime, NVN-to-Vulkan renderer and touch interface, maintaining a small set of Android additions. MIT license; upstream and contributor attribution is retained. **Bring your own matching Switch game files**: no game content is distributed here. See [game files](docs/GAME_FILES.md).

## Baseline

This development branch syncs upstream `1e0e7934` (Android 2.1.0); device validation is pending. `main` remains the upstream-based Android customization. It replaces the previous independent Android implementation.

The old source is preserved at [`archive/android-legacy-20261023`](https://github.com/n2ray/d2r-mobile/tree/archive/android-legacy-20261023), commit `0fa019b02336af144a761668fd4728d8cde6b693`, for reference and rollback, not continued development. [Legacy issue/PR archive](docs/android/LEGACY_ARCHIVE_20261023.md). Closed legacy tasks mean a change of direction, not completed acceptance. Git history remains reachable; legacy source is excluded from the active tree.

## Additions

| Addition | Behavior |
| --- | --- |
| Startup settings | Persistent driver, aspect and optional simple FPS |
| Save backups | Startup-only ZIP export/import through the system picker; validated restore with a local pre-import backup (device validation pending) |
| Camera | Upstream optional shoulder and first-person modes; device validation pending |
| Driver | Bundled Turnip by default; selectable system Vulkan |
| Aspect | Centered 16:9 at 1280×720, or aspect-aware fullscreen at height 720 |
| FPS | Submitted frames; not a guarantee of evenly spaced scanout |
| Power saving | Honors upstream 30 FPS mode; [upstream PR #128](https://github.com/jhaizhou-ops/d2r-mobile/pull/128) |
| Controllers | Integrates [upstream #130](https://github.com/jhaizhou-ops/d2r-mobile/pull/130), with startup/lifecycle input fixes |
| Android Auto | Parked-game entry; [draft #131](https://github.com/jhaizhou-ops/d2r-mobile/pull/131), vehicle testing pending |

Details: [Android customization notes](dm/android/ANDROID_CUSTOMIZATIONS.md). Old-app features such as arbitrary driver ZIP import/delete and extended diagnostics must not be assumed to exist here.

Latest input fixes preserve the player's fullscreen-map choice after panels and area changes, and release virtual menu-button commands after one game input sample to avoid repeated activation. Gameplay skill holds and item drag/hold gestures retain their existing behavior. APK build and local regressions are separate from pending phone/car acceptance for these fixes.

## Device evidence

Test phone: **Xiaomi 15 Ultra / Snapdragon 8 Elite / Adreno 830 / Android 16**. Upstream-based Turnip build reached character selection and camp with normal images in the inspected scene. A 94-second stationary-camp run with Game Turbo had median submitted FPS 60.00, per-second range 56.69–61.09 and some longer frame intervals. This does not establish sustained 60 FPS in all scenes or power efficiency.

Physical-controller and Android Auto acceptance remain pending. Android Auto needs Android 15+ and parked-game support; local builds use Android Auto developer settings → Unknown sources.

## Build and update

Follow [upstream Android build/import instructions](dm/android/README.md), plus the trusted external driver inputs in [ANDROID_CUSTOMIZATIONS.md](dm/android/ANDROID_CUSTOMIZATIONS.md). Driver binaries and generated game content stay outside Git. This is a source fork, not a bundled game download.

The installed customization uses `org.d2rmobile.upstream.test` (`-PdmAppId=org.d2rmobile.upstream.test`). Preserve this application ID and signing key for upgrades retaining private settings/saves. Old and new apps have separate saves and do not automatically synchronize; back up before migration or uninstall. Ordinary APK updates need no reimport unless the upstream game/overlay contract changes.

## Maintenance

Track the original repository as `upstream`. Review updates on short-lived branches from our `main`, preserve customization commits and preference keys, then build and test relevant paths. Do not overwrite the fork wholesale or merge the legacy renderer back. Propose generic improvements upstream; personal settings may remain in this fork.
