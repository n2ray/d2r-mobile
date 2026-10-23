#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "nvn_completion_turn.h"

static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static _Atomic int ready;
static unsigned published;
static void *complete(void *unused) {
    (void)unused;
    assert(!pthread_mutex_lock(&mutex));
    assert(!pthread_cond_broadcast(&condition));
    assert(!pthread_mutex_unlock(&mutex));
    struct timespec pause={0,1000000};nanosleep(&pause,NULL);
    assert(!pthread_mutex_lock(&mutex));
    published++;
    atomic_store_explicit(&ready,0,memory_order_release);
    assert(!pthread_cond_broadcast(&condition));
    assert(!pthread_mutex_unlock(&mutex));
    return NULL;
}
int main(void) {
    for(unsigned i=0;i<100;i++) {
        assert(!pthread_mutex_lock(&mutex));
        atomic_store(&ready,1);
        pthread_t worker;assert(!pthread_create(&worker,NULL,complete,NULL));
        assert(!dm_vk_completion_turn_wait_locked(&ready,&condition,&mutex));
        assert(published==i+1);
        assert(!dm_vk_completion_turn_wait_locked(&ready,&condition,&mutex));
        assert(!pthread_mutex_unlock(&mutex));
        assert(!pthread_join(worker,NULL));
    }
    puts("PASS completion turn: releases lock, ignores spurious wake, publication precedes next batch, no pending completion proceeds");
}
