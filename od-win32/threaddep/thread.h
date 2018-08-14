typedef HANDLE uae_sem_t;
typedef HANDLE uae_thread_id;

extern void uae_sem_destroy(uae_sem_t*);
extern int uae_sem_trywait(uae_sem_t*);
extern int uae_sem_trywait_delay(uae_sem_t*, int);
extern void uae_sem_post(uae_sem_t*);
extern void uae_sem_wait(uae_sem_t*t);
extern void uae_sem_init(uae_sem_t*, int manual_reset, int initial_state);
extern int uae_start_thread(const TCHAR *name, void *(*f)(void *), void *arg, uae_thread_id *thread);
extern int uae_start_thread_fast(void *(*f)(void *), void *arg, uae_thread_id *thread);
extern void uae_end_thread(uae_thread_id *thread);
extern void uae_set_thread_priority(uae_thread_id *, int);
extern uae_thread_id uae_thread_get_id(void);

#include "commpipe.h"

STATIC_INLINE void uae_wait_thread(uae_thread_id tid)
{
    WaitForSingleObject (tid, INFINITE);
    CloseHandle (tid);
}
