#include <SDL.h>
#include "uae/inline.h"
#include <string.h>
#include "sysconfig.h"
#include <stdlib.h>
#include "sysdeps.h"
#include "threaddep/thread.h"

void uae_sem_destroy(uae_sem_t* sem) {
	if (*sem) {
		SDL_DestroySemaphore((SDL_sem*)sem);
		*sem = nullptr;
	}
}

int uae_sem_trywait(uae_sem_t* sem) {
    return SDL_SemTryWait((SDL_sem*)sem);
}

int uae_sem_trywait_delay(uae_sem_t* sem, int ms) {
    return SDL_SemWaitTimeout((SDL_sem*)sem, ms);
}

void uae_sem_post(uae_sem_t* sem) {
    SDL_SemPost((SDL_sem*)sem);
}

void uae_sem_unpost(uae_sem_t*) {
    printf("uae_sem_unpost not implemented\n");
    exit(0);
}

void uae_sem_wait(uae_sem_t* sem) {
    SDL_SemWait((SDL_sem*)sem);
}

void uae_sem_init(uae_sem_t* sem, int manual_reset, int initial_state) {
	if (*sem) {
		SDL_SemPost((SDL_sem*)sem);
	} else {
		*sem = (uae_sem_t*)SDL_CreateSemaphore(initial_state);
	}
}

// This is needed because SDL runner excepts a function to return 0 but the uae code uses void
struct uae_thread_params {
    void (*f)(void*);
    void* arg;
};

static int thread_runner_wrapper(void* arg) {
    uae_thread_params* params = (uae_thread_params*)arg;
    params->f(params->arg);
    return 0;
}

int uae_start_thread(const TCHAR* name, void (*fn)(void *), void *arg, uae_thread_id* tid) {
	int result = 1;
	if (name != nullptr) {
		write_log("uae_start_thread \"%s\" function at %p arg %p\n", name, fn, arg);
	} else {
		name = "StartThread";
	}

    // This will leak, but hopefully isn't a big deal
	uae_thread_params* params = (uae_thread_params*)malloc(sizeof(uae_thread_params)); 
	params->f = fn;
	params->arg = arg;
	
	SDL_Thread* thread = SDL_CreateThread(thread_runner_wrapper, name, params);
	if (thread == nullptr) {
		printf("ERROR creating thread, %s\n", SDL_GetError());
		result = 0;
	}

	if (tid) {
		*tid = thread;
	}

	return result;
}

int uae_start_thread_fast(void (*fn)(void*), void* arg, uae_thread_id* tid) {
	return uae_start_thread(NULL, fn, arg, tid);
}

void uae_end_thread(uae_thread_id *thread) {
 /*
#ifdef _WIN32
    TerminateThread(SDL_GetThreadID(t), 0);
#end
#ifdef __linux
    pthread_kill(SDL_GetThreadID(t), 0);
#endif
*/
}

void uae_set_thread_priority(uae_thread_id*, int) {
    // it seems that the thread priority input is always
    // set to 1 so we assume that the priority is always
	SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
}

uae_thread_id uae_thread_get_id(void) {
	return (uae_thread_id)SDL_GetThreadID(nullptr);
}


uae_atomic atomic_and(volatile uae_atomic * p, uae_u32 v) {
    return __atomic_and_fetch(p, v, __ATOMIC_SEQ_CST);
}

uae_atomic atomic_or(volatile uae_atomic * p, uae_u32 v) {
    return __atomic_or_fetch(p, v, __ATOMIC_SEQ_CST);
}

uae_atomic atomic_inc(volatile uae_atomic * p) {
    return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST);
}

uae_atomic atomic_dec(volatile uae_atomic * p) {
    return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST);
}

uae_u32 atomic_bit_test_and_reset(volatile uae_atomic * p, uae_u32 v) {
    uae_u32 mask = (1 << v);
    uae_u32 res = __atomic_fetch_and(p, ~mask, __ATOMIC_SEQ_CST);
    return (res & mask);
}

void atomic_set(volatile uae_atomic* p, uae_u32 v) {
    __atomic_store_n(p, v, __ATOMIC_SEQ_CST);
}

void sleep_millis(int ms) {
    SDL_Delay(ms);
}
