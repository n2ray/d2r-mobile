#pragma once
#include <pthread.h>
#include <stdatomic.h>

static inline int dm_vk_completion_turn_wait_locked(_Atomic int *ready,pthread_cond_t *condition,pthread_mutex_t *mutex) {
    while(atomic_load_explicit(ready,memory_order_acquire)) {
        int error=pthread_cond_wait(condition,mutex);
        if(error)return error;
    }
    return 0;
}
