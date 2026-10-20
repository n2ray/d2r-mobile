// A full color clear may move to attachment load; every other access executes it first.
static image_entry *deferred_clear_first;
static int deferred_clear_enabled;
static uint64_t deferred_clear_folded,deferred_clear_transfers;
static void deferred_clear_flush(image_entry *e) {
    if(!e->clear_pending)return;
    e->clear_pending=0;graphics_pass_end();
    image_access(e,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdClearColorImage(command,e->image,VK_IMAGE_LAYOUT_GENERAL,&e->clear_color,1,&e->clear_range);
    deferred_clear_transfers++;
}
static void deferred_clear_flush_all(void) {
    while(deferred_clear_first){image_entry *e=deferred_clear_first;deferred_clear_first=e->clear_next;
        e->clear_next=NULL;e->clear_listed=0;deferred_clear_flush(e);}
}
static void deferred_clear_queue(image_entry *e,VkImageSubresourceRange range,VkClearColorValue color) {
    if(e->clear_pending&&memcmp(&e->clear_range,&range,sizeof range))deferred_clear_flush(e);
    e->clear_range=range;e->clear_color=color;e->clear_pending=1;
    if(!deferred_clear_enabled){deferred_clear_flush(e);return;}
    if(!e->clear_listed){e->clear_listed=1;e->clear_next=deferred_clear_first;deferred_clear_first=e;}
}
static int deferred_clear_matches(const image_entry *e,const VkImageSubresourceRange *range,uint32_t width,uint32_t height) {
    return deferred_clear_enabled&&e->clear_pending&&e->aspect==VK_IMAGE_ASPECT_COLOR_BIT
        &&e->image_type==VK_IMAGE_TYPE_2D&&range->levelCount==1&&range->layerCount==1
        &&!memcmp(&e->clear_range,range,sizeof *range)
        &&width==dimension(e->texture.d.width,range->baseMipLevel)&&height==dimension(e->texture.d.height,range->baseMipLevel);
}
static void deferred_clear_attachment_access(image_entry *e,int fold,VkPipelineStageFlags stage,VkAccessFlags access) {
    int pending=e->clear_pending;
    if(fold)e->clear_pending=0;
    image_access(e,stage,access);
    if(fold)e->clear_pending=pending;
}
static void deferred_clear_take(image_entry *e) {
    if(e->clear_pending)deferred_clear_folded++;
    e->clear_pending=0;
}
static void deferred_clear_report(uint64_t ticket) {
    if(ticket%60)return;
#ifdef __ANDROID__
    char value[PROP_VALUE_MAX]={0};__system_property_get("debug.d2r.clear_fold",value);
    deferred_clear_enabled=!strcmp(value,"1");
#endif
    static uint64_t folded,transfers;
    LOG("[vulkan-clear-fold] frame=%llu enabled=%d folded=%llu transfers=%llu",
        (unsigned long long)ticket,deferred_clear_enabled,(unsigned long long)(deferred_clear_folded-folded),
        (unsigned long long)(deferred_clear_transfers-transfers));
    folded=deferred_clear_folded;transfers=deferred_clear_transfers;
}
