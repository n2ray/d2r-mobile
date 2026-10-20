#include "live_touch.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void event_size(dm_live_touch *t,int action,int id,float x,float y,int w,int h,int64_t ns){
    int32_t p=id;
    assert(dm_live_touch_event(t,action,0,1,&p,&x,&y,w,h,0,0,w,h,ns));
}
static void event(dm_live_touch *t,int action,int id,float x,float y,int64_t ns){
    event_size(t,action,id,x,y,667,375,ns);
}
static void map_title_at_game_mask(dm_live_touch *t,int w,int h,int64_t ns){
    dm_live_touch_set_context(t,0,dm_live_touch_game_mask(),1,1,1,0);
    float rect[]={.60f,0,.98f,.35f};
    dm_live_touch_set_minimap(t,rect,0);
    dm_live_touch_visual v;
    assert(dm_live_touch_visual_snapshot(t,&v));
    assert(!(v.enabled_targets&(1u<<DM_TC_MAP)));
    dm_tc_state *s=dm_live_touch_controls(t);
    float mx=s->layout.target[DM_TC_MAP].x,my=s->layout.target[DM_TC_MAP].y;
    float band=(rect[3]-rect[1])*h/4; if(band<30)band=fminf(30,(rect[3]-rect[1])*h*.4f);
    assert(mx>=rect[0]*w&&mx<rect[2]*w&&my<rect[1]*h+band);
    float nx,ny;
    event_size(t,DM_LIVE_ACTION_DOWN,40,mx,my,w,h,ns);
    event_size(t,DM_LIVE_ACTION_UP,40,mx,my,w,h,ns+100000000);
    assert(dm_live_touch_take_map_request(t,&nx,&ny)==1);
}
int main(void){
    dm_live_touch *t=dm_live_touch_create(667,375);assert(t);
    dm_live_touch_set_context(t,0,(1u<<DM_TC_COUNT)-1,1,1,1,0);
    float rect[]={.65f,.02f,.9f,.35f},x,y;
    dm_live_touch_set_minimap(t,rect,0);
    event(t,DM_LIVE_ACTION_DOWN,10,460,15,1000000000);
    event(t,DM_LIVE_ACTION_MOVE,10,461,15,1010000000);
    assert(dm_live_touch_active(t));
    event(t,DM_LIVE_ACTION_UP,10,461,15,1100000000);
    assert(dm_live_touch_take_map_request(t,&x,&y)==1);
    assert(x>.68f&&x<.70f&&y<.05f);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    assert(!dm_live_touch_active(t));
    event(t,DM_LIVE_ACTION_DOWN,11,460,15,2000000000);
    event(t,DM_LIVE_ACTION_UP,11,500,15,2100000000);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    event(t,DM_LIVE_ACTION_DOWN,12,460,15,3000000000);
    event(t,DM_LIVE_ACTION_CANCEL,12,460,15,3100000000);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    dm_live_touch_set_context(t,0,(1u<<DM_TC_COUNT)-1,1,1,1,0);
    dm_live_touch_set_minimap(t,rect,1);
    event(t,DM_LIVE_ACTION_DOWN,13,400,180,4000000000);
    event(t,DM_LIVE_ACTION_UP,13,400,180,4100000000);
    assert(dm_live_touch_take_map_request(t,&x,&y)==2);
    // Main attack remains an attack with the full map visible.
    dm_tc_state *s=dm_live_touch_controls(t);
    event(t,DM_LIVE_ACTION_DOWN,14,s->layout.target[0].x,s->layout.target[0].y,5000000000);
    assert(dm_live_touch_buttons(t));
    event(t,DM_LIVE_ACTION_UP,14,s->layout.target[0].x,s->layout.target[0].y,5100000000);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    event(t,DM_LIVE_ACTION_DOWN,15,460,15,6000000000);
    dm_live_touch_set_context(t,1,0,1,1,2,0);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    map_title_at_game_mask(t,667,375,7000000000);
    dm_live_touch_destroy(t);
    dm_live_touch *phone=dm_live_touch_create(780,360);assert(phone);
    map_title_at_game_mask(phone,780,360,8000000000);
    dm_live_touch_destroy(phone);
    puts("PASS map: title tap/move, full-map return, skill priority, cancel, context, game-mask title at 16:9 and phone aspect");
}
