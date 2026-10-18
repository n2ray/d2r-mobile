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
- 自己拥有 `VkSwapchainKHR`、render pass、image views、framebuffers、command buffers（池仍由 `dm_vk` 拥有）、两个 semaphore 和一个 fence；`VkImage` 由驱动拥有，仅保存句柄。
- `dm_vk_swapchain_destroy()` 先 `vkDeviceWaitIdle`，再按 fence/semaphore → command buffers → framebuffers → views → render pass → swapchain 的顺序销毁；可重复调用。

## 帧同步

- 每帧 `acquire` 使用 `image_available` semaphore；submit 等待它并 signal `render_finished`，同时 signal fence。
- 下一次 acquire 先 `vkWaitForFences`，再 `vkQueueWaitIdle`，然后才复用二进制 semaphore；`vkQueueWaitIdle` 是保守地等待 present 的 wait 完成，避免在 present 仍可能挂起时复用 `render_finished`。
- fence 只证明 submit 完成，不证明 presentation 完成；实现和测试都不把 fence 当作 present 完成证据。

## OUT_OF_DATE / SUBOPTIMAL / 重建

- `acquire` 返回 `VK_ERROR_OUT_OF_DATE_KHR` 时不产生图像、释放任何 in-flight 对象，仅置 `needs_recreate`。
- `acquire` 返回 `VK_SUBOPTIMAL_KHR` 时仍返回可用图像并设置 `suboptimal`，调用者可以先 present 再重建。
- `present` 返回 `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR` 时保留所有对象直到显式 `destroy`/`recreate`；不声称旧链仍可继续回滚使用。
- `dm_vk_swapchain_recreate()` 先 `vkDeviceWaitIdle`，把旧 handle 作为 `oldSwapchain` 创建新链；成功后销毁旧对象。新链创建失败时也按 fail-closed 销毁旧对象，不提供“旧链接管”假象。

## 失败与测试边界

`dm/android/tests/vulkan_swapchain.c` 通过注入 Vulkan API seam 驱动实际生产函数，覆盖 3 个 owned 帧、重建、capability/FIFO 拒绝、每类关键创建/提交/呈现失败、OUT_OF_DATE/SUBOPTIMAL、清理顺序与计数。

host seam 不证明真实 Android 窗口、Adreno 设备、GPU fence/present 完成、像素内容或帧率；这些仍需 NativeActivity 接入和手机实测，父 #18 保持开放。
