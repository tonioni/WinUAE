#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "threaddep/thread.h"
#include "memory.h"
#include "cpuboard.h"
#include "debug.h"
#include "custom.h"
#include "uae.h"
#include "uae/dlopen.h"

#include "uae/ppc.h"

#ifdef WITH_PEARPC_CPU
#include "pearpc/cpu/cpu.h"
#include "pearpc/io/io.h"
#include "pearpc/cpu/cpu_generic/ppc_cpu.h"
#endif

#define PPC_SYNC_WRITE 0
#define PPC_ACCESS_LOG 2

#define TRACE(format, ...) write_log(_T("PPC: ---------------- ") format, ## __VA_ARGS__)

volatile int ppc_state;

static volatile bool ppc_thread_running;
static smp_comm_pipe ppcrequests, ppcreturn;
static smp_comm_pipe ppcquery, ppcreply;
int ppc_cycle_count;
static volatile bool ppc_access;
static volatile int ppc_cpu_lock_state;
static bool ppc_main_thread;
static bool ppc_init_done;

#define CSPPC_PVR 0x00090204
#define BLIZZPPC_PVR 0x00070101

#define KB * 1024
#define MB * (1024 * 1024)

/* Dummy PPC implementation */

static bool PPCCALL dummy_ppc_cpu_init(uint32_t pvr) { return false; }
static void PPCCALL dummy_ppc_cpu_free(void) { }
static void PPCCALL dummy_ppc_cpu_stop(void) { }
static void PPCCALL dummy_ppc_cpu_atomic_raise_ext_exception(void) { }
static void PPCCALL dummy_ppc_cpu_atomic_cancel_ext_exception(void) { }
static void PPCCALL dummy_ppc_cpu_map_memory(uint32_t addr, uint32_t size, void *memory, const char *name) { }
static void PPCCALL dummy_ppc_cpu_set_pc(int cpu, uint32_t value) { }
static void PPCCALL dummy_ppc_cpu_run_continuous(void) { }
static void PPCCALL dummy_ppc_cpu_run_single(int count) { }
static uint64_t PPCCALL dummy_ppc_cpu_get_dec(void) { return 0; }
static void PPCCALL dummy_ppc_cpu_do_dec(int value) { }

/* Functions typedefs for PPC implementation */

typedef bool (PPCCALL *ppc_cpu_init_function)(uint32_t pvr);
typedef void (PPCCALL *ppc_cpu_free_function)(void);
typedef void (PPCCALL *ppc_cpu_stop_function)(void);
typedef void (PPCCALL *ppc_cpu_atomic_raise_ext_exception_function)(void);
typedef void (PPCCALL *ppc_cpu_atomic_cancel_ext_exception_function)(void);
typedef void (PPCCALL *ppc_cpu_map_memory_function)(uint32_t addr, uint32_t size, void *memory, const char *name);
typedef void (PPCCALL *ppc_cpu_set_pc_function)(int cpu, uint32_t value);
typedef void (PPCCALL *ppc_cpu_run_continuous_function)(void);
typedef void (PPCCALL *ppc_cpu_run_single_function)(int count);
typedef uint64_t (PPCCALL *ppc_cpu_get_dec_function)(void);
typedef void (PPCCALL *ppc_cpu_do_dec_function)(int value);

/* Function pointers to active PPC implementation */

static ppc_cpu_init_function g_ppc_cpu_init;
static ppc_cpu_free_function g_ppc_cpu_free;
static ppc_cpu_stop_function g_ppc_cpu_stop;
static ppc_cpu_atomic_raise_ext_exception_function g_ppc_cpu_atomic_raise_ext_exception;
static ppc_cpu_atomic_cancel_ext_exception_function g_ppc_cpu_atomic_cancel_ext_exception;
static ppc_cpu_map_memory_function g_ppc_cpu_map_memory;
static ppc_cpu_set_pc_function g_ppc_cpu_set_pc;
static ppc_cpu_run_continuous_function g_ppc_cpu_run_continuous;
static ppc_cpu_run_single_function g_ppc_cpu_run_single;
static ppc_cpu_get_dec_function g_ppc_cpu_get_dec;
static ppc_cpu_do_dec_function g_ppc_cpu_do_dec;

