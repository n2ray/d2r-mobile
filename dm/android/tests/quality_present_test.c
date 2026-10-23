#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "../native/debug_stats.h"
static VkDevice device;
static VkSwapchainKHR swapchain;
static _Atomic uint64_t quality_gpu_ns;
static pthread_mutex_t debug_lock=PTHREAD_MUTEX_INITIALIZER;
static dm_android_debug_stats debug_stats;
static unsigned resumes,feeds;
static double last_interval;
static void dm_3d_scale_resumed(void){resumes++;}
static void dm_3d_scale_feed(double interval){feeds++;last_interval=interval;}
#define LOG(...) ((void)0)
PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice d,const char *name){(void)d;(void)name;return NULL;}
static VkPastPresentationTimingGOOGLE records[3];
static VkResult past(VkDevice d,VkSwapchainKHR s,uint32_t *count,VkPastPresentationTimingGOOGLE *out){
    (void)d;(void)s;if(out)memcpy(out,records,sizeof records);*count=3;return VK_SUCCESS;
}
static VkResult refresh(VkDevice d,VkSwapchainKHR s,VkRefreshCycleDurationGOOGLE *out){(void)d;(void)s;out->refreshDuration=8333333;return VK_SUCCESS;}
#include "../native/nvn_vulkan_present_timing.inc"
int main(void){
    swapchain=(VkSwapchainKHR)(uintptr_t)1;display_timing_enabled=1;get_past_timing=past;get_refresh_duration=refresh;
    atomic_store(&quality_gpu_ns,123);display_timing_reset();assert(resumes==1&&!atomic_load(&quality_gpu_ns));
    records[0].actualPresentTime=1000000000;records[1].actualPresentTime=1000000000;records[2].actualPresentTime=1016666667;
    display_timing_poll(1100000000);assert(feeds==1&&last_interval>16.66&&last_interval<16.67);
    display_timing_poll(1101000000);assert(feeds==1);
    records[0].actualPresentTime=983333333;records[1].actualPresentTime=1016666667;records[2].actualPresentTime=1050000000;
    display_timing_poll(1110000000);assert(feeds==2&&last_interval>33.33&&last_interval<33.34);
    display_timing_enabled=0;display_timing_poll(1200000000);assert(feeds==2);
    display_timing_reset();display_timing_enabled=1;display_timing_poll(1210000000);
    assert(resumes==2&&feeds==4);
    puts("PASS quality presentation: actual intervals, duplicate/old timestamps, unavailable timing and resume reset");
}
