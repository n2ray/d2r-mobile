enum {PSO_QUEUED,PSO_BUILDING,PSO_DONE};
typedef struct pso_job {
    pipeline_key key;graphics_pass_entry *pass;VkPipelineLayout layout;
    VkPipeline pipeline;VkResult result;int state,demand,warm;
    struct pso_job *next,*hash_next;
} pso_job;
static pthread_mutex_t pso_job_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pso_job_condition=PTHREAD_COND_INITIALIZER;
static pthread_once_t pso_job_once=PTHREAD_ONCE_INIT;
static pso_job *pso_jobs,*pso_job_index[2048];
static unsigned pso_workers;

static pso_job *pso_job_next(void) {
    pso_job *warm=NULL;
    for(pso_job *job=pso_jobs;job;job=job->next)if(job->state==PSO_QUEUED){
        if(job->demand)return job;
        warm=job;
    }
    return warm;
}
static void *pso_compile_main(void *unused) {
    (void)unused;
#ifdef __APPLE__
    pthread_setname_np("D2R PSO compile");
#else
    pthread_setname_np(pthread_self(),"D2R PSO compile");
#endif
    resource_create_quiet=1;
    for(;;){
        pthread_mutex_lock(&pso_job_mutex);pso_job *job;
        while(!(job=pso_job_next()))pthread_cond_wait(&pso_job_condition,&pso_job_mutex);
        job->state=PSO_BUILDING;pthread_mutex_unlock(&pso_job_mutex);
        VkPipeline pipeline=VK_NULL_HANDLE;
        VkResult result=graphics_pipeline_create(&job->key,job->key.vs,job->key.fs,job->pass,job->layout,&pipeline,0);
        pthread_mutex_lock(&pso_job_mutex);
        job->pipeline=pipeline;job->result=result;job->state=PSO_DONE;
        if(job->warm)atomic_fetch_sub(&pso_warm_running,1);
        pthread_cond_broadcast(&pso_job_condition);pthread_mutex_unlock(&pso_job_mutex);
    }
    return NULL;
}
static void pso_compile_start(void) {
    long cpus=sysconf(_SC_NPROCESSORS_ONLN);unsigned count=cpus>=6?3:cpus>=4?2:1;
    const char *setting=getenv("DM_PSO_WORKERS");
    if(setting&&setting[0]>='1'&&setting[0]<='3'&&!setting[1])count=(unsigned)(setting[0]-'0');
    for(unsigned i=0;i<count;i++){
        pthread_t thread;int error=pthread_create(&thread,NULL,pso_compile_main,NULL);
        if(error){LOG("[vulkan-pso] worker_create_failed=%d",error);break;}
        pthread_detach(thread);pso_workers++;
    }
    LOG_ALWAYS("[vulkan-pso] compile_workers=%u skipped_draws=0",pso_workers);
}
static VkResult pso_pipeline_get(const pipeline_key *key,shader_entry *vs,shader_entry *fs,
        graphics_pass_entry *pass,VkPipelineLayout layout,VkPipeline *pipeline,int wait) {
    pthread_once(&pso_job_once,pso_compile_start);
    if(!pso_workers){
        VkPipeline temporary=VK_NULL_HANDLE;
        VkResult result=graphics_pipeline_create(key,vs,fs,pass,layout,pipeline?pipeline:&temporary,0);
        if(!pipeline&&temporary)vkDestroyPipeline(device,temporary,NULL);
        return result;
    }
    pipeline_key normalized;pso_key_normalize(&normalized,key);normalized.vs=vs;normalized.fs=fs;
    unsigned bucket=graphics_hash(&normalized,sizeof normalized)&2047;
    pthread_mutex_lock(&pso_job_mutex);
    pso_job *job=pso_job_index[bucket];
    while(job&&(job->pass!=pass||memcmp(&job->key,&normalized,sizeof normalized)))job=job->hash_next;
    if(!job){
        job=calloc(1,sizeof *job);if(!job)die("Vulkan PSO job allocation");
        job->key=normalized;job->pass=pass;job->layout=layout;
        job->next=pso_jobs;pso_jobs=job;job->hash_next=pso_job_index[bucket];pso_job_index[bucket]=job;
    }
    if(wait)job->demand=1;
    else if(!job->warm&&job->state!=PSO_DONE){job->warm=1;atomic_fetch_add(&pso_warm_running,1);}
    pthread_cond_broadcast(&pso_job_condition);
    if(!wait){pthread_mutex_unlock(&pso_job_mutex);return VK_SUCCESS;}
    while(job->state!=PSO_DONE)pthread_cond_wait(&pso_job_condition,&pso_job_mutex);
    *pipeline=job->pipeline;VkResult result=job->result;
    pthread_mutex_unlock(&pso_job_mutex);return result;
}
