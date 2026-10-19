#include "live_touch_provider.h"
#include "dm.h"
#include "dm_cursor.h"
#include "dm_uiscreen.h"

#include <string.h>

#define DM_LTP_MODULE_ID 0x4c545000u

static int host_ingame(void *context) { (void)context; return dm_uiscreen_ingame(); }
static int host_panel_open(void *context) { (void)context; return dm_uiscreen_panel(); }
static int host_panel_bits(void *context) { (void)context; return dm_ui_panel_state(); }
static uint32_t host_ui_gen(void *context) { (void)context; return dm_ui_gen(); }
static void host_hud(void *context, dm_hud_info *out) { (void)context; dm_hud_get(out); }
static uint64_t host_now(void *context) { (void)context; return now_ns(); }

static int provider_snapshot(void *context, dm_tgp_snapshot *out) {
    return dm_live_touch_provider_refresh((dm_live_touch_provider *)context, out);
}
static int provider_hit(void *context, float x, float y, dm_tgp_hit *out) {
    (void)context; (void)x; (void)y;
    if (out) memset(out, 0, sizeof(*out));
    return 0;
}
static int provider_action(void *context, const dm_tgp_action *action, uint64_t now_ns) {
    (void)context; (void)action; (void)now_ns;
    return 0;
}

void dm_live_touch_provider_init(dm_live_touch_provider *provider,
                                 const dm_live_touch_provider_host *host) {
    if (!provider) return;
    memset(provider, 0, sizeof(*provider));
    if (host) {
        provider->host = *host;
    } else {
        provider->host.context = NULL;
        provider->host.ingame = host_ingame;
        provider->host.panel_open = host_panel_open;
        provider->host.panel_bits = host_panel_bits;
        provider->host.ui_gen = host_ui_gen;
        provider->host.hud = host_hud;
        provider->host.now_ns = host_now;
    }
    provider->module_id = DM_LTP_MODULE_ID;
    provider->generation = 1;
    provider->scene_epoch = 1;
    provider->world_epoch = 1;
    provider->iface.abi_version = DM_TGP_ABI_VERSION;
    provider->iface.module_id = provider->module_id;
    provider->iface.generation = provider->generation;
    // There is no guest module or object range in this scene/identity-only
    // provider. The bridge still receives a structurally valid module identity;
    // its hit path remains unavailable.
    provider->iface.module_base = 1;
    provider->iface.module_size = 1;
    provider->iface.object_range_base = 0;
    provider->iface.object_range_size = 0;
    provider->iface.context = provider;
    provider->iface.snapshot = provider_snapshot;
    provider->iface.hit = provider_hit;
    provider->iface.action = provider_action;
}

int dm_live_touch_provider_refresh(dm_live_touch_provider *provider,
                                   dm_tgp_snapshot *out) {
    if (!provider || !out) return 0;
    memset(out, 0, sizeof(*out));
    if (!provider->host.ingame || !provider->host.panel_open ||
        !provider->host.panel_bits || !provider->host.ui_gen ||
        !provider->host.hud || !provider->host.now_ns) return 0;

    int ingame = provider->host.ingame(provider->host.context) ? 1 : 0;
    dm_hud_info hud;
    memset(&hud, 0, sizeof(hud));
    provider->host.hud(provider->host.context, &hud);
    // An in-run snapshot without the real player publication is unknown. Do
    // not turn that into GAME merely because the surface is marked in-run.
    if (ingame && !hud.valid) return 0;

    int panel_bits = provider->host.panel_bits(provider->host.context);
    if (panel_bits < 0 || (panel_bits & ~0x7f)) return 0;
    int panel_open = provider->host.panel_open(provider->host.context) ? 1 : 0;
    uint32_t ui_gen = provider->host.ui_gen(provider->host.context);
    if (ui_gen == 0) ui_gen = 1;
    uint64_t now = provider->host.now_ns(provider->host.context);
    if (now == 0) return 0;

    int scene;
    if (!ingame) scene = DM_TGP_SCENE_MENU;
    else if (hud.dead || (panel_bits & DM_PS_DEAD)) scene = DM_TGP_SCENE_DEAD;
    else if (panel_open || panel_bits) scene = DM_TGP_SCENE_PANEL;
    else scene = DM_TGP_SCENE_GAME;

    int area = hud.valid ? hud.area : 0;
    int changed = !provider->have_context || provider->last_scene != scene ||
                  provider->last_ingame != ingame || provider->last_panel != panel_bits ||
                  provider->last_ui_identity != ui_gen || provider->last_area != area;
    if (changed) {
        if (provider->have_context) {
            provider->scene_epoch++;
            if (provider->last_ingame != ingame || provider->last_area != area)
                provider->world_epoch++;
        }
        provider->have_context = 1;
        provider->last_scene = scene;
        provider->last_ingame = ingame;
        provider->last_panel = panel_bits;
        provider->last_ui_identity = ui_gen;
        provider->last_area = area;
    }

    out->abi_version = DM_TGP_ABI_VERSION;
    out->module_id = provider->module_id;
    out->generation = provider->generation;
    out->scene_epoch = provider->scene_epoch;
    out->world_epoch = provider->world_epoch;
    out->object_generation = ui_gen;
    out->captured_ns = now;
    out->ui_identity = ui_gen;
    out->panel_bits = (uint32_t)panel_bits;
    out->available = 1;
    out->scene = scene;
    out->dialogue_only = 0;   // not proven by the current host state entry
    out->dead = scene == DM_TGP_SCENE_DEAD;
    return 1;
}

const dm_tgp_provider *dm_live_touch_provider_interface(dm_live_touch_provider *provider) {
    return provider ? &provider->iface : NULL;
}
