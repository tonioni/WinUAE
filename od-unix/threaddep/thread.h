#ifndef WINUAE_OD_UNIX_THREADDEP_THREAD_H
#define WINUAE_OD_UNIX_THREADDEP_THREAD_H

#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include "uae/types.h"

typedef pthread_t uae_thread_id;

struct uae_unix_sem {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int signaled;
    int manual_reset;
};

typedef struct uae_unix_sem* uae_sem_t;

typedef void (*uae_thread_function)(void *);

void uae_sem_destroy(uae_sem_t*);
int uae_sem_trywait(uae_sem_t*);
int uae_sem_trywait_delay(uae_sem_t*, int);
void uae_sem_post(uae_sem_t*);
void uae_sem_unpost(uae_sem_t*);
void uae_sem_wait(uae_sem_t*);
void uae_sem_init(uae_sem_t*, int manual_reset, int initial_state);

int uae_start_thread(const TCHAR *name, uae_thread_function f, void *arg, uae_thread_id *thread);
int uae_start_thread_fast(uae_thread_function f, void *arg, uae_thread_id *thread);
void uae_end_thread(uae_thread_id *thread);
void uae_set_thread_priority(uae_thread_id *, int);
uae_thread_id uae_thread_get_id(void);

static inline void uae_wait_thread(uae_thread_id tid)
{
    pthread_join(tid, NULL);
}

#define BAD_THREAD 0
#define UAE_THREAD_EXIT do { return; } while (0)

#include "commpipe.h"

#endif /* WINUAE_OD_UNIX_THREADDEP_THREAD_H */
