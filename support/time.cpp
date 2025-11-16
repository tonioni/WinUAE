#include "sysconfig.h"
#include "sysdeps.h"
#include "uae/time.h"
#include "options.h"
#include "events.h"
#include "uae.h"

#ifdef _WIN32

#include <process.h>

static int usedtimermode = 0;
static int qpcdivisor = 0;
static SYSTEM_INFO si;

static frame_time_t read_processor_time_qpf(void)
{
	LARGE_INTEGER counter;
	frame_time_t t;
	QueryPerformanceCounter(&counter);
	if (qpcdivisor == 0)
		t = (frame_time_t) (counter.QuadPart);
	else
		t = (frame_time_t) (counter.QuadPart >> qpcdivisor);
	if (!t)
		t++;
	return t;
}

static frame_time_t read_processor_time_tickcount(void)
{
	frame_time_t v = (frame_time_t)GetTickCount64();
	return v;
}

uae_s64 read_processor_time_rdtsc(void)
{
#ifdef __arm__
	return read_processor_time_qpf();
#else
	return __rdtsc();
#endif
}

uae_time_t uae_time(void)
{
	uae_time_t t;
#if 0
	static int cnt;

	cnt++;
	if (cnt > 1000000) {
		write_log(_T("**************\n"));
		cnt = 0;
	}
#endif
	if (usedtimermode == 1) {
		t = read_processor_time_rdtsc();
	} else if (usedtimermode == 0) {
		t = read_processor_time_qpf();
	} else {
		t = read_processor_time_tickcount();
	}
	return t;
}

uae_s64 read_system_time(void)
{
	return GetTickCount64();
}

static volatile int dummythread_die;

static void _cdecl dummythread(void *dummy)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
	while (!dummythread_die);
}

static uae_s64 win32_read_processor_time(void)
{
#if defined(X86_MSVC_ASSEMBLY)
	uae_u32 foo, bar;
	__asm
	{
		cpuid
			rdtsc
			mov foo, eax
			mov bar, edx
	}
	return (((uae_s64)bar) << 32) | foo;
#else
	return 0;
#endif
}

static void figure_processor_speed_tickcount(void)
{
	syncbase = 1000;
	write_log(_T("CLOCKFREQ: TC %.4fMHz\n"), syncbase / 10000000.0);
}

static void figure_processor_speed_rdtsc(void)
{
	static int freqset;
	frame_time_t clockrate;
	int oldpri;
	HANDLE th;

	if (freqset)
		return;
	th = GetCurrentThread ();
	freqset = 1;
	oldpri = GetThreadPriority(th);
	SetThreadPriority(th, THREAD_PRIORITY_HIGHEST);
	dummythread_die = -1;
	_beginthread(&dummythread, 0, 0);
	sleep_millis(500);
	clockrate = win32_read_processor_time();
	sleep_millis(500);
	clockrate = (win32_read_processor_time() - clockrate) * 2;
	dummythread_die = 0;
	SetThreadPriority(th, oldpri);
	write_log(_T("CLOCKFREQ: RDTSC %.4fMHz\n"), clockrate / 1000000.0);
	syncbase = clockrate >> 6;
}

static void figure_processor_speed_qpf(void)
{
	LARGE_INTEGER freq;
	static LARGE_INTEGER freq2;
	uae_u64 qpfrate;

	if (!QueryPerformanceFrequency (&freq))
		return;
	if (freq.QuadPart == freq2.QuadPart)
		return;
	freq2.QuadPart = freq.QuadPart;
	qpfrate = freq.QuadPart;
	/* limit to 10MHz */
	qpcdivisor = 0;
	while (qpfrate > 10000000) {
		qpfrate >>= 1;
		qpcdivisor++;
	}
	write_log(_T("CLOCKFREQ: QPF %.4fMHz (%.2fMHz, DIV=%d)\n"),
		  freq.QuadPart / 1000000.0,
		  qpfrate / 1000000.0, 1 << qpcdivisor);
	syncbase = qpfrate;
}

void uae_time_calibrate(void)
{
	if (usedtimermode == 1) {
		figure_processor_speed_rdtsc();
	} if (usedtimermode == 0) {
		figure_processor_speed_qpf();
	} else {
		figure_processor_speed_tickcount();
	}
}

void uae_time_use_mode(int mode)
{
	usedtimermode = mode;
}

#elif defined(USE_GLIB)

#include <glib.h>

static gint64 epoch;

uae_time_t uae_time(void)
{
	return (uae_time_t) g_get_monotonic_time();
}

void uae_time_calibrate(void)
{

}

#endif

void uae_time_init(void)
{
	static bool initialized = false;
	if (initialized) {
		return;
	}
#ifdef _WIN32
	GetSystemInfo(&si);
#endif
	uae_time_calibrate();
	initialized = true;
}
