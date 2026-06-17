#include "sysconfig.h"
#include "sysdeps.h"

#include <stdio.h>

#include "threaddep/thread.h"

static bool require_int(const char *label, int actual, int expected)
{
    if (actual == expected) {
        return true;
    }
    fprintf(stderr, "%s: expected %d, got %d\n", label, expected, actual);
    return false;
}

int main()
{
    bool ok = true;

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
