// #33 production-boundary scene/identity provider.
//
// The snapshot is read from the existing host state entry points:
// dm_uiscreen_ingame / dm_uiscreen_panel, dm_ui_panel_state, dm_ui_gen and the
// per-tick dm_hud_get publication. This provider deliberately implements only
// scene/identity. It has no item hit model and no action acknowledgement:
// hit()/action() return 0, so the existing game bridge rejects those paths
// instead of inventing a target or a successful action.
#pragma once

#include <stdint.h>
#include "../runtime/touch_game_provider.h"
#include "dm_hud.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dm_live_touch_provider_host {
    void *context;
    int (*ingame)(void *context);
    int (*panel_open)(void *context);
    int (*panel_bits)(void *context);
    uint32_t (*ui_gen)(void *context);
    void (*hud)(void *context, dm_hud_info *out);
    uint64_t (*now_ns)(void *context);
} dm_live_touch_provider_host;

typedef struct dm_live_touch_provider {
    dm_live_touch_provider_host host;
    dm_tgp_provider iface;
    uint32_t module_id;
    uint64_t generation;
    uint64_t scene_epoch;
    uint64_t world_epoch;
    uint64_t last_ui_identity;
    int have_context;
    int last_scene;
    int last_ingame;
    int last_panel;
    int last_area;
} dm_live_touch_provider;

// host == NULL uses the real dm/host entry points. A caller-supplied host is
// the owned test seam and must return the same kinds of values.
void dm_live_touch_provider_init(dm_live_touch_provider *provider,
                                 const dm_live_touch_provider_host *host);
// Returns 0 and leaves the snapshot zeroed/unavailable when the real host has
// not yet published a player state. Front-end pages (not in a run) remain a
// valid menu observation; an in-run state requires dm_hud_get().valid.
int dm_live_touch_provider_refresh(dm_live_touch_provider *provider,
                                   dm_tgp_snapshot *out);
const dm_tgp_provider *dm_live_touch_provider_interface(dm_live_touch_provider *provider);

#ifdef __cplusplus
}
#endif