static void load_dummy_implementation()
{
	write_log(_T("PPC: Loading dummy implementation\n"));
	g_ppc_cpu_init = dummy_ppc_cpu_init;
	g_ppc_cpu_free = dummy_ppc_cpu_free;
	g_ppc_cpu_stop = dummy_ppc_cpu_stop;
	g_ppc_cpu_atomic_raise_ext_exception = dummy_ppc_cpu_atomic_raise_ext_exception;
	g_ppc_cpu_atomic_cancel_ext_exception = dummy_ppc_cpu_atomic_cancel_ext_exception;
	g_ppc_cpu_map_memory = dummy_ppc_cpu_map_memory;
	g_ppc_cpu_set_pc = dummy_ppc_cpu_set_pc;
	g_ppc_cpu_run_continuous = dummy_ppc_cpu_run_continuous;
	g_ppc_cpu_run_single = dummy_ppc_cpu_run_single;
	g_ppc_cpu_get_dec = dummy_ppc_cpu_get_dec;
	g_ppc_cpu_do_dec = dummy_ppc_cpu_do_dec;
}

#ifdef WITH_QEMU_CPU

static void uae_patch_library_ppc(UAE_DLHANDLE handle)
{
	void *ptr;

	ptr = uae_dlsym(handle, "uae_ppc_io_mem_read");
	if (ptr) *((uae_ppc_io_mem_read_function *) ptr) = &uae_ppc_io_mem_read;
	else write_log(_T("WARNING: uae_ppc_io_mem_read not set\n"));

	ptr = uae_dlsym(handle, "uae_ppc_io_mem_write");
	if (ptr) *((uae_ppc_io_mem_write_function *) ptr) = &uae_ppc_io_mem_write;
	else write_log(_T("WARNING: uae_ppc_io_mem_write not set\n"));

	ptr = uae_dlsym(handle, "uae_ppc_io_mem_read64");
	if (ptr) *((uae_ppc_io_mem_read64_function *) ptr) = &uae_ppc_io_mem_read64;
	else write_log(_T("WARNING: uae_ppc_io_mem_read64 not set\n"));

	ptr = uae_dlsym(handle, "uae_ppc_io_mem_write64");
	if (ptr) *((uae_ppc_io_mem_write64_function *) ptr) = &uae_ppc_io_mem_write64;
	else write_log(_T("WARNING: uae_ppc_io_mem_write64 not set\n"));
}

static bool load_qemu_implementation()
{
	write_log(_T("PPC: Loading QEmu implementation\n"));
	// FIXME: replace with a callback to get the qemu path (so it can be
	// implemented separately by WinUAE and FS-UAE.
	UAE_DLHANDLE handle = uae_dlopen(_T("qemu-uae.dll"));
	if (!handle) {
		write_log(_T("Error loading qemu-uae library\n"));
		return false;
	}
	write_log(_T("PPC: Loaded qemu-uae library at %p\n"), handle);

	/* get function pointers */
	g_ppc_cpu_init = (ppc_cpu_init_function) uae_dlsym(handle, "ppc_cpu_init");
	g_ppc_cpu_free = (ppc_cpu_free_function) uae_dlsym(handle, "ppc_cpu_free");
	g_ppc_cpu_stop = (ppc_cpu_stop_function) uae_dlsym(handle, "ppc_cpu_stop");
	g_ppc_cpu_atomic_raise_ext_exception = (ppc_cpu_atomic_raise_ext_exception_function) uae_dlsym(handle, "ppc_cpu_atomic_raise_ext_exception");
	g_ppc_cpu_atomic_cancel_ext_exception = (ppc_cpu_atomic_cancel_ext_exception_function) uae_dlsym(handle, "ppc_cpu_atomic_cancel_ext_exception");
	g_ppc_cpu_map_memory = (ppc_cpu_map_memory_function) uae_dlsym(handle, "ppc_cpu_map_memory");
	g_ppc_cpu_set_pc = (ppc_cpu_set_pc_function) uae_dlsym(handle, "ppc_cpu_set_pc");
	g_ppc_cpu_run_continuous = (ppc_cpu_run_continuous_function) uae_dlsym(handle, "ppc_cpu_run_continuous");
	g_ppc_cpu_run_single = (ppc_cpu_run_single_function) uae_dlsym(handle, "ppc_cpu_run_single");
	g_ppc_cpu_get_dec = (ppc_cpu_get_dec_function) uae_dlsym(handle, "ppc_cpu_get_dec");
	g_ppc_cpu_do_dec = (ppc_cpu_do_dec_function) uae_dlsym(handle, "ppc_cpu_do_dec");

#if 0
	/* register callback functions */
#endif

        uae_patch_library_common(handle);
        uae_patch_library_ppc(handle);
        return true;
}

