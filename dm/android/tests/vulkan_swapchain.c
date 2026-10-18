// #141 owned swapchain API seam: actual dm_vk_swapchain_* production path, mocked Vulkan loader boundaries only.
// The seam exercises 3 frames, recreate, OUT_OF_DATE/SUBOPTIMAL, creation failures and exact cleanup order.
#include "../vulkan/vk_swapchain.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define H(t, v) ((t)(uintptr_t)(v))

static VkSurfaceCapabilitiesKHR g_caps;
static VkSurfaceFormatKHR g_formats[2];
static VkPresentModeKHR g_modes[2];
static int g_mode_count = 1, g_fail_swapchain, g_fail_renderpass, g_fail_view, g_fail_framebuffer;
static int g_fail_commands, g_fail_semaphore, g_fail_fence, g_fail_device_wait;
static VkResult g_acquire_result, g_present_result, g_submit_result;
static uint32_t g_acquire_count, g_submit_count, g_present_count, g_wait_fence_count, g_queue_wait_count;
static VkSwapchainKHR g_old_seen, g_swapchain_handle;
static VkExtent2D g_create_extent;
static char g_order[512]; static unsigned g_order_n;
static uint32_t g_destroy_swapchains, g_free_commands, g_destroy_views, g_destroy_fbs, g_destroy_rp;
static uint32_t g_sem_created, g_fence_created, g_sem_destroyed, g_fence_destroyed;

static void event(char c) { if (g_order_n + 1 < sizeof g_order) { g_order[g_order_n++] = c; g_order[g_order_n] = 0; } }

