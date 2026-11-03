#ifndef STORAGE_TEST_KERNEL_H
#define STORAGE_TEST_KERNEL_H
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
struct k_mutex { pthread_mutex_t mutex; };
struct k_sem { int count; };
struct k_thread { int unused; };
#define K_MUTEX_DEFINE(name) struct k_mutex name = { PTHREAD_MUTEX_INITIALIZER }
#define K_SEM_DEFINE(name, initial, limit) struct k_sem name = { initial }
#define K_THREAD_STACK_DEFINE(name, size) char name[size]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)
#define K_FOREVER -1
#define K_NO_WAIT 0
static inline int k_mutex_lock(struct k_mutex *m, int timeout) { (void)timeout; return pthread_mutex_lock(&m->mutex); }
static inline int k_mutex_unlock(struct k_mutex *m) { return pthread_mutex_unlock(&m->mutex); }
static inline void k_sem_give(struct k_sem *s) { s->count=1; }
static inline int k_sem_take(struct k_sem *s, int timeout) { (void)timeout; if(s->count){s->count=0;return 0;}return -1; }
static inline void k_thread_create(struct k_thread *t, char *stack, size_t size, void (*entry)(void*,void*,void*), void*a,void*b,void*c,int priority,int options,int delay) { (void)t;(void)stack;(void)size;(void)entry;(void)a;(void)b;(void)c;(void)priority;(void)options;(void)delay; }
#endif
extern int64_t test_uptime_ms;
static inline int64_t k_uptime_get(void) { return test_uptime_ms; }
