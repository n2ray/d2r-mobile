// #12 反射消费方回归：正例在真实 GPU（宿主 MoltenVK）上执行并独立复算数值；负例全部在创建 Vulkan 对象之前被拒。
// #12 reflection consumer regression: the positive case runs on a real GPU (host MoltenVK) and is recomputed
// independently; every negative case must be rejected before any Vulkan object exists.
#include "../vulkan/vk_shader_reflect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define EXPECT(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define W 8u
#define H 4u
#define WORDS (W * H)

static char g_dir[512];
static const char *g_spv, *g_res;

static char *path_of(const char *name) {
    static char buf[640];
    snprintf(buf, sizeof buf, "%s/%s", g_dir, name);
    return buf;
}
static uint8_t *read_all(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t *p = malloc((size_t)n ? (size_t)n : 1);
    if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return p;
}
static int write_all(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}
// 用等长的就地替换改写 JSON（测试夹具变异，不是生产解析） | equal-length in-place JSON edit for fixture mutation
static int mutate_json(const char *src, const char *from, const char *to, const char *out) {
    size_t n;
    char *text = (char *)read_all(src, &n);
    if (!text) return 0;
    if (strlen(from) != strlen(to)) { free(text); return 0; }
    char *hit = strstr(text, from);
    if (!hit) { free(text); return 0; }
    memcpy(hit, to, strlen(to));
    int ok = write_all(out, (const uint8_t *)text, n);
    free(text);
    return ok;
}
static int write_json(const char *out, const char *text) { return write_all(out, (const uint8_t *)text, strlen(text)); }

// 找到第 n 个 opcode 指令并把指定操作数改成 value（只用于夹具变异）。
// Find the n-th instruction with `op` and overwrite one operand; fixture mutation only.
static int mutate_word(uint8_t *code, size_t size, uint16_t op, int nth, int operand, uint32_t value) {
    uint32_t *w = (uint32_t *)code;
    size_t words = size / 4;
    size_t i = 5;
    int seen = 0;
    while (i < words) {
        uint16_t got = (uint16_t)(w[i] & 0xffff), wc = (uint16_t)(w[i] >> 16);
        if (!wc || i + wc > words) return 0;
        if (got == op && seen++ == nth) {
            if (operand < 0 || operand >= (int)wc - 1) return 0;
            w[i + 1 + operand] = value;
            return 1;
        }
        i += wc;
    }
    return 0;
}
static int mutate_decor(uint8_t *code, size_t size, uint32_t dec, uint32_t value) {
    uint32_t *w = (uint32_t *)code;
    size_t words = size / 4;
    for (size_t i = 5; i < words;) {
        uint16_t op = (uint16_t)(w[i] & 0xffff), wc = (uint16_t)(w[i] >> 16);
        if (!wc || i + wc > words) return 0;
        if (op == 71 && wc >= 4 && w[i + 2] == dec) { w[i + 3] = value; return 1; }
        i += wc;
    }
    return 0;
}

