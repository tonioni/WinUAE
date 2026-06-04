#include "sysconfig.h"
#include "sysdeps.h"

#include <errno.h>
#include <time.h>
#include <unistd.h>

#include "options.h"
#include "uae.h"
#include "uae/time.h"
#include "memory.h"
#include "newcpu.h"
#include "host.h"

#include <signal.h>
#include <string.h>

uae_u8 *natmem_offset;
uae_u8 *natmem_reserved;
size_t natmem_reserved_size;

static volatile sig_atomic_t unix_signal_quit_requested;
static volatile sig_atomic_t unix_exit_signal;
static bool unix_signal_quit_dispatched;
static bool unix_signals_installed;

static void unix_alarm_handler(int)
{
    int sig = unix_exit_signal ? unix_exit_signal : SIGTERM;
    _exit(128 + sig);
}

static void unix_signal_handler(int sig)
{
    unix_signal_quit_requested = 1;
    unix_exit_signal = sig;
    quit_program = -UAE_QUIT;
    set_special_exter(SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
    alarm(2);
}

bool unix_host_quit_requested(void)
{
    return unix_signal_quit_requested != 0;
}

void unix_host_check_quit(void)
{
    if (unix_signal_quit_requested && !unix_signal_quit_dispatched) {
        unix_signal_quit_dispatched = true;
        uae_quit();
        set_special(SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
    }
}

void setup_brkhandler(void)
{
    if (unix_signals_installed) {
        return;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = unix_signal_handler;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    sa.sa_handler = unix_alarm_handler;
    sigaction(SIGALRM, &sa, NULL);
    unix_signals_installed = true;
}

int machdep_init(void)
{
    setup_brkhandler();
    uae_time_init();
    return 1;
}

int sleep_resolution;

int sleep_millis(int ms)
{
    if (ms <= 0) {
        unix_host_check_quit();
        return 0;
    }
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        unix_host_check_quit();
        if (unix_host_quit_requested()) {
            break;
        }
    }
    unix_host_check_quit();
    return 0;
}

int sleep_millis_main(int ms)
{
    return sleep_millis(ms);
}

int sleep_millis_amiga(int ms)
{
    return sleep_millis(ms);
}

void sleep_cpu_wakeup(void)
{
}

int target_sleep_nanos(int ns)
{
    if (ns <= 0) {
        unix_host_check_quit();
        return 0;
    }
    struct timespec ts;
    ts.tv_sec = ns / 1000000000;
    ts.tv_nsec = ns % 1000000000;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        unix_host_check_quit();
        if (unix_host_quit_requested()) {
            break;
        }
    }
    unix_host_check_quit();
    return 0;
}

uae_atomic atomic_and(volatile uae_atomic *p, uae_u32 v)
{
    return __sync_and_and_fetch(p, (uae_atomic)v);
}

uae_atomic atomic_or(volatile uae_atomic *p, uae_u32 v)
{
    return __sync_or_and_fetch(p, (uae_atomic)v);
}

uae_atomic atomic_inc(volatile uae_atomic *p)
{
    return __sync_add_and_fetch(p, 1);
}

uae_atomic atomic_dec(volatile uae_atomic *p)
{
    return __sync_sub_and_fetch(p, 1);
}

uae_u32 atomic_bit_test_and_reset(volatile uae_atomic *p, uae_u32 v)
{
    uae_atomic mask = (uae_atomic)1 << v;
    uae_atomic old = __sync_fetch_and_and(p, ~mask);
    return (old & mask) != 0;
}

uae_u32 getlocaltime(void)
{
    return (uae_u32)time(NULL);
}

void target_run(void) {}
void target_quit(void) {}
void target_restart(void) {}
void target_reset(void) {}
void target_cpu_speed(void) {}
void target_addtorecent(const TCHAR*, int) {}
void target_setdefaultstatefilename(const TCHAR*) {}
bool target_osd_keyboard(int) { return false; }
void target_osk_control(int, int, int, int) {}
bool isguiactive(void) { return false; }
bool is_mainthread(void) { return true; }
void fpux_save(int*) {}
void fpux_restore(int*) {}
