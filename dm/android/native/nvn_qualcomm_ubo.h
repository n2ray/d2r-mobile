/* Adapted from dodola/d2r-mobile 4d95a686272ba9624f870938b3bd11abfc09cd14 (MIT), nvn_vk_skin.h.
 * Keep the original shader allocation on no-op/error; a successful replacement
 * owns and frees it. Only reflected guest UBO bindings may be rewritten. */
/* Qualcomm's system compiler miscompiles the dynamic component form of
 * uniform-buffer array accesses (seen in character skinning shaders).
 * Read the vec4 element first, then extract the selected component.  The
 * transformation preserves the access shape, and is restricted to reflected guest UBOs of
 * a vertex or fragment shader on the Qualcomm proprietary driver; shaders without such
 * accesses are returned unchanged. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int dm_skin_is_var(const uint32_t *vars,unsigned n,uint32_t id) {for(unsigned i=0;i<n;i++)if(vars[i]==id)return 1;return 0;}

/* guest_bindings is derived from this backend's DMVK guest UBO reflection.
 * Support data, c0/c1/c2 and unrecognized banks keep their original shape. */
static uint8_t *dm_skin_ubo_apply(uint8_t *bytes,size_t *size,unsigned *replaced,uint64_t guest_bindings) {
    *replaced=0;
    if(!bytes||*size<20||*size%4)return NULL;
    uint32_t *w=(uint32_t *)bytes;size_t words=*size/4;
    if(w[0]!=0x07230203||!w[3]||w[3]>1000000)return NULL;
    uint32_t uint_type=0,float_type=0,vector_type=0,uniform_float_ptr=0,vector_ptr=0,vars[32],allowed[32];unsigned nvars=0,nallowed=0;
    for(size_t o=5;o<words;) {
        unsigned n=w[o]>>16,op=w[o]&65535;if(!n||n>words-o)return NULL;
        if(op==21&&n==4&&w[o+2]==32&&w[o+3]==0)uint_type=w[o+1];
        if(op==22&&n==3&&w[o+2]==32)float_type=w[o+1];
        if(op==23&&n==4&&w[o+2]==float_type&&w[o+3]==4)vector_type=w[o+1];
        if(op==32&&n==4&&w[o+2]==2) {
            if(w[o+3]==float_type)uniform_float_ptr=w[o+1];
            if(w[o+3]==vector_type)vector_ptr=w[o+1];
        }
        if(op==71&&n==4&&w[o+2]==33&&w[o+3]<64&&((guest_bindings>>w[o+3])&1)&&nallowed<32)allowed[nallowed++]=w[o+1]; /* OpDecorate Binding */
        if(op==59&&n>=4&&w[o+3]==2&&nvars<32&&dm_skin_is_var(allowed,nallowed,w[o+2]))vars[nvars++]=w[o+2]; /* OpVariable in Uniform storage */
        o+=n;
    }
    /* A shader without these types or uniform variables has nothing to adapt. */
    if(!uint_type||!float_type||!vector_type||!uniform_float_ptr||!nvars)return bytes;
    uint32_t bound=w[3],next=bound;
    uint8_t *is_const=calloc(bound,1);if(!is_const)return NULL;
    for(size_t o=5;o<words;) {unsigned n=w[o]>>16;if(!n||n>words-o){free(is_const);return NULL;}if((w[o]&65535)==43&&n>=4&&w[o+2]<bound)is_const[w[o+2]]=1;o+=n;}
    int declare_vector_ptr=!vector_ptr;
    if(declare_vector_ptr)vector_ptr=next++;
    size_t replacements=0;
    for(size_t o=5;o<words;) {
        unsigned n=w[o]>>16,op=w[o]&65535;if(!n||n>words-o){free(is_const);return NULL;}
        if(op==65&&n==7&&w[o+1]==uniform_float_ptr&&dm_skin_is_var(vars,nvars,w[o+3])&&!(w[o+6]<bound&&is_const[w[o+6]])) {
            if(o+n>=words){free(is_const);return NULL;}
            unsigned next_n=w[o+n]>>16,next_op=w[o+n]&65535;
            if(next_op!=61||next_n!=4||next_n>words-o-n||w[o+n+1]!=float_type||w[o+n+3]!=w[o+2]){free(is_const);return NULL;}
            if(w[o+2]>=bound||w[o+6]>=bound){free(is_const);return NULL;}
            // Replacing this scalar pointer is safe only when its sole use is
            // the adjacent load. Unknown aliases leave the module unchanged.
            for(size_t at=o+n+next_n;at<words;) {
                unsigned count=w[at]>>16;
                for(unsigned j=1;j<count;j++)if(w[at+j]==w[o+2]){free(is_const);return bytes;}
                at+=count;
            }
            replacements++;
            o+=n;
            o+=next_n;
            continue;
        }
        o+=n;
    }
    if(!replacements){free(is_const);return bytes;}
    if(next>1000000||replacements>(1000000-next)/3||words>SIZE_MAX/4-32||replacements>(SIZE_MAX/4-words-32)/12){free(is_const);return NULL;}
    size_t capacity=words+replacements*12+32;uint32_t *out=malloc(capacity*4);if(!out){free(is_const);return NULL;}
    memcpy(out,w,20);out[3]=next;size_t used=5;int declared=0;
    for(size_t o=5;o<words;) {
        unsigned n=w[o]>>16,op=w[o]&65535;if(op==54&&!declared&&declare_vector_ptr) {
            uint32_t p[]={4u<<16|32,vector_ptr,2,vector_type};
            memcpy(out+used,p,sizeof p);used+=4;declared=1;
        }
        if(op==65&&n==7&&w[o+1]==uniform_float_ptr&&dm_skin_is_var(vars,nvars,w[o+3])&&!(w[o+6]<bound&&is_const[w[o+6]])) {
            unsigned load_n=w[o+n]>>16,load_op=w[o+n]&65535;
            if(load_op!=61||load_n!=4||w[o+n+1]!=float_type||w[o+n+3]!=w[o+2]){free(is_const);free(out);return NULL;}
            uint32_t ptr=next++,vec=next++,idx=next++,load_result=w[o+n+2];
            uint32_t access[]={6u<<16|65,vector_ptr,ptr,w[o+3],w[o+4],w[o+5]};
            uint32_t load[]={4u<<16|61,vector_type,vec,ptr};
            uint32_t bitcast[]={4u<<16|124,uint_type,idx,w[o+6]};
            uint32_t extract[]={5u<<16|77,float_type,load_result,vec,idx};
            memcpy(out+used,access,sizeof access);used+=6;
            memcpy(out+used,load,sizeof load);used+=4;
            memcpy(out+used,bitcast,sizeof bitcast);used+=4;
            memcpy(out+used,extract,sizeof extract);used+=5;
            o+=n;
            o+=load_n;
            continue;
        }
        memcpy(out+used,w+o,n*4);used+=n;o+=n;
    }
    out[3]=next;free(is_const);free(bytes);*size=used*4;*replaced=(unsigned)replacements;return (uint8_t *)out;
}

static int dm_qualcomm_ubo_should_lower(int proprietary, unsigned stage, int disabled) {
    return proprietary && stage <= 1 && !disabled;
}