#endif

#ifdef WITH_PEARPC_CPU

static bool load_pearpc_implementation()
{
	write_log(_T("PPC: Loading PearPC implementation\n"));
	g_ppc_cpu_init = ppc_cpu_init;
	g_ppc_cpu_free = ppc_cpu_free;
	g_ppc_cpu_stop = ppc_cpu_stop;
	g_ppc_cpu_atomic_raise_ext_exception = ppc_cpu_atomic_raise_ext_exception;
	g_ppc_cpu_atomic_cancel_ext_exception = ppc_cpu_atomic_cancel_ext_exception;

	g_ppc_cpu_map_memory = dummy_ppc_cpu_map_memory;

	g_ppc_cpu_set_pc = ppc_cpu_set_pc;
	g_ppc_cpu_run_continuous = ppc_cpu_run_continuous;
	g_ppc_cpu_run_single = ppc_cpu_run_single;
	g_ppc_cpu_get_dec = ppc_cpu_get_dec;
	g_ppc_cpu_do_dec = ppc_cpu_do_dec;
	return true;
}

#endif

static void load_ppc_implementation()
{
	int impl = currprefs.ppc_implementation;
#ifdef WITH_PEARPC_CPU
	if (impl == PPC_IMPLEMENTATION_AUTO || impl == PPC_IMPLEMENTATION_PEARPC) {
		if (load_pearpc_implementation()) {
			return;
		}
	}
#endif
#ifdef WITH_QEMU_CPU
	if (impl == PPC_IMPLEMENTATION_AUTO || impl == PPC_IMPLEMENTATION_QEMU) {
		if (load_qemu_implementation()) {
			return;
		}
	}
#endif
	load_dummy_implementation();
}

static void initialize()
{
	static bool initialized = false;
	if (initialized) {
		return;
	}
	initialized = true;
	load_ppc_implementation();
}

static void map_banks(void)
{
	/*
	 * Use NULL to get callbacks to uae_ppc_io_mem_read/write. Use real
	 * memory address for direct access to RAM banks (looks like this
	 * is needed by JIT, or at least more work is needed on QEmu Side
	 * to allow all memory access to go via callbacks).
	 */

	// FIXME: hack, replace with automatic / dynamic mapping
#if 1
	g_ppc_cpu_map_memory(0x00000000, 2048 KB, NULL,				  "Chip memory");
	g_ppc_cpu_map_memory(0x00BF0000,	 64 KB, NULL,			  "CIA");
	g_ppc_cpu_map_memory(0x00F00000,	256 KB, get_real_address(0x00F00000), "CPUBoard F00000");
	g_ppc_cpu_map_memory(0x00F50000,	192 KB, NULL,				  "CPUBoard IO");
	g_ppc_cpu_map_memory(0x00DF0000,	 64 KB, NULL,				  "Custom chipset");
	g_ppc_cpu_map_memory(0x08000000,	 16 MB, get_real_address(0x08000000), "RAMSEY memory (high)");
	g_ppc_cpu_map_memory(0xFFF00000,	512 KB, get_real_address(0xFFF00000), "CPUBoard MAPROM");
#else
	g_ppc_cpu_map_memory(0x00BF0000,	 64 KB, NULL,				  "CIA");
	g_ppc_cpu_map_memory(0x00F00000,	256 KB, NULL,				  "CPUBoard F00000");
	g_ppc_cpu_map_memory(0x00F50000,	192 KB, NULL,				  "CPUBoard IO");
	g_ppc_cpu_map_memory(0x08000000,	 16 MB, NULL,				  "RAMSEY memory (high)");
	g_ppc_cpu_map_memory(0xFFF00000,	512 KB, get_real_address(0xFFF00000), "CPUBoard MAPROM");
#endif
}

