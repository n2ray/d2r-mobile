// #139 present-aware init seam: real production dm_vk_create_present path, injected Vulkan loader boundary.
// Two devices prove non-blind selection: first has graphics/compute but no present; second has swapchain/present.
// Covers missing extensions, surface/device/pool failure, no present device, cleanup order and offscreen non-use.
#include "../vulkan/vk_host.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define H(t, v) ((t)(uintptr_t)(v))

static int g_surface_ext = 1, g_surface_fail, g_surface_null, g_device_fail, g_pool_fail;
static int g_present_supported = 1, g_swapchain = 1, g_instance_created, g_instance_destroyed;
static int g_surface_destroyed, g_device_created, g_device_destroyed, g_pool_created, g_pool_destroyed;
static VkInstance g_instance;
static VkDevice g_device;
static void *g_window = (void *)(uintptr_t)0x1234;

static VkResult VKAPI_CALL test_surface_factory(VkInstance instance, void *window, VkSurfaceKHR *surface) {
    CHECK(instance == g_instance && window == g_window);
    if (g_surface_fail) return VK_ERROR_INITIALIZATION_FAILED;
    if (g_surface_null) { *surface = VK_NULL_HANDLE; return VK_SUCCESS; }
    *surface = H(VkSurfaceKHR, 0x5001);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL test_vkEnumerateInstanceVersion(uint32_t *v) { *v = VK_API_VERSION_1_1; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkEnumerateInstanceLayerProperties(uint32_t *count, VkLayerProperties *p) {
    (void)p; *count = 0; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkEnumerateInstanceExtensionProperties(const char *layer, uint32_t *count, VkExtensionProperties *p) {
    (void)layer;
    const char *names[2] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
    uint32_t n = g_surface_ext ? 2u : 0u;
    if (!p) { *count = n; return VK_SUCCESS; }
    for (uint32_t i = 0; i < n; i++) { memset(&p[i], 0, sizeof p[i]); snprintf(p[i].extensionName, sizeof p[i].extensionName, "%s", names[i]); }
    *count = n; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateInstance(const VkInstanceCreateInfo *i, const VkAllocationCallbacks *a, VkInstance *out) {
    (void)a; CHECK(i && i->enabledExtensionCount >= 1);
    int surface = 0;
    for (uint32_t k = 0; k < i->enabledExtensionCount; k++) if (!strcmp(i->ppEnabledExtensionNames[k], VK_KHR_SURFACE_EXTENSION_NAME)) surface = 1;
    CHECK(surface);
    g_instance_created++; g_instance = H(VkInstance, 0x1001); *out = g_instance; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyInstance(VkInstance i, const VkAllocationCallbacks *a) { (void)i; (void)a; g_instance_destroyed++; }
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL test_vkGetInstanceProcAddr(VkInstance i, const char *name) { (void)i; (void)name; return NULL; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkEnumeratePhysicalDevices(VkInstance i, uint32_t *count, VkPhysicalDevice *d) {
    (void)i;
    if (!d) { *count = 2; return VK_SUCCESS; }
    d[0] = H(VkPhysicalDevice, 0x2001); d[1] = H(VkPhysicalDevice, 0x2002); *count = 2; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice p, uint32_t *count, VkQueueFamilyProperties *f) {
    (void)p;
    if (!f) { *count = 1; return; }
    memset(f, 0, sizeof *f); f[0].queueCount = 1; f[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT; *count = 1;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice p, const char *layer, uint32_t *count, VkExtensionProperties *e) {
    (void)p; (void)layer;
    if (!g_swapchain || p == H(VkPhysicalDevice, 0x2001)) { *count = 0; return VK_SUCCESS; }
    if (!e) { *count = 1; return VK_SUCCESS; }
    memset(e, 0, sizeof *e); snprintf(e->extensionName, sizeof e->extensionName, "%s", VK_KHR_SWAPCHAIN_EXTENSION_NAME); *count = 1; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL test_vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice p, uint32_t family, VkSurfaceKHR s, VkBool32 *supported) {
    (void)family; CHECK(s == H(VkSurfaceKHR, 0x5001)); *supported = (p == H(VkPhysicalDevice, 0x2002) && g_present_supported) ? VK_TRUE : VK_FALSE; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkGetPhysicalDeviceProperties(VkPhysicalDevice p, VkPhysicalDeviceProperties *o) { (void)p; memset(o, 0, sizeof *o); o->apiVersion = VK_API_VERSION_1_1; }
VKAPI_ATTR void VKAPI_CALL test_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice p, VkPhysicalDeviceMemoryProperties *o) { (void)p; memset(o, 0, sizeof *o); }
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateDevice(VkPhysicalDevice p, const VkDeviceCreateInfo *i, const VkAllocationCallbacks *a, VkDevice *d) {
    (void)a; CHECK(p == H(VkPhysicalDevice, 0x2002) && i->enabledExtensionCount == 1);
    CHECK(!strcmp(i->ppEnabledExtensionNames[0], VK_KHR_SWAPCHAIN_EXTENSION_NAME));
    if (g_device_fail) return VK_ERROR_OUT_OF_HOST_MEMORY;
    g_device_created++; g_device = H(VkDevice, 0x3001); *d = g_device; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyDevice(VkDevice d, const VkAllocationCallbacks *a) { (void)d; (void)a; g_device_destroyed++; }
VKAPI_ATTR void VKAPI_CALL test_vkGetDeviceQueue(VkDevice d, uint32_t family, uint32_t index, VkQueue *q) { (void)d; (void)family; (void)index; *q = H(VkQueue, 0x4001); }
VKAPI_ATTR VkResult VKAPI_CALL test_vkCreateCommandPool(VkDevice d, const VkCommandPoolCreateInfo *i, const VkAllocationCallbacks *a, VkCommandPool *p) {
    (void)d; (void)i; (void)a; if (g_pool_fail) return VK_ERROR_OUT_OF_HOST_MEMORY; g_pool_created++; *p = H(VkCommandPool, 0x6001); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL test_vkDestroyCommandPool(VkDevice d, VkCommandPool p, const VkAllocationCallbacks *a) { (void)d; (void)p; (void)a; g_pool_destroyed++; }
VKAPI_ATTR VkResult VKAPI_CALL test_vkDeviceWaitIdle(VkDevice d) { (void)d; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL test_vkDestroySurfaceKHR(VkInstance i, VkSurfaceKHR s, const VkAllocationCallbacks *a) { (void)i; (void)s; (void)a; g_surface_destroyed++; }

static void reset(void) {
    g_surface_ext = g_present_supported = g_swapchain = 1;
    g_surface_fail = g_surface_null = g_device_fail = g_pool_fail = 0;
    g_instance_created = g_instance_destroyed = g_surface_destroyed = 0;
    g_device_created = g_device_destroyed = g_pool_created = g_pool_destroyed = 0;
    g_instance = VK_NULL_HANDLE; g_device = VK_NULL_HANDLE;
}
static dm_vk_present_options options(void) {
    dm_vk_present_options o = DM_VK_PRESENT_OPTIONS_INIT;
    o.window = g_window; o.create_surface = test_surface_factory; return o;
}
static void expect_clean(void) {
    CHECK(g_instance_created == g_instance_destroyed);
    CHECK(g_device_created == g_device_destroyed);
    CHECK(g_pool_created == g_pool_destroyed);
    CHECK(g_surface_destroyed == 1);
}

int main(void) {
    dm_vk_present_options o = options();
    reset(); dm_vk vk = {0}; CHECK(dm_vk_create_present(&o, &vk) == VK_SUCCESS);
    CHECK(vk.device == H(VkDevice, 0x3001) && vk.surface == H(VkSurfaceKHR, 0x5001) && vk.surface_owned);
    CHECK(vk.family == 0);
    dm_vk_destroy(&vk); expect_clean();

    reset(); g_surface_ext = 0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_ERROR_EXTENSION_NOT_PRESENT);
    CHECK(!vk.instance && !vk.device && !vk.surface);
    CHECK(g_instance_created == 0);

    reset(); g_swapchain = 0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_ERROR_FEATURE_NOT_PRESENT);
    CHECK(!vk.device && g_instance_created == g_instance_destroyed && g_surface_destroyed == 1);

    reset(); g_present_supported = 0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_ERROR_FEATURE_NOT_PRESENT);
    CHECK(!vk.device && g_instance_destroyed == 1 && g_surface_destroyed == 1);

    reset(); g_surface_fail = 1; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) != VK_SUCCESS);
    CHECK(g_instance_destroyed == 1 && g_surface_destroyed == 0 && !vk.surface);

    reset(); g_surface_null = 1; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(g_instance_destroyed == 1 && !vk.surface);

    reset(); g_device_fail = 1; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) != VK_SUCCESS);
    CHECK(g_instance_destroyed == 1 && g_surface_destroyed == 1 && !vk.device);

    reset(); g_pool_fail = 1; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&o, &vk) != VK_SUCCESS);
    CHECK(g_instance_destroyed == 1 && g_surface_destroyed == 1 && g_device_destroyed == 1 && !vk.pool);

    // Surface factory sees the same instance and the borrowed window; device selection does not pick device 0.
    reset(); dm_vk_present_options bad = options(); bad.struct_size = 0; vk = (dm_vk){0};
    CHECK(dm_vk_create_present(&bad, &vk) == VK_ERROR_INITIALIZATION_FAILED);
    CHECK(g_instance_created == 0);

    printf("vulkan_present_init: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
