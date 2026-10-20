/* Owned shader fixture assembled from qualcomm_ubo.spvasm. */
#include "nvn_qualcomm_ubo.h"
#include <stdio.h>
static const uint32_t fixture[]={
    0x07230203u,0x00010300u,0x00070000u,0x0000001bu,0x00000000u,0x00020011u,0x00000001u,0x0003000eu,
    0x00000000u,0x00000001u,0x0007000fu,0x00000000u,0x00000001u,0x6e69616du,0x00000000u,0x00000002u,
    0x00000003u,0x00040047u,0x00000002u,0x0000000bu,0x0000002au,0x00040047u,0x00000003u,0x0000000bu,
    0x00000000u,0x00040047u,0x00000004u,0x00000006u,0x00000010u,0x00050048u,0x00000005u,0x00000000u,
    0x00000023u,0x00000000u,0x00030047u,0x00000005u,0x00000002u,0x00040047u,0x00000006u,0x00000022u,
    0x00000000u,0x00040047u,0x00000006u,0x00000021u,0x00000003u,0x00020013u,0x00000007u,0x00030021u,
    0x00000008u,0x00000007u,0x00040015u,0x00000009u,0x00000020u,0x00000000u,0x00040015u,0x0000000au,
    0x00000020u,0x00000001u,0x00030016u,0x0000000bu,0x00000020u,0x00040017u,0x0000000cu,0x0000000bu,
    0x00000004u,0x0004002bu,0x00000009u,0x0000000du,0x00000000u,0x0004002bu,0x00000009u,0x0000000eu,
    0x00000004u,0x0004002bu,0x0000000au,0x0000000fu,0x00000003u,0x0004002bu,0x0000000bu,0x00000010u,
    0x3f800000u,0x0004001cu,0x00000004u,0x0000000cu,0x0000000eu,0x0003001eu,0x00000005u,0x00000004u,
    0x00040020u,0x00000011u,0x00000002u,0x00000005u,0x00040020u,0x00000012u,0x00000002u,0x0000000bu,
    0x00040020u,0x00000013u,0x00000001u,0x0000000au,0x00040020u,0x00000014u,0x00000003u,0x0000000cu,
    0x0004003bu,0x00000011u,0x00000006u,0x00000002u,0x0004003bu,0x00000013u,0x00000002u,0x00000001u,
    0x0004003bu,0x00000014u,0x00000003u,0x00000003u,0x00050036u,0x00000007u,0x00000001u,0x00000000u,
    0x00000008u,0x000200f8u,0x00000015u,0x0004003du,0x0000000au,0x00000016u,0x00000002u,0x000500c7u,
    0x0000000au,0x00000017u,0x00000016u,0x0000000fu,0x00070041u,0x00000012u,0x00000018u,0x00000006u,
    0x0000000du,0x0000000du,0x00000017u,0x0004003du,0x0000000bu,0x00000019u,0x00000018u,0x00070050u,
    0x0000000cu,0x0000001au,0x00000019u,0x00000019u,0x00000019u,0x00000010u,0x0003003eu,0x00000003u,
    0x0000001au,0x000100fdu,0x00010038u,
};
static int failures;
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#c);failures++;}}while(0)
static uint8_t *copy(void){void *p=malloc(sizeof fixture);memcpy(p,fixture,sizeof fixture);return p;}
static size_t find_op(uint32_t *w,size_t words,unsigned op){for(size_t i=5;i<words;i+=w[i]>>16)if((w[i]&65535)==op)return i;return 0;}
int main(int argc,char **argv){
    if(argc==5&&!strcmp(argv[1],"apply")){
        FILE *f=fopen(argv[2],"rb");if(!f)return 2;fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);
        uint8_t *p=malloc(n);if(fread(p,1,n,f)!=n)return 3;fclose(f);unsigned count=0;
        uint8_t *q=dm_skin_ubo_apply(p,&n,&count,strtoull(argv[4],NULL,0));
        if(!q){free(p);return 4;}f=fopen(argv[3],"wb");if(!f)return 5;fwrite(q,1,n,f);fclose(f);free(q);printf("%u\n",count);return 0;
    }
    CHECK(dm_qualcomm_ubo_should_lower(1,0,0));CHECK(dm_qualcomm_ubo_should_lower(1,1,0));
    CHECK(!dm_qualcomm_ubo_should_lower(0,0,0));CHECK(!dm_qualcomm_ubo_should_lower(1,5,0));CHECK(!dm_qualcomm_ubo_should_lower(1,0,1));
    size_t n=sizeof fixture;unsigned count=0;uint8_t *p=copy(),*q=dm_skin_ubo_apply(p,&n,&count,UINT64_C(1)<<3);
    CHECK(q&&count==1&&n>sizeof fixture);
    if(q){uint32_t *out=(void*)q;size_t at=find_op(out,n/4,77);CHECK(at&&out[at+1]==fixture[find_op((uint32_t*)fixture,sizeof fixture/4,65)+8]);
        size_t access=find_op(out,n/4,65);CHECK(access&&(out[access]>>16)==6);
        size_t original=find_op((uint32_t*)fixture,sizeof fixture/4,65);
        CHECK(out[access+3]==fixture[original+3]&&out[access+4]==fixture[original+4]&&out[access+5]==fixture[original+5]);
        CHECK(at&&out[at+2]==fixture[original+9]);
        if(argc==3&&!strcmp(argv[1],"emit")){FILE *f=fopen(argv[2],"wb");if(!f)return 6;fwrite(q,1,n,f);fclose(f);}free(q);}
    p=copy();n=sizeof fixture;count=99;q=dm_skin_ubo_apply(p,&n,&count,0);CHECK(q==p&&count==0&&n==sizeof fixture);free(p);
    p=copy();uint32_t *v=(void*)p;size_t access=find_op(v,sizeof fixture/4,65);v[access+6]=v[access+4];n=sizeof fixture;
    q=dm_skin_ubo_apply(p,&n,&count,UINT64_C(1)<<3);CHECK(q==p&&count==0);free(p);
    // A predeclared Uniform vec4 pointer must be reused, not redeclared.
    n=sizeof fixture+16;p=malloc(n);size_t function=find_op((uint32_t*)fixture,sizeof fixture/4,54);
    memcpy(p,fixture,function*4);v=(void*)p;
    uint32_t vector_type=0;for(size_t i=5;i<sizeof fixture/4;i+=fixture[i]>>16)if((fixture[i]&65535)==23)vector_type=fixture[i+1];
    uint32_t ptr_id=fixture[3];uint32_t ptr_type[]={4u<<16|32u,ptr_id,2,vector_type};
    memcpy(v+function,ptr_type,16);memcpy(v+function+4,fixture+function,sizeof fixture-function*4);v[3]++;
    q=dm_skin_ubo_apply(p,&n,&count,UINT64_C(1)<<3);CHECK(q&&count==1);
    if(q){unsigned declarations=0;v=(void*)q;for(size_t i=5;i<n/4;i+=v[i]>>16)if((v[i]&65535)==32&&v[i+1]==ptr_id)declarations++;CHECK(declarations==1);free(q);}

    // A second scalar-pointer use cannot be left undefined by rewriting the first.
    n=sizeof fixture+16;p=malloc(n);access=find_op((uint32_t*)fixture,sizeof fixture/4,65);size_t after=access+11;
    memcpy(p,fixture,after*4);v=(void*)p;uint32_t extra[]={4u<<16|61u,fixture[access+8],fixture[3],fixture[access+2]};
    memcpy(v+after,extra,16);memcpy(v+after+4,fixture+after,sizeof fixture-after*4);v[3]++;
    q=dm_skin_ubo_apply(p,&n,&count,UINT64_C(1)<<3);CHECK(q==p&&count==0);free(p);

    // Truncated instructions are rejected without changing caller ownership.
    p=copy();v=(void*)p;v[5]=0;n=sizeof fixture;q=dm_skin_ubo_apply(p,&n,&count,UINT64_C(1)<<3);CHECK(!q&&count==0);free(p);
    p=copy();v=(void*)p;access=find_op(v,sizeof fixture/4,65);v[access+7]=(100u<<16)|61u;n=sizeof fixture;
    q=dm_skin_ubo_apply(p,&n,&count,UINT64_C(1)<<3);CHECK(!q&&count==0);free(p);
    if(failures)return 1;puts("PASS Qualcomm UBO: dynamic extraction, constants, bank exclusion, driver/stage policy, malformed ownership");return 0;
}
