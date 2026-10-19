// #36 owned producer->consumer fixture for the Android inventory item route.
// It drives the real dm_live_touch core and live_touch_items seam. Host hit and
// drop answers are injected; the callbacks are the only observable actions.
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "live_touch.h"
#include "live_touch_items.h"
#include "dm_touch_controls.h"
#include "dm_input.h"

typedef struct {
    unsigned next_item, next_drop;
    int item_answer, drop_answer, drop_last;
    unsigned item_queries, drop_queries;
    float last_drop_lifted;
    int item_tap, identify, use, long_press, tab_go, stack_take, mouse_click;
    int order[32], order_count;
    float mouse_x, mouse_y;
} fake_host;

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static void note(fake_host *f, int what) {
    if (f->order_count < (int)(sizeof f->order / sizeof f->order[0])) f->order[f->order_count++] = what;
}
static unsigned item_query(void *context, float x, float y) {
    (void)x; (void)y; fake_host *f = context; f->item_queries++; return ++f->next_item;
}
static int item_result(void *context, unsigned seq) {
    (void)seq; fake_host *f = context; return f->item_answer;
}
static unsigned drop_query(void *context, float x, float y, float finger_y) {
    (void)x; (void)y; (void)finger_y; fake_host *f = context; f->drop_queries++; return ++f->next_drop;
}
static int drop_result(void *context, unsigned seq) {
    (void)seq; fake_host *f = context; return f->drop_answer;
}
static int drop_last(void *context) { fake_host *f = context; return f->drop_last; }
static void item_tap(void *context) { fake_host *f = context; f->item_tap++; note(f, 1); }
static void identify(void *context) { fake_host *f = context; f->identify++; note(f, 2); }
static void use(void *context) { fake_host *f = context; f->use++; note(f, 3); }
static void long_press(void *context) { fake_host *f = context; f->long_press++; note(f, 4); }
static void tab_go(void *context) { fake_host *f = context; f->tab_go++; note(f, 5); }
static void stack_take(void *context) { fake_host *f = context; f->stack_take++; note(f, 6); }
static void mouse_click(void *context, float x, float y) {
    fake_host *f = context; f->mouse_click++; f->mouse_x = x; f->mouse_y = y; note(f, 7);
}

static const dm_live_touch_items_host host_vtable = {
    .item_query = item_query, .item_result = item_result,
    .drop_query = drop_query, .drop_result = drop_result, .drop_last = drop_last,
};
static const dm_live_touch_items_actions actions = {
    .item_tap = item_tap, .identify = identify, .use = use,
    .long_press = long_press, .tab_go = tab_go, .stack_take = stack_take,
    .mouse_click = mouse_click,
};

typedef struct { dm_live_touch *touch; dm_live_touch_items *items; fake_host fake; } rig;

static rig make_rig(void) {
    rig r;
    memset(&r, 0, sizeof r);
    r.touch = dm_live_touch_create(667, 375);
    dm_live_touch_items_host host = host_vtable;
    host.context = &r.fake;
    r.items = dm_live_touch_items_create(r.touch, &host);
    dm_live_touch_set_mode(r.touch, 1, dm_tc_nav_mask(1) | (1u << DM_TC_MENU));
    r.fake.item_answer = 1;
    r.fake.drop_answer = 5;
    (void)dm_live_touch_items_set_context(r.items, 1, 1, 1, 1);
    return r;
}
static void drop_rig(rig *r) {
    dm_live_touch_items_destroy(r->items);
    dm_live_touch_destroy(r->touch);
}
static int event(rig *r, int action, int index, int count, int32_t id,
                 float x, float y, int64_t t) {
    float xs[2] = {x, x}, ys[2] = {y, y};
    int32_t ids[2] = {id, id};
    return dm_live_touch_items_event(r->items, action, index, count, ids, xs, ys,
                                     667, 375, 0, 0, 667, 375, t);
}
static void consume(rig *r) {
    dm_live_touch_items_actions a = actions;
    a.context = &r->fake;
    dm_live_touch_items_consume(r->items, 1, 1, 1, &a);
}
static void tap(rig *r, int32_t id, float x, float y, int64_t down_ns) {
    event(r, DM_LIVE_ACTION_DOWN, 0, 1, id, x, y, down_ns);
    event(r, DM_LIVE_ACTION_UP, 0, 1, id, x, y, down_ns + 60000000);
    dm_live_touch_items_tick(r->items, down_ns + 70000000);
    consume(r);
}