VKAPI_ATTR VkResult VKAPI_CALL test_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice p, VkSurfaceKHR s, VkSurfaceCapabilitiesKHR *c) {
    (void)p; (void)s; *c = g_caps; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice p, VkSurfaceKHR s, uint32_t *n, VkSurfaceFormatKHR *f) {
    (void)p; (void)s; if (!f) { *n = 2; return VK_SUCCESS; } f[0] = g_formats[0]; f[1] = g_formats[1]; *n = 2; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice p, VkSurfaceKHR s, uint32_t *n, VkPresentModeKHR *m) {
    (void)p; (void)s; if (!m) { *n = (uint32_t)g_mode_count; return VK_SUCCESS; } for (int i = 0; i < g_mode_count; i++) m[i] = g_modes[i]; *n = (uint32_t)g_mode_count; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice p, VkFormat f, VkFormatProperties *o) {
    (void)p; (void)f; memset(o, 0, sizeof *o); o->optimalTilingFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateSwapchainKHR(VkDevice d, const VkSwapchainCreateInfoKHR *i, const VkAllocationCallbacks *a, VkSwapchainKHR *s) {
    (void)d; (void)a; g_old_seen = i->oldSwapchain; g_create_extent = i->imageExtent; CHECK(i->imageUsage == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    CHECK(i->presentMode == VK_PRESENT_MODE_FIFO_KHR); CHECK(i->minImageCount == 3);
    if (g_fail_swapchain) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    g_swapchain_handle = H(VkSwapchainKHR, 0x1000); *s = g_swapchain_handle; event('N'); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkGetSwapchainImagesKHR(VkDevice d, VkSwapchainKHR s, uint32_t *n, VkImage *images) {
    (void)d; (void)s; if (!images) { *n = 3; return VK_SUCCESS; } for (uint32_t i = 0; i < 3; i++) images[i] = H(VkImage, 0x2000 + i); *n = 3; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroySwapchainKHR(VkDevice d, VkSwapchainKHR s, const VkAllocationCallbacks *a) { (void)d; (void)s; (void)a; g_destroy_swapchains++; event('S'); }
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateRenderPass(VkDevice d, const VkRenderPassCreateInfo *i, const VkAllocationCallbacks *a, VkRenderPass *r) {
    (void)d; (void)i; (void)a; if (g_fail_renderpass) return VK_ERROR_OUT_OF_DEVICE_MEMORY; *r = H(VkRenderPass, 0x3000); event('R'); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyRenderPass(VkDevice d, VkRenderPass r, const VkAllocationCallbacks *a) { (void)d; (void)r; (void)a; g_destroy_rp++; event('r'); }
static uint32_t g_view_n;
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateImageView(VkDevice d, const VkImageViewCreateInfo *i, const VkAllocationCallbacks *a, VkImageView *v) {
    (void)d; (void)i; (void)a; if (g_fail_view) return VK_ERROR_OUT_OF_DEVICE_MEMORY; *v = H(VkImageView, 0x4000 + g_view_n++); event('V'); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyImageView(VkDevice d, VkImageView v, const VkAllocationCallbacks *a) { (void)d; (void)v; (void)a; g_destroy_views++; event('v'); }
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateFramebuffer(VkDevice d, const VkFramebufferCreateInfo *i, const VkAllocationCallbacks *a, VkFramebuffer *f) {
    (void)d; (void)i; (void)a; if (g_fail_framebuffer) return VK_ERROR_OUT_OF_DEVICE_MEMORY; *f = H(VkFramebuffer, 0x5000); event('B'); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyFramebuffer(VkDevice d, VkFramebuffer f, const VkAllocationCallbacks *a) { (void)d; (void)f; (void)a; g_destroy_fbs++; event('b'); }
VKAPI_ATTR VkResult VKAPI_CALL test_vkAllocateCommandBuffers(VkDevice d, const VkCommandBufferAllocateInfo *i, VkCommandBuffer *c) {
    (void)d; if (g_fail_commands) return VK_ERROR_OUT_OF_DEVICE_MEMORY; for (uint32_t k = 0; k < i->commandBufferCount; k++) c[k] = H(VkCommandBuffer, 0x6000 + k); event('C'); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkFreeCommandBuffers(VkDevice d, VkCommandPool p, uint32_t n, const VkCommandBuffer *c) { (void)d; (void)p; (void)c; g_free_commands += n; event('f'); }
VKAPI_ATTR VkResult VKAPI_CALL test_vkBeginCommandBuffer(VkCommandBuffer c, const VkCommandBufferBeginInfo *i) { (void)c; (void)i; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL test_vkCmdBeginRenderPass(VkCommandBuffer c, const VkRenderPassBeginInfo *i, VkSubpassContents s) { (void)c; (void)i; (void)s; }
VKAPI_ATTR void VKAPI_CALL test_vkCmdEndRenderPass(VkCommandBuffer c) { (void)c; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkEndCommandBuffer(VkCommandBuffer c) { (void)c; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateSemaphore(VkDevice d, const VkSemaphoreCreateInfo *i, const VkAllocationCallbacks *a, VkSemaphore *s) {
    (void)d; (void)i; (void)a; if (g_fail_semaphore && g_sem_created == 1) return VK_ERROR_OUT_OF_DEVICE_MEMORY; g_sem_created++; *s = H(VkSemaphore, 0x7000 + g_sem_created); event('E'); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroySemaphore(VkDevice d, VkSemaphore s, const VkAllocationCallbacks *a) { (void)d; (void)s; (void)a; g_sem_destroyed++; event('e'); }
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateFence(VkDevice d, const VkFenceCreateInfo *i, const VkAllocationCallbacks *a, VkFence *f) {
    (void)d; (void)i; (void)a; if (g_fail_fence) return VK_ERROR_OUT_OF_DEVICE_MEMORY; g_fence_created++; *f = H(VkFence, 0x8000); event('F'); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyFence(VkDevice d, VkFence f, const VkAllocationCallbacks *a) { (void)d; (void)f; (void)a; g_fence_destroyed++; event('g'); }
VKAPI_ATTR VkResult VKAPI_CALL test_vkWaitForFences(VkDevice d, uint32_t n, const VkFence *f, VkBool32 all, uint64_t t) { (void)d; (void)n; (void)f; (void)all; (void)t; g_wait_fence_count++; event('W'); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkResetFences(VkDevice d, uint32_t n, const VkFence *f) { (void)d; (void)n; (void)f; event('X'); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkQueueWaitIdle(VkQueue q) { (void)q; g_queue_wait_count++; event('Q'); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkDeviceWaitIdle(VkDevice d) { (void)d; if (g_fail_device_wait) return VK_ERROR_DEVICE_LOST; event('D'); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkAcquireNextImageKHR(VkDevice d, VkSwapchainKHR s, uint64_t t, VkSemaphore sem, VkFence f, uint32_t *i) {
    (void)d; (void)s; (void)t; (void)sem; (void)f; g_acquire_count++; event('a');
    if (g_acquire_result == VK_ERROR_OUT_OF_DATE_KHR) return g_acquire_result;
    if (g_acquire_result == VK_SUBOPTIMAL_KHR) { *i = (g_acquire_count - 1) % 3; return g_acquire_result; }
    *i = (g_acquire_count - 1) % 3; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkQueueSubmit(VkQueue q, uint32_t n, const VkSubmitInfo *s, VkFence f) {
    (void)q; (void)n; (void)s; (void)f; g_submit_count++; event('s'); return g_submit_result;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkQueuePresentKHR(VkQueue q, const VkPresentInfoKHR *p) {
    (void)q; (void)p; g_present_count++; event('p');
    if (g_present_result != VK_SUCCESS) return g_present_result;
    return VK_SUCCESS;
}

static void reset(void) {
    memset(&g_caps, 0, sizeof g_caps);
    g_caps.minImageCount = 3; g_caps.maxImageCount = 4;
    g_caps.currentExtent = (VkExtent2D){640, 480};
    g_caps.supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    g_caps.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    g_caps.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    g_formats[0] = (VkSurfaceFormatKHR){VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    g_formats[1] = (VkSurfaceFormatKHR){VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    g_modes[0] = VK_PRESENT_MODE_FIFO_KHR; g_mode_count = 1;
    g_fail_swapchain = g_fail_renderpass = g_fail_view = g_fail_framebuffer = 0;
    g_fail_commands = g_fail_semaphore = g_fail_fence = g_fail_device_wait = 0;
    g_acquire_result = VK_SUCCESS; g_present_result = VK_SUCCESS; g_submit_result = VK_SUCCESS;
    g_acquire_count = g_submit_count = g_present_count = g_wait_fence_count = g_queue_wait_count = 0;
    g_old_seen = g_swapchain_handle = VK_NULL_HANDLE;
    memset(&g_create_extent, 0, sizeof g_create_extent);
    g_order_n = 0; memset(g_order, 0, sizeof g_order);
    g_destroy_swapchains = g_free_commands = g_destroy_views = g_destroy_fbs = g_destroy_rp = 0;
    g_sem_created = g_fence_created = g_sem_destroyed = g_fence_destroyed = 0;
    g_view_n = 0;
}

static dm_vk fake_vk(void) { dm_vk vk = {0}; vk.device = H(VkDevice, 0x200); vk.physical = H(VkPhysicalDevice, 0x300); vk.queue = H(VkQueue, 0x400); vk.pool = H(VkCommandPool, 0x500); vk.surface = H(VkSurfaceKHR, 0x100); return vk; }
static dm_vk_swapchain_options options(void) { dm_vk_swapchain_options o = DM_VK_SWAPCHAIN_OPTIONS_INIT; o.width = 640; o.height = 480; return o; }
static void expect_destroyed(void) {
    CHECK(g_destroy_swapchains == 1); CHECK(g_free_commands == 3); CHECK(g_destroy_views == 3);
    CHECK(g_destroy_fbs == 3); CHECK(g_destroy_rp == 1); CHECK(g_sem_destroyed == 2); CHECK(g_fence_destroyed == 1);
}

int main(void) {
    dm_vk vk = fake_vk(); dm_vk_swapchain_options o = options(); dm_vk_swapchain sc = {0};
    reset(); CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    CHECK(sc.handle == H(VkSwapchainKHR, 0x1000) && sc.image_count == 3);
    CHECK(sc.extent.width == 640 && sc.extent.height == 480 && sc.format == VK_FORMAT_B8G8R8A8_UNORM);
    CHECK(sc.present_mode == VK_PRESENT_MODE_FIFO_KHR && sc.views && sc.framebuffers && sc.commands);
    CHECK(g_sem_created == 2 && g_fence_created == 1 && g_free_commands == 0);

    for (int frame = 0; frame < 3; frame++) {
        int sub = 0;
        CHECK(dm_vk_swapchain_acquire(&vk, &sc, &sub) == VK_SUCCESS && !sub && sc.acquired);
        CHECK(dm_vk_swapchain_submit(&vk, &sc) == VK_SUCCESS && !sc.acquired && sc.submitted);
        CHECK(dm_vk_swapchain_present(&vk, &sc, &sub) == VK_SUCCESS && !sub && !sc.submitted);
    }
    CHECK(g_acquire_count == 3 && g_submit_count == 3 && g_present_count == 3);
    CHECK(g_wait_fence_count == 2 && g_queue_wait_count == 2);
    g_order_n = 0; g_order[0] = 0;
    dm_vk_swapchain_destroy(&vk, &sc); expect_destroyed();
    CHECK(!strcmp(g_order, "DgeefbvbvbvrS"));

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    CHECK(dm_vk_swapchain_recreate(&vk, &sc, &o) == VK_SUCCESS);
    CHECK(g_old_seen == H(VkSwapchainKHR, 0x1000) && sc.handle == H(VkSwapchainKHR, 0x1000));
    CHECK(g_destroy_swapchains == 1 && g_free_commands == 3);
    dm_vk_swapchain_destroy(&vk, &sc); CHECK(g_destroy_swapchains == 2);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    g_fail_swapchain = 1;
    CHECK(dm_vk_swapchain_recreate(&vk, &sc, &o) != VK_SUCCESS);
    CHECK(!sc.handle && g_destroy_swapchains == 1 && g_free_commands == 3);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    CHECK(dm_vk_swapchain_acquire(&vk, &sc, NULL) == VK_SUCCESS);
    CHECK(dm_vk_swapchain_submit(&vk, &sc) == VK_SUCCESS);
    g_present_result = VK_ERROR_OUT_OF_DATE_KHR;
    CHECK(dm_vk_swapchain_present(&vk, &sc, NULL) == VK_ERROR_OUT_OF_DATE_KHR);
    CHECK(sc.needs_recreate && sc.handle == H(VkSwapchainKHR, 0x1000) && g_destroy_swapchains == 0);
    dm_vk_swapchain_destroy(&vk, &sc); CHECK(g_destroy_swapchains == 1);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    g_acquire_result = VK_SUBOPTIMAL_KHR; int sub = 0;
    CHECK(dm_vk_swapchain_acquire(&vk, &sc, &sub) == VK_SUBOPTIMAL_KHR && sub && sc.acquired);
    g_acquire_result = VK_SUCCESS;
    CHECK(dm_vk_swapchain_submit(&vk, &sc) == VK_SUCCESS);
    CHECK(dm_vk_swapchain_present(&vk, &sc, &sub) == VK_SUCCESS);
    dm_vk_swapchain_destroy(&vk, &sc); expect_destroyed();

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    g_acquire_result = VK_ERROR_OUT_OF_DATE_KHR;
    CHECK(dm_vk_swapchain_acquire(&vk, &sc, NULL) == VK_ERROR_OUT_OF_DATE_KHR);
    CHECK(sc.needs_recreate && !sc.acquired && g_acquire_count == 1 && g_destroy_swapchains == 0);
    dm_vk_swapchain_destroy(&vk, &sc); CHECK(g_destroy_swapchains == 1);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0}; g_caps.supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_ERROR_FEATURE_NOT_PRESENT);
    CHECK(!sc.handle && g_destroy_swapchains == 0);

    // Undefined currentExtent: requested size is clamped to min/maxImageExtent.
    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0};
    g_caps.currentExtent = (VkExtent2D){UINT32_MAX, UINT32_MAX};
    g_caps.minImageExtent = (VkExtent2D){320, 240};
    g_caps.maxImageExtent = (VkExtent2D){1280, 720};
    dm_vk_swapchain_options clamp = o; clamp.width = 2000; clamp.height = 100;
    CHECK(dm_vk_swapchain_create(&vk, &clamp, &sc) == VK_SUCCESS);
    CHECK(g_create_extent.width == 1280 && g_create_extent.height == 240 && sc.extent.width == 1280 && sc.extent.height == 240);
    dm_vk_swapchain_destroy(&vk, &sc);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0}; g_modes[0] = VK_PRESENT_MODE_MAILBOX_KHR;
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_ERROR_FEATURE_NOT_PRESENT && !sc.handle);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0}; g_fail_swapchain = 1;
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) != VK_SUCCESS && !sc.handle);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0}; g_fail_renderpass = 1;
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) != VK_SUCCESS && !sc.handle);
    CHECK(g_destroy_swapchains == 1 && g_free_commands == 0);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0}; g_fail_view = 1;
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) != VK_SUCCESS && !sc.handle);
    CHECK(g_destroy_swapchains == 1 && g_destroy_views == 0 && g_free_commands == 0);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0}; g_fail_framebuffer = 1;
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) != VK_SUCCESS && !sc.handle);
    CHECK(g_destroy_swapchains == 1 && g_destroy_views == 1 && g_destroy_fbs == 0 && g_free_commands == 0);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0}; g_fail_commands = 1;
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) != VK_SUCCESS && !sc.handle);
    CHECK(g_destroy_swapchains == 1 && g_free_commands == 0 && g_destroy_views == 3 && g_destroy_fbs == 3);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0}; g_fail_semaphore = 1;
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) != VK_SUCCESS && !sc.handle);
    CHECK(g_destroy_swapchains == 1 && g_free_commands == 3 && g_sem_destroyed == 1);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0}; g_fail_fence = 1;
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) != VK_SUCCESS && !sc.handle);
    CHECK(g_destroy_swapchains == 1 && g_free_commands == 3 && g_sem_destroyed == 2);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    CHECK(dm_vk_swapchain_acquire(&vk, &sc, NULL) == VK_SUCCESS);
    g_submit_result = VK_ERROR_DEVICE_LOST;
    CHECK(dm_vk_swapchain_submit(&vk, &sc) == VK_ERROR_DEVICE_LOST && sc.acquired && !sc.submitted);
    dm_vk_swapchain_destroy(&vk, &sc); CHECK(g_destroy_swapchains == 1);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    CHECK(dm_vk_swapchain_acquire(&vk, &sc, NULL) == VK_SUCCESS);
    CHECK(dm_vk_swapchain_submit(&vk, &sc) == VK_SUCCESS);
    g_present_result = VK_ERROR_DEVICE_LOST;
    CHECK(dm_vk_swapchain_present(&vk, &sc, NULL) == VK_ERROR_DEVICE_LOST && sc.submitted);
    CHECK(sc.handle == H(VkSwapchainKHR, 0x1000) && g_destroy_swapchains == 0);
    dm_vk_swapchain_destroy(&vk, &sc); CHECK(g_destroy_swapchains == 1);

    reset(); vk = fake_vk(); sc = (dm_vk_swapchain){0};
    CHECK(dm_vk_swapchain_create(&vk, &o, &sc) == VK_SUCCESS);
    g_fail_fence = 0; g_fail_semaphore = 0; g_fail_device_wait = 1;
    dm_vk_swapchain_destroy(&vk, &sc); CHECK(!sc.handle);

    printf("vulkan_swapchain: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
