#include "sysconfig.h"
#include "sysdeps.h"

#include <stdio.h>

#include "threaddep/thread.h"
#include "uae.h"

static bool require_int(const char *label, int actual, int expected)
{
    if (actual == expected) {
        return true;
    }
    fprintf(stderr, "%s: expected %d, got %d\n", label, expected, actual);
    return false;
}

static void check_worker_thread(void *data)
{
    *(bool*)data = is_mainthread();
}

int main()
{
    bool ok = true;

    // Check from a worker first: the first caller need not be the main thread.
    bool worker_is_main = true;
    uae_thread_id worker;
    if (!uae_start_thread("main-thread-test", check_worker_thread,
            &worker_is_main, &worker)) {
        fprintf(stderr, "failed to start main-thread test worker\n");
        return 1;
    }
    uae_wait_thread(worker);
    ok = require_int("worker is not main thread", worker_is_main, 0) && ok;
    ok = require_int("main thread recognized", is_mainthread(), 1) && ok;

    volatile uae_atomic pending = 0x12;
    ok = require_int("atomic or returns previous bits",
        atomic_or(&pending, 0x24), 0x12) && ok;
    ok = require_int("atomic or stores combined bits", pending, 0x36) && ok;
    ok = require_int("atomic and returns previous bits",
        atomic_and(&pending, 0x0f), 0x36) && ok;
    ok = require_int("atomic and stores masked bits", pending, 0x06) && ok;
    // The interrupt dispatcher consumes pending requests this way.
    ok = require_int("claim pending interrupt bits",
        atomic_and(&pending, 0), 0x06) && ok;
    ok = require_int("claimed interrupt bits cleared", pending, 0) && ok;
    ok = require_int("atomic increment returns new value", atomic_inc(&pending), 1) && ok;
    ok = require_int("atomic decrement returns new value", atomic_dec(&pending), 0) && ok;
    atomic_or(&pending, 4);
    ok = require_int("test and reset reports previous bit",
        atomic_bit_test_and_reset(&pending, 2), 1) && ok;
    ok = require_int("test and reset clears bit", pending, 0) && ok;

    uae_sem_t sem = NULL;
    uae_sem_init(&sem, 0, 0);
    ok = require_int("auto initial timeout", uae_sem_trywait(&sem), -1) && ok;
    uae_sem_post(&sem);
    ok = require_int("auto post wakes once", uae_sem_trywait(&sem), 0) && ok;
    ok = require_int("auto post consumed", uae_sem_trywait(&sem), -1) && ok;
    uae_sem_post(&sem);
    uae_sem_post(&sem);
    ok = require_int("auto repeated post coalesces", uae_sem_trywait(&sem), 0) && ok;
    ok = require_int("auto repeated post consumed once", uae_sem_trywait(&sem), -1) && ok;
    uae_sem_post(&sem);
    uae_sem_unpost(&sem);
    ok = require_int("auto unpost clears signal", uae_sem_trywait(&sem), -1) && ok;
    uae_sem_destroy(&sem);
    ok = require_int("destroy clears handle", sem == NULL ? 0 : 1, 0) && ok;

    uae_sem_init(&sem, 1, 0);
    ok = require_int("manual initial timeout", uae_sem_trywait(&sem), -1) && ok;
    uae_sem_post(&sem);
    ok = require_int("manual post first wait", uae_sem_trywait(&sem), 0) && ok;
    ok = require_int("manual remains signaled", uae_sem_trywait(&sem), 0) && ok;
    uae_sem_unpost(&sem);
    ok = require_int("manual unpost clears signal", uae_sem_trywait(&sem), -1) && ok;
    uae_sem_init(&sem, 1, 1);
    ok = require_int("manual reinit existing sets signal", uae_sem_trywait(&sem), 0) && ok;
    uae_sem_destroy(&sem);

    uae_sem_post(&sem);
    uae_sem_unpost(&sem);
    uae_sem_wait(&sem);
    ok = require_int("null trywait fails", uae_sem_trywait(&sem), -1) && ok;

    return ok ? 0 : 1;
}
