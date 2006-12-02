
typedef HANDLE uae_sem_t;
typedef HANDLE uae_thread_id;

extern void uae_sem_destroy (uae_sem_t*);
extern int uae_sem_trywait (uae_sem_t*);
extern void uae_sem_post (uae_sem_t*);
extern void uae_sem_wait (uae_sem_t*t);
extern void uae_sem_init (uae_sem_t*, int manual_reset, int initial_state);
extern int uae_start_thread(void *(*f)(void *), void *arg, uae_thread_id *thread);
extern void uae_set_thread_priority (int);

#include "commpipe.h"

STATIC_INLINE uae_wait_thread(uae_thread_id *tid)
{
    WaitForSingleObject(tid, INFINITE);
}
