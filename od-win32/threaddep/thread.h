
#if 1

typedef HANDLE uae_sem_t;
typedef int uae_thread_id;
extern void sem_close (void*);
extern int sem_trywait (void*);
extern void sem_post (void*);
extern void sem_wait (void*t);
extern void sem_init (void*, int manual_reset, int initial_state);
extern int start_penguin (void *(*f)(void *), void *arg, DWORD * foo);
extern void set_thread_priority (int);

#define uae_sem_init sem_init
#define uae_sem_destroy sem_close
#define uae_sem_post sem_post
#define uae_sem_wait sem_wait
#define uae_sem_trywait sem_trywait
#define uae_start_thread start_penguin

#include "commpipe.h"

#else

/*
  * UAE - The Un*x Amiga Emulator
  *
  * Threading support, using SDL
  *
  * Copyright 1997, 2001 Bernd Schmidt
  */

#include "SDL.h"
#include "SDL_thread.h"

/* Sempahores. We use POSIX semaphores; if you are porting this to a machine
 * with different ones, make them look like POSIX semaphores. */
typedef SDL_sem *uae_sem_t;

#define uae_sem_init(PSEM, DUMMY, INIT) do { \
    *PSEM = SDL_CreateSemaphore (INIT); \
} while (0)
#define uae_sem_destroy(PSEM) SDL_DestroySemaphore (*PSEM)
#define uae_sem_post(PSEM) SDL_SemPost (*PSEM)
#define uae_sem_wait(PSEM) SDL_SemWait (*PSEM)
#define uae_sem_trywait(PSEM) SDL_SemTryWait (*PSEM)
#define uae_sem_getvalue(PSEM) SDL_SemValue (*PSEM)

#include "commpipe.h"
extern void set_thread_priority (int);

typedef SDL_Thread *uae_thread_id;
#define BAD_THREAD NULL

static __inline__ int uae_start_thread (void *(*f) (void *), void *arg, uae_thread_id *foo)
{
    *foo = SDL_CreateThread ((int (*)(void *))f, arg);
    return *foo == 0;
}

/* Do nothing; thread exits if thread function returns.  */
#define UAE_THREAD_EXIT do {} while (0)

#endif