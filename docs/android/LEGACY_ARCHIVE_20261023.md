# Legacy Android archive / 旧 Android 实现归档

2026-10-23: the owner chose upstream-based Android plus small customizations as the only active implementation. `main` now follows that direction. Legacy source/history remain at [`archive/android-legacy-20261023`](https://github.com/n2ray/d2r-mobile/tree/archive/android-legacy-20261023), commit `0fa019b02336af144a761668fd4728d8cde6b693`. A history-only merge retains the old main ancestry without mixing its source into the active tree; no force-push is required.

用户决定后续只维护上游基础版及少量定制。旧问题按停止该路线归档关闭，不代表已修复或完成验收。原正文和讨论保留在 GitHub；若问题在新基线复现，新建 issue 并引用旧记录。

## Issues

| Record | Legacy scope |
| --- | --- |
| [#12](https://github.com/n2ray/d2r-mobile/issues/12) | 着色器覆盖、缓存和 Adreno 回归 |
| [#13](https://github.com/n2ray/d2r-mobile/issues/13) | 图形资源生命周期、别名及失败恢复 |
| [#22](https://github.com/n2ray/d2r-mobile/issues/22) | 游戏后台恢复、异常退出及流程 |
| [#23](https://github.com/n2ray/d2r-mobile/issues/23) | 旧实现60帧性能及30帧能效 |
| [#24](https://github.com/n2ray/d2r-mobile/issues/24) | 旧实现构建、安装及交付 |
| [#38](https://github.com/n2ray/d2r-mobile/issues/38) | 文字输入回归 |
| [#40](https://github.com/n2ray/d2r-mobile/issues/40) | 组合触控、交易和手柄回归 |
| [#42](https://github.com/n2ray/d2r-mobile/issues/42) | Vulkan 合规及失败路径 |
| [#53](https://github.com/n2ray/d2r-mobile/issues/53) | SetPatchSize 兼容储备 |
| [#54](https://github.com/n2ray/d2r-mobile/issues/54) | MultiDrawArraysIndirectCount 兼容储备 |
| [#55](https://github.com/n2ray/d2r-mobile/issues/55) | MultiDrawElementsIndirectCount 兼容储备 |
| [#56](https://github.com/n2ray/d2r-mobile/issues/56) | DownsampleTextureView 兼容储备 |
| [#302](https://github.com/n2ray/d2r-mobile/issues/302) | 旧触屏 HUD、技能状态及安全区 |
| [#360](https://github.com/n2ray/d2r-mobile/issues/360) | 旧 Android 自动增益接线 |
| [#361](https://github.com/n2ray/d2r-mobile/issues/361) | 旧 Android 换组位置接线 |

## Unmerged PRs / 未合并 PR

Closed without merge because they target the retired implementation. Source branches remain available; no new-baseline equivalence or device acceptance is claimed.

| PR | Preserved branch | Exact head |
| --- | --- | --- |
| [#362](https://github.com/n2ray/d2r-mobile/pull/362) | `hermes/android-autobuff-360` | `283f80957291e27780e2899a815cd9bac6422ee3` |
| [#363](https://github.com/n2ray/d2r-mobile/pull/363) | `hermes/android-swap-above-361` | `4bd6c07f1374c740d4f7b881e83beba56c19c534` |

## Upstream contributions / 上游贡献

Upstream [#128](https://github.com/jhaizhou-ops/d2r-mobile/pull/128) (30 FPS pacing) and draft [#131](https://github.com/jhaizhou-ops/d2r-mobile/pull/131) (Android Auto parked entry) are separate contributions and remain open. Controller integration comes from upstream [#130](https://github.com/jhaizhou-ops/d2r-mobile/pull/130). Vehicle and physical-controller acceptance remain pending.
