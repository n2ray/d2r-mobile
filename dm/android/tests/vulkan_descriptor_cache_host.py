# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
# Run: python3 vulkan_descriptor_cache_host.py <production.inc> <fixture.c> [clang]
"""Compile actual descriptor-cache bodies against owned API shims, without GPU work."""
from pathlib import Path
import re
import subprocess
import sys


def extract(source: str, name: str) -> str:
    """Select exactly one production definition, including its balanced body."""
    matches = list(re.finditer(r"^static [^\n]+\b" + name + r"\([^\n]*\) \{", source, re.M))
    if len(matches) != 1:
        raise RuntimeError(name, "expected one production definition", len(matches))
    match = matches[0]
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main() -> None:
    source = Path(sys.argv[1]).read_text()
    target = Path(sys.argv[2])
    compiler = sys.argv[3] if len(sys.argv) > 3 else "clang"
    prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define die(...) abort()
#define VK_NULL_HANDLE 0
#define DM_CPU_PHASE_DESCRIPTOR 0
#define VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER 6
#define VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 7
#define VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC 8
#define VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER 4
#define VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER 5
typedef uintptr_t VkDescriptorSetLayout;
typedef uintptr_t VkDescriptorSet;
typedef uintptr_t VkBufferView;
typedef uint32_t VkDescriptorType;
typedef struct {uintptr_t buffer;uint64_t offset,range;} VkDescriptorBufferInfo;
typedef struct {uintptr_t sampler,imageView;uint32_t imageLayout,padding;} VkDescriptorImageInfo;
typedef struct {
    uint32_t descriptorType,dstBinding,descriptorCount,ignored;
    VkDescriptorSet dstSet;
    const VkDescriptorBufferInfo *pBufferInfo;
    const VkDescriptorImageInfo *pImageInfo;
    const VkBufferView *pTexelBufferView;
} VkWriteDescriptorSet;
static int descriptor_type_uniform(VkDescriptorType t){return t==6||t==8;}
static uint64_t dm_cpu_phase_begin(void){return 0;}
static void dm_cpu_phase_end(int phase,uint64_t start){(void)phase;(void)start;}
typedef struct uniform_version_chunk {unsigned used;struct uniform_version_chunk *next;} uniform_version_chunk;
static void *uniform_versions[2048];
static uniform_version_chunk *uniform_version_chunks,*uniform_version_current;
static unsigned test_hashes;
'''
    start = source.index("typedef struct descriptor_version {")
    end = source.index("static uniform_version *uniform_versions", start)
    prefix += source[start:end]
    start = source.index("static descriptor_version *descriptor_versions[2048];")
    end = source.index("static void binding_cache_reset", start)
    prefix += source[start:end]
    if "descriptor_front_enabled" not in prefix:
        prefix += "\nstatic int descriptor_front_enabled=1;\n"
    body = extract(source, "binding_cache_reset") + "\n" + extract(source, "descriptor_payload_build")
    if "static void descriptor_payload_hash_build(" in source:
        body += "\n" + extract(source, "descriptor_payload_hash_build")
    body += "\n" + extract(source, "descriptor_cache_find_read")
    body += "\n" + extract(source, "descriptor_cache_store")
    # Count real hash computation in either the old or new production body.
    body = body.replace("descriptor_payload_hash=14695981039346656037ull;",
                        "descriptor_payload_hash=14695981039346656037ull;test_hashes++;")
    suffix = r'''
static void cleanup(void){
    while(descriptor_version_chunks){descriptor_version_chunk *c=descriptor_version_chunks;descriptor_version_chunks=c->next;
        for(unsigned i=0;i<64;i++)free(c->entries[i].payload);free(c);}
    free(descriptor_payload);
}
int main(void){
    VkDescriptorBufferInfo buffer={11,64,256};
    VkDescriptorImageInfo image={21,22,1,0xdeadbeef};
    VkBufferView texel=31;
    VkWriteDescriptorSet writes[3]={
        {.descriptorType=6,.dstBinding=0,.descriptorCount=1,.pBufferInfo=&buffer},
        {.descriptorType=1,.dstBinding=1,.descriptorCount=1,.pImageInfo=&image},
        {.descriptorType=4,.dstBinding=2,.descriptorCount=1,.pTexelBufferView=&texel}};
    assert(!descriptor_cache_find_read(1,writes,3));assert(test_hashes==1);descriptor_cache_store(1,101);
    unsigned hashes=test_hashes;
    assert(descriptor_cache_find_read(1,writes,3)==101);
    if(test_hashes!=hashes){fprintf(stderr,"identical resolved descriptors were rehashed\n");return 1;}
    image.padding=0;writes[0].dstSet=999;writes[0].ignored=17;
    assert(descriptor_cache_find_read(1,writes,3)==101&&test_hashes==hashes);
    descriptor_front_enabled=0;
    assert(descriptor_cache_find_read(1,writes,3)==101&&test_hashes==hashes+1);
    descriptor_front_enabled=1;hashes=test_hashes;
    assert(descriptor_cache_find_read(1,writes,3)==101&&test_hashes==hashes);
    for(unsigned field=0;field<7;field++){
        switch(field){case 0:buffer.buffer++;break;case 1:buffer.offset++;break;case 2:buffer.range++;break;
            case 3:image.sampler++;break;case 4:image.imageView++;break;case 5:image.imageLayout++;break;case 6:texel++;break;}
        assert(!descriptor_cache_find_read(1,writes,3));descriptor_cache_store(1,102+field);
        assert(descriptor_cache_find_read(1,writes,3)==102+field);
    }
    writes[0].dstBinding=4;assert(!descriptor_cache_find_read(1,writes,3));descriptor_cache_store(1,201);
    writes[0].descriptorType=8;assert(!descriptor_cache_find_read(1,writes,3));descriptor_cache_store(1,202);
    writes[0].descriptorCount=0;assert(!descriptor_cache_find_read(1,writes,3));descriptor_cache_store(1,203);
    assert(!descriptor_cache_find_read(2,writes,3));descriptor_cache_store(2,204);
    assert(descriptor_cache_find_read(1,writes,3)==203);
    assert(!descriptor_cache_find_read(1,writes,2));descriptor_cache_store(1,205);
    assert(descriptor_cache_find_read(1,writes,3)==203); /* historical-version fallback */
    for(unsigned layout=3;layout<4100;layout++){
        assert(!descriptor_cache_find_read(layout,writes,3));descriptor_cache_store(layout,layout+1000);
    }
    assert(descriptor_cache_find_read(1,writes,3)==203); /* front-slot collision */
    assert(!descriptor_cache_find_read(9000,NULL,0));descriptor_cache_store(9000,500);
    assert(descriptor_cache_find_read(9000,NULL,0)==500);
    binding_cache_reset();assert(!descriptor_cache_find_read(1,writes,3));
    descriptor_cache_store(1,601);assert(descriptor_cache_find_read(1,writes,3)==601);
    cleanup();puts("actual descriptor cache: exact fields, ignored padding, history, collision, empty, reset PASS");
}
'''
    target.write_text(prefix + body + suffix)
    subprocess.run([compiler,"-std=c11","-UNDEBUG","-Wall","-Wextra","-Wno-unused-variable","-Werror",
                    "-fsanitize=address,undefined",str(target),"-o",str(target.with_suffix(""))],check=True)
    subprocess.run([str(target.with_suffix(""))],check=True)


if __name__ == "__main__":
    main()
