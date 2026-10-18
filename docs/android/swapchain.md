# #141 自有 VK_KHR_swapchain 连续帧边界

## 范围

`dm_vk_swapchain_*` 是唯一有界的 WSI 切片：在 #139 的 `dm_vk_create_present()` 上下文上创建真实 `VK_KHR_swapchain`，执行 `acquire → clear command submit → present`，并显式处理重建、同步和失败退役。它不是完整渲染器，不含 NVN 游戏命令、HUD 合成、性能调优或 NativeActivity 接入。

## 协商与所有权

- 通过 `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`、`vkGetPhysicalDeviceSurfaceFormatsKHR`、`vkGetPhysicalDeviceSurfacePresentModesKHR` 与 `vkGetPhysicalDeviceFormatProperties` 协商：
  - 仅接受 `VK_PRESENT_MODE_FIFO_KHR`；
  - `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` 必须受支持；
  - 固定 `currentExtent` 直接采用；未定义时把请求尺寸夹到 `min/maxImageExtent`；
  - 优先 `VK_FORMAT_B8G8R8A8_UNORM`，其次 `VK_FORMAT_R8G8B8A8_UNORM`，再退到首个支持 color attachment 的格式；
  - image count 使用 `minImageCount`，并检查 `maxImageCount`。
- 必须由 `dm_vk_create_present()` 协商并启用 `VK_EXT_swapchain_maintenance1` 的 `swapchainMaintenance1` feature；缺失时 swapchain 创建 fail-closed，不把扩展字符串可用当成 feature 已启用。
- 自己拥有 `VkSwapchainKHR`、render pass、image views、framebuffers、command buffers（池仍由 `dm_vk` 拥有）、每 image 一个 acquire/present semaphore、每 image 一个 present fence，加一个 submit fence；`VkImage` 由驱动拥有，仅保存句柄。
- `dm_vk_swapchain_destroy()` 先等待每个 in-flight present fence（以及 submit fence），确认 presentation 完成后才按 fence/semaphore → command buffers → framebuffers → views → render pass → swapchain 的顺序销毁；任一证明失败则返回错误并保留全部对象，不静默释放。

## 帧同步

- 每帧 acquire 使用 `image_available` semaphore；submit 等待它并 signal 该 acquired image index 对应的 `render_finished[image]`，同时 signal submit fence。
- 同一 image index 再次 acquire 后，先等待该 image 的 present fence，证明上一轮 presentation（包括 present wait semaphore）已完成，再允许 submit 重新 signal `render_finished[image]`；per-image semaphore + reacquire proof 是 primary guide 的无扩展安全路径。
- 每次 present 通过 `VkSwapchainPresentFenceInfoEXT` 附带 per-image present fence。submit fence 只证明 submit 完成；presentation 完成只由 maintenance1 present fence 证明，绝不以 submit fence 或 `vkQueueWaitIdle` 代替。

## OUT_OF_DATE / SUBOPTIMAL / 重建

- `acquire` 返回 `VK_ERROR_OUT_OF_DATE_KHR` 时不产生图像、释放任何 in-flight 对象，仅置 `needs_recreate`。
- `acquire` 返回 `VK_SUBOPTIMAL_KHR` 时仍返回可用图像并设置 `suboptimal`，调用者可以先 present 再重建。
- `present` 返回 `VK_ERROR_OUT_OF_DATE_KHR` 或其它错误时，presentation 是否已入队无法证明，置 `retire_blocked` 并保留所有对象；后续 destroy/recreate 返回错误而不是靠 device idle 释放。
- `dm_vk_swapchain_recreate()` 先等待所有 in-flight present fences，再把旧 handle 作为 `oldSwapchain` 创建新链；成功后销毁旧对象。新链创建失败时也按 fail-closed 销毁旧对象，不提供“旧链接管”假象；present 证明失败时不销毁。
- 测试覆盖 acquire 顺序 `0,1,2,0`，present fence 故意保持 pending，验证下一次 submit 只能在等待该 image 的 present fence 之后重新 signal 对应 semaphore。

## 失败与测试边界

`dm/android/tests/vulkan_swapchain.c` 通过注入 Vulkan API seam 驱动实际生产函数，覆盖 4 次 acquire（`0,1,2,0`）、延迟 presentation、重建、capability/FIFO 拒绝、每类关键创建/提交/呈现失败、OUT_OF_DATE/SUBOPTIMAL、清理顺序与计数。

host seam 不证明真实 Android 窗口、Adreno 设备、GPU fence/present 完成、像素内容或帧率；这些仍需 NativeActivity 接入和手机实测，父 #18 保持开放。
