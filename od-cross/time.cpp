#include <stdlib.h>
#include "sysdeps.h"
#include "uae/time.h"

static int64_t uae_time_epoch;

static int64_t time_ns() {
#ifdef _WIN32
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (uint64_t)((double)counter.QuadPart / frequency.QuadPart * 1e9);
#else
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return (uint64_t)(tp.tv_sec * 1e9 + tp.tv_nsec);
#endif
}

static int64_t time_us() {
    return time_ns() / 1000;
}

uae_time_t uae_time(void) {
	int64_t t = time_us() - uae_time_epoch;
	// Will overflow in 49.71 days. Whether that is a problem depends on usage.
	// Should go through all old uses of read_processor_time / uae_time and
	// make sure to use overflow-safe code or move to 64-bit timestamps.
	return (uae_time_t) t;
}

int64_t uae_time_us(void) {
	// We subtrach epoch here so that this function uses the same epoch as
	// the 32-bit uae_time legacy function. Maybe not necessary.
	return time_us() - uae_time_epoch;
}

void uae_time_init() {
	if (uae_time_epoch == 0) {
		uae_time_epoch = uae_time_us();
	}
}

