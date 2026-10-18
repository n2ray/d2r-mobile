// 有界 swapchain：只做 surface capability 协商、清屏命令、acquire/submit/present 与显式退役。
// Bounded swapchain: surface capability negotiation, clear commands, acquire/submit/present and explicit retirement.
#include "vk_swapchain.h"
#include <stdlib.h>
#include <string.h>

static uint32_t dm_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int dm_format_color_attachment(dm_vk *vk, VkFormat format) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(vk->physical, format, &props);
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
}

static VkCompositeAlphaFlagBitsKHR dm_pick_alpha(VkCompositeAlphaFlagsKHR supported) {
    if (supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (supported & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    if (supported & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
}

static void dm_swapchain_destroy_owned(dm_vk *vk, dm_vk_swapchain *chain, int destroy_swapchain) {
    if (!chain) return;
    if (vk && vk->device) {
        if (chain->fence) vkDestroyFence(vk->device, chain->fence, NULL);
        if (chain->render_finished) vkDestroySemaphore(vk->device, chain->render_finished, NULL);
        if (chain->image_available) vkDestroySemaphore(vk->device, chain->image_available, NULL);
        if (chain->commands_allocated && chain->commands && vk->pool) vkFreeCommandBuffers(vk->device, vk->pool, chain->image_count, chain->commands);
        for (uint32_t i = 0; i < chain->image_count; i++) {
            if (chain->framebuffers && chain->framebuffers[i]) vkDestroyFramebuffer(vk->device, chain->framebuffers[i], NULL);
            if (chain->views && chain->views[i]) vkDestroyImageView(vk->device, chain->views[i], NULL);
        }
        if (chain->render_pass) vkDestroyRenderPass(vk->device, chain->render_pass, NULL);
        if (destroy_swapchain && chain->handle) vkDestroySwapchainKHR(vk->device, chain->handle, NULL);
    }
    free(chain->images); free(chain->views); free(chain->framebuffers); free(chain->commands);
    memset(chain, 0, sizeof *chain);
}

static VkResult dm_swapchain_create_impl(dm_vk *vk, const dm_vk_swapchain_options *options,
    VkSwapchainKHR old_swapchain, dm_vk_swapchain *out) {
    if (!vk || !vk->device || !vk->physical || !vk->surface || !vk->pool || !options ||
        options->struct_size != sizeof *options || !options->width || !options->height)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (options->present_mode != VK_PRESENT_MODE_FIFO_KHR) return VK_ERROR_FEATURE_NOT_PRESENT;
    *out = (dm_vk_swapchain){0};
    VkResult result;
    VkSurfaceCapabilitiesKHR caps;
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->physical, vk->surface, &caps);
    if (result != VK_SUCCESS) return result;
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) return VK_ERROR_FEATURE_NOT_PRESENT;
    if (!caps.supportedCompositeAlpha) return VK_ERROR_FEATURE_NOT_PRESENT;

    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
        if (!extent.width || !extent.height) return VK_ERROR_INITIALIZATION_FAILED;
    } else {
        if (!caps.minImageExtent.width || !caps.minImageExtent.height) return VK_ERROR_INITIALIZATION_FAILED;
        extent.width = dm_clamp_u32(options->width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = dm_clamp_u32(options->height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    uint32_t image_count = caps.minImageCount;
    if (!image_count || (caps.maxImageCount && image_count > caps.maxImageCount))
        return VK_ERROR_FEATURE_NOT_PRESENT;

    uint32_t format_count = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(vk->physical, vk->surface, &format_count, NULL);
    if (result != VK_SUCCESS || !format_count) return result == VK_SUCCESS ? VK_ERROR_FEATURE_NOT_PRESENT : result;
    VkSurfaceFormatKHR *formats = calloc(format_count, sizeof *formats);
    if (!formats) return VK_ERROR_OUT_OF_HOST_MEMORY;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(vk->physical, vk->surface, &format_count, formats);
    if (result != VK_SUCCESS) { free(formats); return result; }
    VkSurfaceFormatKHR chosen = {0};
    int found = 0;
    for (uint32_t pass = 0; pass < 2 && !found; pass++) {
        for (uint32_t i = 0; i < format_count; i++) {
            VkFormat preferred = pass == 0 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
            if (formats[i].format == preferred && dm_format_color_attachment(vk, formats[i].format)) {
                chosen = formats[i]; found = 1; break;
            }
        }
    }
    for (uint32_t i = 0; i < format_count && !found; i++)
        if (dm_format_color_attachment(vk, formats[i].format)) { chosen = formats[i]; found = 1; }
    free(formats);
    if (!found) return VK_ERROR_FEATURE_NOT_PRESENT;

    uint32_t mode_count = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(vk->physical, vk->surface, &mode_count, NULL);
    if (result != VK_SUCCESS || !mode_count) return result == VK_SUCCESS ? VK_ERROR_FEATURE_NOT_PRESENT : result;
    VkPresentModeKHR *modes = calloc(mode_count, sizeof *modes);
    if (!modes) return VK_ERROR_OUT_OF_HOST_MEMORY;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(vk->physical, vk->surface, &mode_count, modes);
    int fifo = 0;
    for (uint32_t i = 0; result == VK_SUCCESS && i < mode_count; i++) if (modes[i] == VK_PRESENT_MODE_FIFO_KHR) fifo = 1;
    free(modes);
    if (result != VK_SUCCESS) return result;
    if (!fifo) return VK_ERROR_FEATURE_NOT_PRESENT;

    VkCompositeAlphaFlagBitsKHR alpha = dm_pick_alpha(caps.supportedCompositeAlpha);
    VkSwapchainCreateInfoKHR create = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk->surface, .minImageCount = image_count, .imageFormat = chosen.format,
        .imageColorSpace = chosen.colorSpace, .imageExtent = extent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform, .compositeAlpha = alpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE, .oldSwapchain = old_swapchain};
    result = vkCreateSwapchainKHR(vk->device, &create, NULL, &out->handle);
    if (result != VK_SUCCESS) return result;
    out->format = chosen.format; out->color_space = chosen.colorSpace; out->extent = extent;
    out->present_mode = VK_PRESENT_MODE_FIFO_KHR;
    result = vkGetSwapchainImagesKHR(vk->device, out->handle, &out->image_count, NULL);
    if (result != VK_SUCCESS || !out->image_count) goto fail;
    if (out->image_count > 64) { result = VK_ERROR_INITIALIZATION_FAILED; goto fail; }
    out->images = calloc(out->image_count, sizeof *out->images);
    out->views = calloc(out->image_count, sizeof *out->views);
    out->framebuffers = calloc(out->image_count, sizeof *out->framebuffers);
    out->commands = calloc(out->image_count, sizeof *out->commands);
    if (!out->images || !out->views || !out->framebuffers || !out->commands) {
        result = VK_ERROR_OUT_OF_HOST_MEMORY; goto fail;
    }
    result = vkGetSwapchainImagesKHR(vk->device, out->handle, &out->image_count, out->images);
    if (result != VK_SUCCESS) goto fail;

    VkAttachmentDescription attachment = {.format = out->format, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference color_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &color_ref};
    VkSubpassDependency dependency = {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo render_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = 1, .pDependencies = &dependency};
    result = vkCreateRenderPass(vk->device, &render_info, NULL, &out->render_pass);
    if (result != VK_SUCCESS) goto fail;

    VkImageViewCreateInfo view_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = out->format,
        .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
    VkFramebufferCreateInfo framebuffer_info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = out->render_pass, .attachmentCount = 1, .width = extent.width,
        .height = extent.height, .layers = 1};
    for (uint32_t i = 0; i < out->image_count; i++) {
        view_info.image = out->images[i];
        result = vkCreateImageView(vk->device, &view_info, NULL, &out->views[i]);
        if (result != VK_SUCCESS) goto fail;
        framebuffer_info.pAttachments = &out->views[i];
        result = vkCreateFramebuffer(vk->device, &framebuffer_info, NULL, &out->framebuffers[i]);
        if (result != VK_SUCCESS) goto fail;
    }
    VkCommandBufferAllocateInfo command_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = out->image_count};
    result = vkAllocateCommandBuffers(vk->device, &command_info, out->commands);
    if (result != VK_SUCCESS) goto fail;
    out->commands_allocated = 1;
    for (uint32_t i = 0; i < out->image_count; i++) {
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        result = vkBeginCommandBuffer(out->commands[i], &begin);
        if (result != VK_SUCCESS) goto fail;
        VkClearValue clear = {.color = options->clear};
        VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = out->render_pass, .framebuffer = out->framebuffers[i],
            .renderArea = {{0, 0}, extent}, .clearValueCount = 1, .pClearValues = &clear};
        vkCmdBeginRenderPass(out->commands[i], &pass, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(out->commands[i]);
        result = vkEndCommandBuffer(out->commands[i]);
        if (result != VK_SUCCESS) goto fail;
    }
    VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    result = vkCreateSemaphore(vk->device, &semaphore_info, NULL, &out->image_available);
    if (result != VK_SUCCESS) goto fail;
    result = vkCreateSemaphore(vk->device, &semaphore_info, NULL, &out->render_finished);
    if (result != VK_SUCCESS) goto fail;
    result = vkCreateFence(vk->device, &fence_info, NULL, &out->fence);
    if (result != VK_SUCCESS) goto fail;
    return VK_SUCCESS;

fail:
    dm_swapchain_destroy_owned(vk, out, 1);
    return result;
}

