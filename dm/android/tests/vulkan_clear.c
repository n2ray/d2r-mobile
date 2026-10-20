#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t VkPipelineStageFlags;
typedef uint32_t VkAccessFlags;
typedef union {float float32[4];uint32_t uint32[4];} VkClearColorValue;
typedef struct {uint32_t aspectMask,baseMipLevel,levelCount,baseArrayLayer,layerCount;} VkImageSubresourceRange;
enum {VK_PIPELINE_STAGE_TRANSFER_BIT=1,VK_ACCESS_TRANSFER_WRITE_BIT=2,VK_IMAGE_LAYOUT_GENERAL=3,VK_IMAGE_ASPECT_COLOR_BIT=1,VK_IMAGE_TYPE_2D=1};
typedef struct image_entry {
    unsigned image,aspect,image_type;struct {struct {uint32_t width,height;} d;} texture;
    int clear_pending,clear_listed;VkClearColorValue clear_color;VkImageSubresourceRange clear_range;
    struct image_entry *clear_next;
} image_entry;
static unsigned accesses,transfers,ends;static VkClearColorValue actual[4];
static void graphics_pass_end(void){ends++;}
static void image_access(image_entry *e,VkPipelineStageFlags stage,VkAccessFlags access);
static uint32_t dimension(uint32_t x,unsigned level){x>>=level;return x?x:1;}
static void vkCmdClearColorImage(unsigned command,unsigned image,unsigned layout,const VkClearColorValue *value,unsigned count,const VkImageSubresourceRange *range){
    (void)command;assert(layout==VK_IMAGE_LAYOUT_GENERAL&&count==1&&range->aspectMask==VK_IMAGE_ASPECT_COLOR_BIT);
    actual[image]=*value;transfers++;
}
static unsigned command;
#define LOG(...) do {if(0)printf(__VA_ARGS__);} while(0)
#include "../native/nvn_vulkan_clear.inc"
static void image_access(image_entry *e,VkPipelineStageFlags stage,VkAccessFlags access){
    (void)stage;(void)access;deferred_clear_flush(e);accesses++;
}
int main(void){
    image_entry a={.image=1,.aspect=1,.image_type=1,.texture={.d={8,8}}},b=a;b.image=2;
    VkImageSubresourceRange range={1,0,1,0,1};VkClearColorValue red={.float32={1,0,0,1}},green={.float32={0,1,0,1}};
    deferred_clear_queue(&a,range,red);assert(transfers==1&&!a.clear_pending&&!a.clear_listed);
    deferred_clear_enabled=1;deferred_clear_queue(&a,range,green);assert(transfers==1&&a.clear_pending);
    assert(deferred_clear_matches(&a,&range,8,8));assert(!deferred_clear_matches(&a,&range,7,8));
    VkImageSubresourceRange other=range;other.baseMipLevel=1;assert(!deferred_clear_matches(&a,&other,4,4));
    other=range;other.layerCount=2;assert(!deferred_clear_matches(&a,&other,8,8));
    a.image_type=2;assert(!deferred_clear_matches(&a,&range,8,8));a.image_type=1;
    a.aspect=2;assert(!deferred_clear_matches(&a,&range,8,8));a.aspect=1;
    deferred_clear_attachment_access(&a,1,4,8);assert(a.clear_pending&&transfers==1);
    // The render pass consumes its load clear once, and submission retires its list node.
    actual[a.image]=a.clear_color;deferred_clear_take(&a);deferred_clear_flush_all();
    assert(!a.clear_pending&&!a.clear_listed&&!deferred_clear_first&&transfers==1&&deferred_clear_folded==1);
    assert(!memcmp(&actual[a.image],&green,sizeof green));
    // A sample/copy access executes the clear before the reader, rather than dropping it.
    deferred_clear_queue(&a,range,red);image_access(&a,8,16);
    assert(!a.clear_pending&&transfers==2&&!memcmp(&actual[a.image],&red,sizeof red));
    deferred_clear_flush_all();assert(!a.clear_listed);
    // Later full clears replace only the exact same subresource; other mips flush first.
    deferred_clear_queue(&a,range,red);deferred_clear_queue(&a,range,green);assert(transfers==2);
    other=range;other.baseMipLevel=1;deferred_clear_queue(&a,other,red);assert(transfers==3);
    deferred_clear_queue(&b,range,green);deferred_clear_flush_all();assert(transfers==5);
    assert(!a.clear_pending&&!a.clear_listed&&!b.clear_pending&&!b.clear_listed&&!deferred_clear_first);
    deferred_clear_queue(&a,range,red);deferred_clear_attachment_access(&a,0,4,8);assert(transfers==6);
    deferred_clear_flush_all();deferred_clear_report(60);assert(accesses&&ends);
    puts("production deferred clear: exact extent/subresource, read-before-fold, overwrite, batch retirement PASS");
}