int main(void) {
    rig r = make_rig();
    CHECK(r.touch && r.items);

    // Tap: hit answer is applied only after the async answer, then one host tap.
    event(&r, DM_LIVE_ACTION_DOWN, 0, 1, 11, 100, 100, 1000000000LL);
    CHECK(r.fake.item_queries == 1);
    event(&r, DM_LIVE_ACTION_UP, 0, 1, 11, 100, 100, 1060000000LL);
    CHECK(r.fake.item_tap == 0);                 // not before the host answer
    dm_live_touch_items_tick(r.items, 1070000000LL);
    consume(&r);
    CHECK(r.fake.item_tap == 1);
    CHECK(r.fake.mouse_click == 0);

    // Double tap: a first tap at C selects C; the second tap at C uses it.
    tap(&r, 13, 260, 200, 2100000000LL);
    event(&r, DM_LIVE_ACTION_DOWN, 0, 1, 14, 260, 200, 2200000000LL);
    event(&r, DM_LIVE_ACTION_UP, 0, 1, 14, 260, 200, 2260000000LL);
    dm_live_touch_items_tick(r.items, 2270000000LL);
    consume(&r);
    CHECK(r.fake.use == 1);
    CHECK(r.fake.item_tap == 2);
    CHECK(r.fake.order_count >= 3);
    CHECK(r.fake.order[r.fake.order_count - 3] == 1);
    CHECK(r.fake.order[r.fake.order_count - 2] == 1);
    CHECK(r.fake.order[r.fake.order_count - 1] == 3);

    // Long press: core decides the hold; host action is one-shot.
    event(&r, DM_LIVE_ACTION_DOWN, 0, 1, 21, 120, 120, 3000000000LL);
    dm_live_touch_scheduler_step(r.touch, 3600000000LL);
    event(&r, DM_LIVE_ACTION_UP, 0, 1, 21, 120, 120, 3610000000LL);
    dm_live_touch_items_tick(r.items, 3620000000LL);
    consume(&r);
    CHECK(r.fake.long_press == 1);
    CHECK(r.fake.item_tap == 2);

    // Drag: query drop at release, accept placement, core emits the pick/drop pulse.
    event(&r, DM_LIVE_ACTION_DOWN, 0, 1, 31, 100, 100, 4000000000LL);
    event(&r, DM_LIVE_ACTION_MOVE, -1, 1, 31, 160, 100, 4020000000LL);
    CHECK(r.fake.drop_queries > 0);
    event(&r, DM_LIVE_ACTION_UP, 0, 1, 31, 160, 100, 4060000000LL);
    dm_live_touch_items_tick(r.items, 4070000000LL);
    consume(&r);
    CHECK(r.fake.drop_queries >= 2);
    CHECK(r.fake.item_tap == 2);
    CHECK(dm_live_touch_buttons(r.touch) & DM_BTN_A);

    // Cancel: a later answer must not click or finish a stale gesture.
    event(&r, DM_LIVE_ACTION_DOWN, 0, 1, 41, 140, 140, 5000000000LL);
    event(&r, DM_LIVE_ACTION_CANCEL, 0, 1, 41, 140, 140, 5001000000LL);
    dm_live_touch_items_tick(r.items, 5100000000LL);
    consume(&r);
    CHECK(r.fake.item_tap == 2 && r.fake.use == 1 && r.fake.mouse_click == 0);

    // Out-of-range host category (vendor shelf) is refused without a click/sale.
    event(&r, DM_LIVE_ACTION_DOWN, 0, 1, 51, 150, 150, 6000000000LL);
    r.fake.item_answer = 7;
    event(&r, DM_LIVE_ACTION_UP, 0, 1, 51, 150, 150, 6006000000LL);
    dm_live_touch_items_tick(r.items, 6007000000LL);
    consume(&r);
    CHECK(r.fake.item_tap == 2 && r.fake.mouse_click == 0);

    // Unknown/timeout becomes an explicit no-op, never a mouse click.
    r.fake.item_answer = -1;
    event(&r, DM_LIVE_ACTION_DOWN, 0, 1, 61, 160, 160, 7000000000LL);
    event(&r, DM_LIVE_ACTION_UP, 0, 1, 61, 160, 160, 7006000000LL);
    dm_live_touch_items_tick(r.items, 7300000000LL);
    consume(&r);
    CHECK(r.fake.mouse_click == 0);

    // Context change discards a pending answer.
    r.fake.item_answer = 1;
    event(&r, DM_LIVE_ACTION_DOWN, 0, 1, 71, 170, 170, 8000000000LL);
    dm_live_touch_items_set_context(r.items, 2, 2, 2, 1);
    dm_live_touch_items_tick(r.items, 8010000000LL);
    consume(&r);
    CHECK(r.fake.item_tap == 2);

    drop_rig(&r);
    if (failures) return 1;
    puts("PASS live touch items: tap/double/long/drag/cancel/unknown/context");
    return 0;
}
