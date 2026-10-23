#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
typedef int VkResult;
typedef uintptr_t VkPipeline;
typedef uintptr_t VkPipelineLayout;
#define VK_NULL_HANDLE 0
#define VK_SUCCESS 0
typedef struct {int id;} shader_entry;
typedef struct {shader_entry *vs,*fs;unsigned id;} pipeline_key;
typedef struct {int id;} graphics_pass_entry;
static _Atomic int pso_warm_running;
static _Thread_local int resource_create_quiet;
static int device;
static pthread_mutex_t fixture_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fixture_condition=PTHREAD_COND_INITIALIZER;
static unsigned active,maximum,started,completed,allow,counts[100],order[100];
static int fail_threads;
static void die(const char *message){fprintf(stderr,"%s\n",message);abort();}
#define LOG(...) ((void)0)
#define LOG_ALWAYS(...) ((void)0)
static void pso_key_normalize(pipeline_key *out,const pipeline_key *in){memset(out,0,sizeof *out);out->id=in->id;}
static uint64_t graphics_hash(const void *p,size_t size){const uint8_t *b=p;uint64_t n=0;while(size--)n=n*131+*b++;return n;}
static void vkDestroyPipeline(int d,VkPipeline p,void *a){(void)d;(void)p;(void)a;}
static VkResult graphics_pipeline_create(const pipeline_key *key,shader_entry *vs,shader_entry *fs,graphics_pass_entry *pass,VkPipelineLayout layout,VkPipeline *pipeline,int fatal){
    (void)vs;(void)fs;(void)pass;(void)layout;(void)fatal;
    pthread_mutex_lock(&fixture_lock);counts[key->id]++;order[started++]=key->id;
    active++;if(active>maximum)maximum=active;pthread_cond_broadcast(&fixture_condition);
    while(!allow)pthread_cond_wait(&fixture_condition,&fixture_lock);
    active--;completed++;pthread_cond_broadcast(&fixture_condition);pthread_mutex_unlock(&fixture_lock);
    if(key->id==99)return -1;
    *pipeline=key->id+100;return VK_SUCCESS;
}
static int test_create(pthread_t *thread,const pthread_attr_t *attr,void *(*entry)(void *),void *arg){return fail_threads?EAGAIN:pthread_create(thread,attr,entry,arg);}
#define pthread_create test_create
#include "../native/nvn_vulkan_pso_jobs.inc"
#undef pthread_create
static shader_entry vs={1},fs={2};
static graphics_pass_entry pass={1};
static void *demand(void *unused){(void)unused;pipeline_key key={.id=5};VkPipeline pipeline=0;assert(!pso_pipeline_get(&key,&vs,&fs,&pass,1,&pipeline,1));assert(pipeline==105);return NULL;}
int main(int argc,char **argv){
    (void)argv;alarm(15);setenv("DM_PSO_WORKERS","3",1);
    if(argc>1){fail_threads=1;allow=1;pipeline_key key={.id=7};VkPipeline pipeline=0;
        assert(!pso_pipeline_get(&key,&vs,&fs,&pass,1,&pipeline,1)&&pipeline==107);
        assert(!pso_workers&&counts[7]==1);puts("PASS PSO worker creation failure synchronous fallback");return 0;}
    for(unsigned i=1;i<=3;i++){pipeline_key key={.id=i};assert(!pso_pipeline_get(&key,&vs,&fs,&pass,1,NULL,0));}
    pthread_mutex_lock(&fixture_lock);while(started<3)pthread_cond_wait(&fixture_condition,&fixture_lock);
    assert(maximum==3&&completed==0);pthread_mutex_unlock(&fixture_lock);
    for(unsigned i=4;i<=5;i++){pipeline_key key={.id=i};assert(!pso_pipeline_get(&key,&vs,&fs,&pass,1,NULL,0));}
    pthread_t caller;assert(!pthread_create(&caller,NULL,demand,NULL));
    pthread_mutex_lock(&pso_job_mutex);
    for(;;){pso_job *job=pso_jobs;while(job&&job->key.id!=5)job=job->next;if(job&&job->demand)break;pthread_cond_wait(&pso_job_condition,&pso_job_mutex);}
    pthread_mutex_unlock(&pso_job_mutex);
    pthread_mutex_lock(&fixture_lock);allow=1;pthread_cond_broadcast(&fixture_condition);pthread_mutex_unlock(&fixture_lock);
    pthread_join(caller,NULL);
    for(unsigned i=1;i<=5;i++){pipeline_key key={.id=i};VkPipeline pipeline=0;assert(!pso_pipeline_get(&key,&vs,&fs,&pass,1,&pipeline,1)&&pipeline==i+100);}
    assert(!atomic_load(&pso_warm_running)&&order[3]==5);
    for(unsigned i=1;i<=5;i++)assert(counts[i]==1);
    pipeline_key failed={.id=99};VkPipeline pipeline=0;
    assert(pso_pipeline_get(&failed,&vs,&fs,&pass,1,&pipeline,1)==-1);
    assert(pso_pipeline_get(&failed,&vs,&fs,&pass,1,&pipeline,1)==-1&&counts[99]==1);
    puts("PASS PSO jobs: parallel compile, demand priority, deduplication, warm lifetime, wait and error propagation");
}