static void uae_ppc_cpu_reset(void)
{
	TRACE(_T("uae_ppc_cpu_reset\n"));
	initialize();
	if (!ppc_init_done) {
		write_log(_T("PPC: Hard reset\n"));
		g_ppc_cpu_init(currprefs.cpuboard_type == BOARD_BLIZZARDPPC ? BLIZZPPC_PVR : CSPPC_PVR);
		map_banks();
		ppc_init_done = true;
	}
	write_log(_T("PPC: Init\n"));
	g_ppc_cpu_set_pc(0, 0xfff00100);
	ppc_cycle_count = 2000;
	ppc_state = PPC_STATE_ACTIVE;
	ppc_cpu_lock_state = 0;
}

static void *ppc_thread(void *v)
{
	for (;;) {
		uae_u32 v = read_comm_pipe_u32_blocking(&ppcrequests);
		if (v == 0xffffffff)
			break;
		uae_ppc_cpu_reset();
		g_ppc_cpu_run_continuous();
		if (ppc_state == PPC_STATE_ACTIVE || ppc_state == PPC_STATE_SLEEP)
			ppc_state = PPC_STATE_STOP;
		write_log(_T("ppc_cpu_run() exited.\n"));
		write_comm_pipe_u32(&ppcreturn, 0, 0);
	}

	ppc_thread_running = false;
	return NULL;
}

void uae_ppc_to_main_thread(void)
{
	TRACE(_T("uae_ppc_to_main_thread\n"));
	if (ppc_thread_running) {
		write_log(_T("PPC: transferring PPC emulation to main thread.\n"));
		uae_ppc_cpu_stop();
		write_comm_pipe_u32(&ppcrequests, 0xffffffff, 1);
		while (ppc_thread_running)
			sleep_millis(2);
		while (comm_pipe_has_data(&ppcquery))
			uae_ppc_poll_queue();
		write_log(_T("PPC: transfer complete.\n"));
	}
	ppc_state = PPC_STATE_ACTIVE;
	ppc_main_thread = true;
}

void uae_ppc_emulate(void)
{
	TRACE(_T("uae_ppc_emulate\n"));
	if (ppc_state == PPC_STATE_ACTIVE || ppc_state == PPC_STATE_SLEEP)
		g_ppc_cpu_run_single(10);
}

bool uae_ppc_poll_queue(void)
{
	// ppc locked?
	if (ppc_cpu_lock_state < 0)
		return false;

	if (comm_pipe_has_data(&ppcquery)) {
		ppc_access = true;
		uae_u32 addr = read_comm_pipe_u32_blocking(&ppcquery);
		uae_u32 size = read_comm_pipe_u32_blocking(&ppcquery);
		uae_u32 data = 0, data2 = 0;
		if (size & 0x80) {
			if (size & 0x08)
				data2 = read_comm_pipe_u32_blocking(&ppcquery);
			data = read_comm_pipe_u32_blocking(&ppcquery);
			switch (size & 127)
			{
			case 8:
				put_long(addr + 0, data2);
				put_long(addr + 4, data);
				break;
			case 4:
				put_long(addr, data);
				break;
			case 2:
				put_word(addr, data);
				break;
			case 1:
				put_byte(addr, data);
				break;
			}
#if PPC_SYNC_WRITE
			write_comm_pipe_u32(&ppcreply, 0, 1);
#else
			read_comm_pipe_u32_blocking(&ppcquery);
#endif
		} else {
			switch (size & 127)
			{
			case 8:
				data2 = get_long(addr + 0);
				data = get_long(addr + 4);
				break;
			case 4:
				data = get_long(addr);
				break;
			case 2:
				data = get_word(addr);
				break;
			case 1:
				data = get_byte(addr);
				break;
			}
			if (size & 0x08)
				write_comm_pipe_u32(&ppcreply, data2, 0);
			write_comm_pipe_u32(&ppcreply, data, 1);
		}
		ppc_access = false;
	}
	if (ppc_cpu_lock_state > 0)
		return true;
	return false;
}

void uae_ppc_sync (void)
{
	while (ppc_thread_running && comm_pipe_has_data(&ppcquery));
}

bool uae_ppc_direct_physical_memory_handle(uint32_t addr, uint8_t *&ptr)
{
	if (valid_address(addr, 0x1000)) {
		ptr = get_real_address(addr);
		return true;
	}
	return false;
}

