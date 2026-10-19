#pragma once

// 采样绑定的有界分类，不含资源内容、地址、设备标识。
// Bounded sampled-binding classification without resource contents, addresses or device identifiers.
typedef enum {
    TEXTURE_DIAG_OK = 0,
    TEXTURE_DIAG_UNBOUND,
    TEXTURE_DIAG_MISSING,
    TEXTURE_DIAG_UNSUPPORTED_FORMAT,
    TEXTURE_DIAG_UNSAMPLED_FORMAT,
    TEXTURE_DIAG_VIEW_FORMAT,
    TEXTURE_DIAG_DEPTH,
    TEXTURE_DIAG_LINEAR,
} texture_diag_kind;

static inline const char *texture_diag_name(unsigned kind) {
    switch (kind) {
        case TEXTURE_DIAG_UNBOUND: return "unbound";
        case TEXTURE_DIAG_MISSING: return "missing";
        case TEXTURE_DIAG_UNSUPPORTED_FORMAT: return "unsupported_format";
        case TEXTURE_DIAG_UNSAMPLED_FORMAT: return "unsampled_format";
        case TEXTURE_DIAG_VIEW_FORMAT: return "view_format";
        case TEXTURE_DIAG_DEPTH: return "depth";
        case TEXTURE_DIAG_LINEAR: return "linear";
        default: return "ok";
    }
}

// 顺序即诊断优先级：未绑定 > 未登记 > 格式未知/不可采样 > 格式视图 > 深度 > 线性。
// Priority: unbound > unregistered > unknown/unsampled format > view format > depth > linear.
static inline unsigned texture_diag_classify(int valid, unsigned handle, unsigned format,
                                             int format_known, int sampled,
                                             int view_set, unsigned view_format, unsigned flags) {
    if (!handle) return TEXTURE_DIAG_UNBOUND;
    if (!valid) return TEXTURE_DIAG_MISSING;
    if (!format_known) return TEXTURE_DIAG_UNSUPPORTED_FORMAT;
    if (!sampled) return TEXTURE_DIAG_UNSAMPLED_FORMAT;
    if (view_set && view_format != format) return TEXTURE_DIAG_VIEW_FORMAT;
    if (format >= 0x31 && format <= 0x36) return TEXTURE_DIAG_DEPTH;
    if (flags & 0x10) return TEXTURE_DIAG_LINEAR;
    return TEXTURE_DIAG_OK;
}
