// #109 固定布局 uniform 原生消费验收：单个 16 字节 std140 uvec4 + 输出 storage buffer；非平凡输入、独立 CPU
// 数值 oracle、调用者输入不变、sidecar/布局/设备范围/短输入负例。宿主 Vulkan 通过不是 Adreno/真机验收。
// #109 fixed-layout uniform native consumer: one 16-byte std140 uvec4 + output storage buffer; nontrivial input,
// independent CPU oracle, caller input unchanged, and sidecar/layout/device-range/short-input negatives. Host Vulkan
// success is not Adreno/device acceptance.
#include "../vulkan/vk_shader_reflect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
static const uint32_t SENTINEL = 0x5a5a5a5au;

static uint32_t oracle(uint32_t x, uint32_t y, uint32_t z, uint32_t w, uint32_t i) {
    return x * (i + 1u) + (y ^ (z + w * 0x9e3779b9u));   // 与 uniform.comp 同式的无符号 32 位运算
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s uniform.spv uniform.res.json uniform_bad.spv [uniform_array.spv]\n", argv[0]); return 2; }
    dm_vk_shader_plan plan;
    char why[DM_VK_REFLECT_WHY] = {0};
    CHECK(dm_vk_reflect_load(argv[1], argv[2], &plan, why, sizeof why) == VK_SUCCESS);
    CHECK(plan.binding_count == 2 && plan.cbuf_offset == 0 && plan.cbuf_size == 16);
    int cbuf = 0, sbuf = 0;
    for (uint32_t i = 0; i < plan.binding_count; i++) {
        CHECK(plan.bindings[i].set == 0 && plan.bindings[i].count == 1);
        cbuf |= plan.bindings[i].kind == DM_VK_REFLECT_CBUF;
        sbuf |= plan.bindings[i].kind == DM_VK_REFLECT_SBUF;
    }
    CHECK(cbuf && sbuf && plan.push_size == 0);

    // 不支持的成员布局必须在 GPU 前拒绝（有效 GLSL，不是畸形指令）。
    // Unsupported member layout must fail before GPU (valid GLSL, not malformed instructions).
    dm_vk_shader_plan bad;
    char bad_why[DM_VK_REFLECT_WHY] = {0};
    memset(&bad, 0, sizeof bad);
    CHECK(dm_vk_reflect_load(argv[3], argv[2], &bad, bad_why, sizeof bad_why) != VK_SUCCESS);
    CHECK(!bad.code);
    for (int i = 4; i < argc; i++) {
        memset(&bad, 0, sizeof bad);
        CHECK(dm_vk_reflect_load(argv[i], argv[2], &bad, bad_why, sizeof bad_why) != VK_SUCCESS);
        CHECK(!bad.code);
    }
    // module-vs-sidecar：size/offset 必须严格等于模块推导值。
    // Module-vs-sidecar: size/offset must strictly equal module-derived values.
    const char *bad_sidecars[] = {
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":1,\"count\":1}],\"cbufs\":[{\"binding\":0,\"count\":1,\"offset\":0,\"size\":32}]}",
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":1,\"count\":1}],\"cbufs\":[{\"binding\":0,\"count\":1,\"offset\":16,\"size\":16}]}",
        "{\"stage\":\"cs\",\"sbufs\":[{\"binding\":1,\"count\":1}],\"cbufs\":[{\"binding\":0,\"count\":1,\"offset\":0}]}"
    };
    for (unsigned i = 0; i < 3; i++) {
        char path[64]; snprintf(path, sizeof path, "uniform-bad-%u.json", i);
        FILE *f = fopen(path, "wb"); CHECK(f != NULL);
        if (f) { fwrite(bad_sidecars[i], 1, strlen(bad_sidecars[i]), f); fclose(f); }
        memset(&bad, 0, sizeof bad);
        CHECK(dm_vk_reflect_load(argv[1], path, &bad, bad_why, sizeof bad_why) != VK_SUCCESS);
        CHECK(!bad.code);
        remove(path);
    }
    // 设备 uniform-range 上限在创建任何 Vulkan 对象之前检查。
    // Device uniform-range limit is checked before any Vulkan object.
    dm_vk_reflect_limits small;
    memset(&small, 0, sizeof small);
    small.max_workgroup_size[0] = small.max_workgroup_size[1] = small.max_workgroup_size[2] = 1024;
    small.max_invocations = 1024;
    small.max_workgroup_count[0] = small.max_workgroup_count[1] = small.max_workgroup_count[2] = 65535;
    small.max_push_constants = 128;
    small.max_storage_range = 1u << 20;
    small.max_uniform_range = 8;
    CHECK(!dm_vk_reflect_check(&plan, &small, 4, bad_why, sizeof bad_why));

    dm_vk vk = {0};
    VkResult created = dm_vk_create(&vk);
    if (created != VK_SUCCESS) {
        printf("vulkan_reflect_uniform: no Vulkan device; SKIP\n");
        dm_vk_reflect_free(&plan);
        return 77;
    }
    uint32_t input[4] = {0x12345678u, 0x9abcdef0u, 0x0f0f0f0fu, 0xdeadbeefu};
    uint32_t before[4];
    memcpy(before, input, sizeof input);
    uint32_t words[4] = {SENTINEL, SENTINEL, SENTINEL, SENTINEL};
    dm_vk_pixels canvas = {.width = 4, .height = 1, .bytes = (uint8_t *)words, .capacity = sizeof words};

    // 短输入/空指针/画布容量不足：预检失败，画布与调用者输入都不变。
    // Short input/null/canvas-capacity preflight failures leave canvas and caller input unchanged.
    CHECK(dm_vk_reflect_compute_uniform(&vk, &plan, &canvas, input, 15, NULL, 0) != VK_SUCCESS);
    CHECK(dm_vk_reflect_compute_uniform(&vk, &plan, &canvas, NULL, 16, NULL, 0) != VK_SUCCESS);
    for (unsigned i = 0; i < 4; i++) CHECK(words[i] == SENTINEL);
    CHECK(!memcmp(input, before, sizeof input));

    VkResult r = dm_vk_reflect_compute_uniform(&vk, &plan, &canvas, input, 16, NULL, 0);
    CHECK(r == VK_SUCCESS);
    for (uint32_t i = 0; i < 4; i++) CHECK(words[i] == oracle(input[0], input[1], input[2], input[3], i));
    CHECK(!memcmp(input, before, sizeof input));

    dm_vk_reflect_free(&plan);
    dm_vk_destroy(&vk);
    printf("vulkan_reflect_uniform: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