bool UAECALL uae_ppc_io_mem_write(uint32_t addr, uint32_t data, int size)
{
	while (ppc_thread_running && ppc_cpu_lock_state < 0 && ppc_state);

	if (ppc_thread_running && !valid_address(addr, size)) {
		write_comm_pipe_u32(&ppcquery, addr, 0);
		write_comm_pipe_u32(&ppcquery, size | 0x80, 0);
		write_comm_pipe_u32(&ppcquery, data, 1);
#if PPC_SYNC_WRITE
		read_comm_pipe_u32_blocking(&ppcreply);
#else
		write_comm_pipe_u32(&ppcquery, data, 0);
#endif
#if PPC_ACCESS_LOG > 0
		write_log(_T("PPC io write %08x = %08x %d\n"), addr, data, size);
#endif
		return true;
	}
#if PPC_ACCESS_LOG > 0
	if (!ppc_thread_running && !valid_address(addr, size)) {
		write_log(_T("PPC io write %08x = %08x %d\n"), addr, data, size);
	}
#endif
	switch (size)
	{
	case 4:
		put_long(addr, data);
		break;
	case 2:
		put_word(addr, data);
		break;
	case 1:
		put_byte(addr, data);
		break;
	default:
		write_log(_T("unknown ppc write %d %08x\n"), addr, size);
		return false;
	}
#if PPC_ACCESS_LOG > 2
	write_log(_T("PPC mem write %08x = %08x %d\n"), addr, data, size);
#endif
	return true;
}

bool UAECALL uae_ppc_io_mem_read(uint32_t addr, uint32_t *data, int size)
{
	uint32_t v;

	while (ppc_thread_running && ppc_cpu_lock_state < 0 && ppc_state);

	if (ppc_thread_running && !valid_address(addr, size)) {
		write_comm_pipe_u32(&ppcquery, addr, 0);
		write_comm_pipe_u32(&ppcquery, size, 1);
		v = read_comm_pipe_u32_blocking(&ppcreply);
#if PPC_ACCESS_LOG > 0
		if (addr != 0xbfe001)
			write_log(_T("PPC io read %08x=%08x %d\n"), addr, v, size);
#endif
		*data = v;
		return true;
	}
	switch (size)
	{
	case 4:
		v = get_long(addr);
		break;
	case 2:
		v = get_word(addr);
		break;
	case 1:
		v = get_byte(addr);
		break;
	default:
		write_log(_T("unknown ppc read %d %08x\n"), addr, size);
		return false;
	}
	*data = v;

#if PPC_ACCESS_LOG > 0
	if (!ppc_thread_running && !valid_address(addr, size)) {
		write_log(_T("PPC io read %08x=%08x %d\n"), addr, v, size);
	}
#endif
#if PPC_ACCESS_LOG > 2
	write_log(_T("PPC mem read %08x=%08x %d\n"), addr, v, size);
#endif
	return true;
}

bool UAECALL uae_ppc_io_mem_write64(uint32_t addr, uint64_t data)
{
	while (ppc_thread_running && ppc_cpu_lock_state < 0 && ppc_state);

	if (ppc_thread_running && !valid_address(addr, 8)) {
#if PPC_ACCESS_LOG > 0
		write_log(_T("PPC io write64 %08x = %08llx\n"), addr, (unsigned long long) data);
#endif
		write_comm_pipe_u32(&ppcquery, addr, 0);
		write_comm_pipe_u32(&ppcquery, 8 | 0x80, 0);
		write_comm_pipe_u32(&ppcquery, data >> 32, 0);
		write_comm_pipe_u32(&ppcquery, data & 0xffffffff, 1);
#if PPC_SYNC_WRITE
		read_comm_pipe_u32_blocking(&ppcreply);
#else
		write_comm_pipe_u32(&ppcquery, data, 0);
#endif
		return true;
	}
	put_long(addr + 0, data >> 32);
	put_long(addr + 4, data & 0xffffffff);
#if PPC_ACCESS_LOG > 2
	write_log(_T("PPC mem write64 %08x = %08llx\n"), addr, data);
#endif
	return true;
}

