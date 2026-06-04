#include "sysconfig.h"
#include "sysdeps.h"

#include "threaddep/thread.h"
#include "uae.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void unix_sem_timeout_from_now(struct timespec *ts, int ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

void uae_sem_init(uae_sem_t *sem, int manual_reset, int initial_state)
{
    if (!sem) {
        return;
    }
    if (*sem) {
        pthread_mutex_lock(&(*sem)->mutex);
        (*sem)->manual_reset = manual_reset ? 1 : 0;
        (*sem)->signaled = initial_state ? 1 : 0;
        if ((*sem)->signaled) {
            pthread_cond_broadcast(&(*sem)->cond);
        }
        pthread_mutex_unlock(&(*sem)->mutex);
        return;
    }
    *sem = (uae_sem_t)calloc(1, sizeof(**sem));
    if (!*sem) {
        abort();
    }
    pthread_mutex_init(&(*sem)->mutex, NULL);
    pthread_cond_init(&(*sem)->cond, NULL);
    (*sem)->manual_reset = manual_reset ? 1 : 0;
    (*sem)->signaled = initial_state ? 1 : 0;
}

void uae_sem_destroy(uae_sem_t *sem)
{
    if (!sem || !*sem) {
        return;
    }
    pthread_cond_destroy(&(*sem)->cond);
    pthread_mutex_destroy(&(*sem)->mutex);
    free(*sem);
    *sem = NULL;
}

void uae_sem_post(uae_sem_t *sem)
{
    if (!sem || !*sem) {
        return;
    }
    pthread_mutex_lock(&(*sem)->mutex);
    (*sem)->signaled = 1;
    if ((*sem)->manual_reset) {
        pthread_cond_broadcast(&(*sem)->cond);
    } else {
        pthread_cond_signal(&(*sem)->cond);
    }
    pthread_mutex_unlock(&(*sem)->mutex);
}

void uae_sem_unpost(uae_sem_t *sem)
{
    if (!sem || !*sem) {
        return;
    }
    pthread_mutex_lock(&(*sem)->mutex);
    (*sem)->signaled = 0;
    pthread_mutex_unlock(&(*sem)->mutex);
}

void uae_sem_wait(uae_sem_t *sem)
{
    if (!sem || !*sem) {
        return;
    }
    pthread_mutex_lock(&(*sem)->mutex);
    while (!(*sem)->signaled) {
        pthread_cond_wait(&(*sem)->cond, &(*sem)->mutex);
    }
    if (!(*sem)->manual_reset) {
        (*sem)->signaled = 0;
    }
    pthread_mutex_unlock(&(*sem)->mutex);
}

int uae_sem_trywait(uae_sem_t *sem)
{
    return uae_sem_trywait_delay(sem, 0);
}

int uae_sem_trywait_delay(uae_sem_t *sem, int ms)
{
    int result = -1;
    if (!sem || !*sem) {
        return result;
    }
    pthread_mutex_lock(&(*sem)->mutex);
    if (!(*sem)->signaled && ms != 0) {
        if (ms < 0) {
            while (!(*sem)->signaled) {
                pthread_cond_wait(&(*sem)->cond, &(*sem)->mutex);
            }
        } else {
            struct timespec ts;
            unix_sem_timeout_from_now(&ts, ms);
            while (!(*sem)->signaled) {
                int err = pthread_cond_timedwait(&(*sem)->cond, &(*sem)->mutex, &ts);
                if (err == ETIMEDOUT) {
                    break;
                }
            }
        }
    }
    if ((*sem)->signaled) {
        if (!(*sem)->manual_reset) {
            (*sem)->signaled = 0;
        }
        result = 0;
    }
    pthread_mutex_unlock(&(*sem)->mutex);
    return result;
}

struct thread_start_data {
    uae_thread_function fn;
    void *arg;
};

static void *thread_entry(void *data)
{
    thread_start_data *tsd = (thread_start_data*)data;
    uae_thread_function fn = tsd->fn;
    void *arg = tsd->arg;
    free(tsd);
    fn(arg);
    return NULL;
}

int uae_start_thread(const TCHAR *, uae_thread_function f, void *arg, uae_thread_id *thread)
{
    thread_start_data *tsd = (thread_start_data*)calloc(1, sizeof(*tsd));
    tsd->fn = f;
    tsd->arg = arg;
    pthread_t tid;
    if (pthread_create(&tid, NULL, thread_entry, tsd) != 0) {
        free(tsd);
        return 0;
    }
    if (thread) {
        *thread = tid;
    } else {
        pthread_detach(tid);
    }
    return 1;
}

int uae_start_thread_fast(uae_thread_function f, void *arg, uae_thread_id *thread)
{
    return uae_start_thread(NULL, f, arg, thread);
}

void uae_end_thread(uae_thread_id *thread)
{
    if (thread) {
        *thread = 0;
    }
}

void uae_set_thread_priority(uae_thread_id *, int)
{
}

uae_thread_id uae_thread_get_id(void)
{
    return pthread_self();
}