static void expect_reject(const char *label, const char *spv, const char *res, const char *needle) {
    dm_vk_shader_plan plan;
    char why[DM_VK_REFLECT_WHY] = {0};
    VkResult r = dm_vk_reflect_load(spv, res, &plan, why, sizeof why);
    checks++;
    if (r == VK_SUCCESS) { failures++; fprintf(stderr, "FAIL %s: unexpectedly accepted\n", label); return; }
    if (!why[0]) { failures++; fprintf(stderr, "FAIL %s: rejected without a diagnostic\n", label); return; }
    if (needle && !strstr(why, needle)) { failures++; fprintf(stderr, "FAIL %s: diagnostic %s lacks %s\n", label, why, needle); }
    printf("reject %-22s %s\n", label, why);
}

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: %s <reflect.spv> <reflect.res.json> <tmpdir>\n", argv[0]); return 2; }
    g_spv = argv[1]; g_res = argv[2];
    snprintf(g_dir, sizeof g_dir, "%s", argv[3]);

    // ---- 正例：解析与交叉核对 | positive: parse and cross-check --------------------------------
    dm_vk_shader_plan plan;
    char why[DM_VK_REFLECT_WHY] = {0};
    VkResult r = dm_vk_reflect_load(g_spv, g_res, &plan, why, sizeof why);
    printf("load: %s %s\n", r == VK_SUCCESS ? "ok" : "FAILED", r == VK_SUCCESS ? plan.code_sha256 : why);
    EXPECT(r == VK_SUCCESS);
    if (r != VK_SUCCESS) { printf("vulkan_reflect: %d checks, %d failures\n", checks, failures); return 1; }
    EXPECT(plan.local[0] == 4 && plan.local[1] == 1 && plan.local[2] == 1);
    EXPECT(plan.push_offset == 0 && plan.push_size == 8);
    EXPECT(plan.binding_count == 1);
    EXPECT(plan.binding_count == 1 && plan.bindings[0].set == 0 && plan.bindings[0].binding == 0 &&
           plan.bindings[0].count == 1 && plan.bindings[0].kind == DM_VK_REFLECT_SBUF);
    EXPECT(plan.code_bytes > 0 && strlen(plan.code_sha256) == 64);
    EXPECT(plan.sidecar_hash_bound == 0);

    // ---- 负例：模块变异 | negative: module mutations ------------------------------------------
    size_t size;
    uint8_t *code = read_all(g_spv, &size);
    EXPECT(code != NULL);
    if (code) {
        uint8_t *m = malloc(size);
        EXPECT(m != NULL);
        if (m) {
            memcpy(m, code, size);
            ((uint32_t *)m)[0] = 0xdeadbeefu;
            EXPECT(write_all(path_of("magic.spv"), m, size));
            expect_reject("magic", path_of("magic.spv"), g_res, "魔数");

            memcpy(m, code, size);
            EXPECT(write_all(path_of("truncated.spv"), m, size - 4));
            expect_reject("truncated", path_of("truncated.spv"), g_res, "OpFunctionEnd");

            memcpy(m, code, size);
            // 入口名 main -> mxin（4 字节 ASCII，长度不变） | entry name main -> mxin, same length
            int renamed = 0;
            for (size_t i = 0; i + 4 < size; i++) {
                if (!memcmp(m + i, "main", 4)) { m[i + 1] = 'x'; renamed = 1; break; }
            }
            EXPECT(renamed);
            EXPECT(write_all(path_of("entry.spv"), m, size));
            expect_reject("entry-name", path_of("entry.spv"), g_res, "入口名");

            memcpy(m, code, size);
            EXPECT(mutate_word(m, size, 16, 0, 2, 0));   // LocalSize x = 0
            EXPECT(write_all(path_of("local.spv"), m, size));
            expect_reject("localsize-zero", path_of("local.spv"), g_res, "LocalSize");

            memcpy(m, code, size);
            EXPECT(mutate_decor(m, size, 34, 1));   // DescriptorSet 0 -> 1
            EXPECT(write_all(path_of("set.spv"), m, size));
            expect_reject("descriptor-set", path_of("set.spv"), g_res, "set");

            memcpy(m, code, size);
            int ptr_changed = 0;
            size_t words = size / 4;
            for (size_t i = 5; i < words;) {
                uint32_t *w = (uint32_t *)m;
                uint16_t op = (uint16_t)(w[i] & 0xffff), wc = (uint16_t)(w[i] >> 16);
                if (!wc || i + wc > words) break;
                if (op == 32 && wc == 4 && w[i + 2] == 12) { w[i + 2] = 2; ptr_changed = 1; break; }
                i += wc;
            }
            EXPECT(ptr_changed);
            EXPECT(write_all(path_of("storage.spv"), m, size));
            expect_reject("storage-class", path_of("storage.spv"), g_res, "存储类");
            free(m);
        }
        free(code);
    }

    // ---- 负例：sidecar 变异 | negative: sidecar mutations -------------------------------------
    EXPECT(mutate_json(g_res, "\"cs\"", "\"vs\"", path_of("stage.json")));
    expect_reject("stage", g_spv, path_of("stage.json"), "stage");
    EXPECT(mutate_json(g_res, "\"binding\": 0", "\"binding\": 5", path_of("binding.json")));
    expect_reject("binding", g_spv, path_of("binding.json"), "绑定");
    EXPECT(mutate_json(g_res, "\"count\": 1", "\"count\": 2", path_of("count.json")));
    expect_reject("count", g_spv, path_of("count.json"), "count");
    EXPECT(mutate_json(g_res, "\"sbufs\"", "\"sbuf_\"", path_of("missing.json")));
    expect_reject("missing-binding", g_spv, path_of("missing.json"), "绑定");
    EXPECT(mutate_json(g_res, "\"offset\": 0", "\"offset\": 4", path_of("pc-offset.json")));
    expect_reject("push-offset", g_spv, path_of("pc-offset.json"), "push constant");
    EXPECT(write_json(path_of("unsupported.json"),
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}],\"textures\":[{\"binding\":0,\"count\":1}]}"));
    expect_reject("textures-unsupported", g_spv, path_of("unsupported.json"), "textures");
    EXPECT(write_json(path_of("no-pc.json"), "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}]}"));
    expect_reject("missing-pc", g_spv, path_of("no-pc.json"), "push constant");
    char bogus[128];
    memset(bogus, '0', 64); bogus[64] = 0;
    char json[512];
    snprintf(json, sizeof json,
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}],\"push_constants\":[{\"offset\":0,\"size\":8}],"
        "\"spirv_sha256\":\"%s\"}", bogus);
    EXPECT(write_json(path_of("digest.json"), json));
    expect_reject("sidecar-digest", g_spv, path_of("digest.json"), "spirv_sha256");
    // 正确的摘要必须被接受，证明摘要路径不是永远拒绝 | a correct digest must pass, so this is not a blanket reject
    char good[600];
    snprintf(good, sizeof good,
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":0,\"count\":1}],\"push_constants\":[{\"offset\":0,\"size\":8}],"
        "\"spirv_sha256\":\"%s\"}", plan.code_sha256);
    EXPECT(write_json(path_of("digest-ok.json"), good));
    dm_vk_shader_plan bound;
    char why2[DM_VK_REFLECT_WHY] = {0};
    EXPECT(dm_vk_reflect_load(g_spv, path_of("digest-ok.json"), &bound, why2, sizeof why2) == VK_SUCCESS);
    EXPECT(bound.sidecar_hash_bound == 1);

    // ---- 正例：真实 GPU 执行 + 独立数值复算 | positive: real GPU execution + independent oracle -----
    dm_vk vk = {0};
    if (dm_vk_create(&vk) != VK_SUCCESS) { fprintf(stderr, "no Vulkan device available\n"); return 1; }
    uint32_t canvas_bytes[WORDS] = {0};
    dm_vk_pixels canvas = {.width = W, .height = H, .bytes = (uint8_t *)canvas_bytes, .capacity = sizeof canvas_bytes};
    uint32_t pc[2] = {7, 3};
    r = dm_vk_reflect_compute(&vk, &plan, g_spv, &canvas, pc, sizeof pc);
    EXPECT(r == VK_SUCCESS);
    int mismatch = 0;
    for (uint32_t i = 0; i < WORDS; i++) if (canvas_bytes[i] != pc[0] + i * pc[1]) mismatch++;
    EXPECT(mismatch == 0);
    printf("oracle run1: %u/%u words mismatched, first=%u expected=%u\n", mismatch, WORDS, canvas_bytes[0], pc[0]);
    pc[0] = 100; pc[1] = 0;
    memset(canvas_bytes, 0, sizeof canvas_bytes);
    EXPECT(dm_vk_reflect_compute(&vk, &plan, g_spv, &canvas, pc, sizeof pc) == VK_SUCCESS);
    mismatch = 0;
    for (uint32_t i = 0; i < WORDS; i++) if (canvas_bytes[i] != 100u) mismatch++;
    EXPECT(mismatch == 0);
    printf("oracle run2: %u/%u words mismatched\n", mismatch, WORDS);
    // push 字节数不符要在 GPU 之前拒绝 | a wrong push size must be rejected before GPU work
    EXPECT(dm_vk_reflect_compute(&vk, &plan, g_spv, &canvas, pc, 4) == VK_ERROR_INITIALIZATION_FAILED);
    dm_vk_destroy(&vk);

    printf("vulkan_reflect: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