bool UAECALL uae_ppc_io_mem_read64(uint32_t addr, uint64_t *data)
{
	uint32_t v1, v2;

	while (ppc_thread_running && ppc_cpu_lock_state < 0 && ppc_state);

	if (ppc_thread_running && !valid_address(addr, 8)) {
		write_comm_pipe_u32(&ppcquery, addr, 0);
		write_comm_pipe_u32(&ppcquery, 8, 0);
		v1 = read_comm_pipe_u32_blocking(&ppcreply);
		v2 = read_comm_pipe_u32_blocking(&ppcreply);
		*data = ((uint64_t)v1 << 32) | v2;
#if PPC_ACCESS_LOG > 0
		write_log(_T("PPC io read64 %08x = %08llx\n"), addr, (unsigned long long) *data);
#endif
		return true;
	}
	v1 = get_long(addr + 0);
	v2 = get_long(addr + 4);
	*data = ((uint64_t)v1 << 32) | v2;
#if PPC_ACCESS_LOG > 2
	write_log(_T("PPC mem read64 %08x = %08llx\n"), addr, *data);
#endif
	return true;
}

void uae_ppc_cpu_stop(void)
{
	TRACE(_T("uae_ppc_cpu_stop\n"));
	if (ppc_thread_running && ppc_state) {
		write_log(_T("Stopping PPC.\n"));
		uae_ppc_wakeup();
		g_ppc_cpu_stop();
		while (ppc_state != PPC_STATE_STOP && ppc_state != PPC_STATE_CRASH) {
			uae_ppc_wakeup();
			uae_ppc_poll_queue();
		}
		read_comm_pipe_u32_blocking(&ppcreturn);
		ppc_state = PPC_STATE_STOP;
		write_log(_T("PPC stopped.\n"));
	}
}

void uae_ppc_cpu_reboot(void)
{
	TRACE(_T("uae_ppc_cpu_reboot\n"));
	if (ppc_main_thread) {
		uae_ppc_cpu_reset();
	} else {
		write_log(_T("Starting PPC thread.\n"));
		if (!ppc_thread_running) {
			ppc_thread_running = true;
			ppc_main_thread = false;
			init_comm_pipe(&ppcrequests, 10, 1);
			init_comm_pipe(&ppcreturn, 10, 1);
			init_comm_pipe(&ppcreply, 100, 1);
			init_comm_pipe(&ppcquery, 100, 1);
			uae_start_thread(_T("ppc"), ppc_thread, NULL, NULL);
		}
		write_comm_pipe_u32(&ppcrequests, 1, 1);
	}
}

void uae_ppc_reset(bool hardreset)
{
	TRACE(_T("uae_ppc_reset hardreset=%d\n"), hardreset);
	uae_ppc_cpu_stop();
	ppc_main_thread = false;
	if (hardreset) {
		if (ppc_init_done)
			g_ppc_cpu_free();
		ppc_init_done = false;
	}
}

void uae_ppc_cpu_lock(void)
{
	// when called, lock was already set by other CPU
	if (ppc_access) {
		// ppc accessing but m68k already locked
		ppc_cpu_lock_state = -1;
	} else {
		// m68k accessing but ppc already locked
		ppc_cpu_lock_state = 1;
	}
}

bool uae_ppc_cpu_unlock(void)
{
	if (!ppc_cpu_lock_state)
		return true;
	ppc_cpu_lock_state = 0;
	return false;
}

void uae_ppc_wakeup(void)
{
	TRACE(_T("uae_ppc_wakeup\n"));
	if (ppc_state == PPC_STATE_SLEEP)
		ppc_state = PPC_STATE_ACTIVE;
}

void uae_ppc_interrupt(bool active)
{
	TRACE(_T("uae_ppc_interrupt\n"));
	if (active) {
		g_ppc_cpu_atomic_raise_ext_exception();
		uae_ppc_wakeup();
	} else {
		g_ppc_cpu_atomic_cancel_ext_exception();
	}
}

// sleep until interrupt (or PPC stopped)
void uae_ppc_doze(void)
{
	TRACE(_T("uae_ppc_doze\n"));
	if (!ppc_thread_running)
		return;
	ppc_state = PPC_STATE_SLEEP;
	while (ppc_state == PPC_STATE_SLEEP) {
		sleep_millis(2);
	}
}

void uae_ppc_crash(void)
{
	TRACE(_T("uae_ppc_crash\n"));
	ppc_state = PPC_STATE_CRASH;
	g_ppc_cpu_stop();
}

void uae_ppc_hsync_handler(void)
{
	if (ppc_state != PPC_STATE_SLEEP)
		return;
	if (g_ppc_cpu_get_dec() == 0) {
		uae_ppc_wakeup();
	} else {
		g_ppc_cpu_do_dec(ppc_cycle_count);
	}
}