VkResult dm_vk_swapchain_create(dm_vk *vk, const dm_vk_swapchain_options *options, dm_vk_swapchain *out) {
    if (!out) return VK_ERROR_INITIALIZATION_FAILED;
    *out = (dm_vk_swapchain){0};
    return dm_swapchain_create_impl(vk, options, VK_NULL_HANDLE, out);
}

VkResult dm_vk_swapchain_recreate(dm_vk *vk, dm_vk_swapchain *chain, const dm_vk_swapchain_options *options) {
    if (!vk || !vk->device || !chain || !chain->handle) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = vkDeviceWaitIdle(vk->device);
    if (result != VK_SUCCESS) return result;
    dm_vk_swapchain fresh = {0};
    result = dm_swapchain_create_impl(vk, options, chain->handle, &fresh);
    if (result == VK_SUCCESS) {
        dm_swapchain_destroy_owned(vk, chain, 1);
        *chain = fresh;
    } else {
        // Recreate is fail-closed: the old chain was offered as oldSwapchain and is not claimed as a usable rollback.
        dm_swapchain_destroy_owned(vk, chain, 1);
    }
    return result;
}

VkResult dm_vk_swapchain_acquire(dm_vk *vk, dm_vk_swapchain *chain, int *suboptimal) {
    if (suboptimal) *suboptimal = 0;
    if (!vk || !vk->device || !chain || !chain->handle || chain->acquired || chain->submitted)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (chain->fence_pending) {
        VkResult result = vkWaitForFences(vk->device, 1, &chain->fence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) return result;
        result = vkQueueWaitIdle(vk->queue);
        if (result != VK_SUCCESS) return result;
        chain->fence_pending = 0;
    }
    uint32_t index = 0;
    VkResult result = vkAcquireNextImageKHR(vk->device, chain->handle, UINT64_MAX,
        chain->image_available, VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { chain->needs_recreate = 1; return result; }
    if (result == VK_SUBOPTIMAL_KHR) { if (suboptimal) *suboptimal = 1; }
    else if (result != VK_SUCCESS) return result;
    if (index >= chain->image_count) return VK_ERROR_INITIALIZATION_FAILED;
    chain->image_index = index; chain->acquired = 1;
    return result;
}

VkResult dm_vk_swapchain_submit(dm_vk *vk, dm_vk_swapchain *chain) {
    if (!vk || !vk->device || !chain || !chain->acquired) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = vkResetFences(vk->device, 1, &chain->fence);
    if (result != VK_SUCCESS) return result;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &chain->image_available, .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1, .pCommandBuffers = &chain->commands[chain->image_index],
        .signalSemaphoreCount = 1, .pSignalSemaphores = &chain->render_finished};
    result = vkQueueSubmit(vk->queue, 1, &submit, chain->fence);
    if (result != VK_SUCCESS) return result;
    chain->acquired = 0; chain->submitted = 1; chain->fence_pending = 1;
    return VK_SUCCESS;
}

VkResult dm_vk_swapchain_present(dm_vk *vk, dm_vk_swapchain *chain, int *suboptimal) {
    if (suboptimal) *suboptimal = 0;
    if (!vk || !vk->device || !chain || !chain->handle || !chain->submitted)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &chain->render_finished,
        .swapchainCount = 1, .pSwapchains = &chain->handle, .pImageIndices = &chain->image_index};
    VkResult result = vkQueuePresentKHR(vk->queue, &present);
    if (result == VK_SUCCESS) { chain->submitted = 0; return result; }
    if (result == VK_SUBOPTIMAL_KHR) { if (suboptimal) *suboptimal = 1; chain->needs_recreate = 1; chain->submitted = 0; return result; }
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { chain->needs_recreate = 1; chain->submitted = 0; return result; }
    return result;
}

void dm_vk_swapchain_destroy(dm_vk *vk, dm_vk_swapchain *chain) {
    if (!chain) return;
    if (vk && vk->device) vkDeviceWaitIdle(vk->device);
    dm_swapchain_destroy_owned(vk, chain, 1);
}
