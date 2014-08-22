#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "threaddep/thread.h"
#include "memory.h"
#include "cpuboard.h"
#include "debug.h"
#include "custom.h"
#include "uae.h"

#if defined(__cplusplus) && defined(WITH_QEMU_CPU)
#define PPC_EXTERN_C extern "C"
#endif

#include "ppc.h"
//#include "ppc-if.h"

#ifdef WITH_PEARPC_CPU
#include "pearpc/cpu/cpu.h"
#include "pearpc/io/io.h"
#include "pearpc/cpu/cpu_generic/ppc_cpu.h"
#endif

#define PPC_SYNC_WRITE 0
#define PPC_ACCESS_LOG 2

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

static void map_banks(void)
{
#ifdef WITH_QEMU_CPU
	/*
	 * Use NULL to get callbacks to uae_ppc_io_mem_read/write. Use real
	 * memory address for direct access to RAM banks (looks like this
	 * is needed by JIT, or at least more work is needed on QEmu Side
	 * to allow all memory access to go via callbacks).
	 */

	// FIXME: hack, replace with automatic / dynamic mapping
#if 1
	ppc_cpu_map_memory(0x00000000, 2048 KB, NULL,						  "Chip memory");
	ppc_cpu_map_memory(0x00BF0000,	 64 KB, NULL,						  "CIA");
	ppc_cpu_map_memory(0x00F00000,	256 KB, get_real_address(0x00F00000), "CPUBoard F00000");
	ppc_cpu_map_memory(0x00F50000,	192 KB, NULL,						  "CPUBoard IO");
	ppc_cpu_map_memory(0x00DF0000,	 64 KB, NULL,						  "Custom chipset");
	ppc_cpu_map_memory(0x08000000,	128 MB, get_real_address(0x08000000), "RAMSEY memory (high)");
	ppc_cpu_map_memory(0xFFF00000,	512 KB, get_real_address(0xFFF00000), "CPUBoard MAPROM");
#else
	ppc_cpu_map_memory(0x00BF0000,	 64 KB, NULL,						  "CIA");
	ppc_cpu_map_memory(0x00F00000,	256 KB, NULL,						  "CPUBoard F00000");
	ppc_cpu_map_memory(0x00F50000,	192 KB, NULL,						  "CPUBoard IO");
	ppc_cpu_map_memory(0x08000000,	128 MB, NULL,						  "RAMSEY memory (high)");
	ppc_cpu_map_memory(0xFFF00000,	512 KB, get_real_address(0xFFF00000), "CPUBoard MAPROM");
#endif
#endif
}

static void uae_ppc_cpu_reset(void)
{
#ifdef WITH_PPC
	write_log("---- uae_ppc_cpu_reset ----\n");
	if (!ppc_init_done) {
		write_log(_T("PPC: Hard reset\n"));
		ppc_cpu_init(currprefs.cpuboard_type == BOARD_BLIZZARDPPC ? BLIZZPPC_PVR : CSPPC_PVR);
		map_banks();
		ppc_init_done = true;
	}
	write_log(_T("PPC: Init\n"));
	ppc_cpu_set_pc(0, 0xfff00100);
	ppc_cycle_count = 2000;
	ppc_state = PPC_STATE_ACTIVE;
	ppc_cpu_lock_state = 0;
#endif
}

static void *ppc_thread(void *v)
{
#ifdef WITH_PPC
	for (;;) {
		uae_u32 v = read_comm_pipe_u32_blocking(&ppcrequests);
		if (v == 0xffffffff)
			break;
		uae_ppc_cpu_reset();
		ppc_cpu_run_continuous();
		if (ppc_state == PPC_STATE_ACTIVE || ppc_state == PPC_STATE_SLEEP)
			ppc_state = PPC_STATE_STOP;
		write_log(_T("ppc_cpu_run() exited.\n"));
		write_comm_pipe_u32(&ppcreturn, 0, 0);
	}

	ppc_thread_running = false;
#endif
	return NULL;
}

void uae_ppc_to_main_thread(void)
{
	write_log("---- uae_ppc_to_main_thread ----\n");
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
#ifdef WITH_PPC
	write_log("---- uae_ppc_emulate ----\n");
	if (ppc_state == PPC_STATE_ACTIVE || ppc_state == PPC_STATE_SLEEP)
		ppc_cpu_run_single(10);
#endif
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

PPC_EXTERN_C bool uae_ppc_io_mem_write(uint32_t addr, uint32_t data, int size)
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

PPC_EXTERN_C bool uae_ppc_io_mem_read(uint32_t addr, uint32_t *data, int size)
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
#if PPC_ACCESS_LOG > 0
	if (!ppc_thread_running && !valid_address(addr, size)) {
		write_log(_T("PPC io read %08x=%08x %d\n"), addr, v, size);
	}
#endif
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

#if PPC_ACCESS_LOG > 2
	write_log(_T("PPC mem read %08x=%08x %d\n"), addr, v, size);
#endif
	return true;
}

bool uae_ppc_io_mem_write64(uint32_t addr, uint64_t data)
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

bool uae_ppc_io_mem_read64(uint32_t addr, uint64_t *data)
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
	write_log("---- uae_ppc_cpu_stop ----\n");
#ifdef WITH_PPC
	if (ppc_thread_running && ppc_state) {
		write_log(_T("Stopping PPC.\n"));
		uae_ppc_wakeup();
		ppc_cpu_stop();
		while (ppc_state != PPC_STATE_STOP && ppc_state != PPC_STATE_CRASH) {
			uae_ppc_wakeup();
			uae_ppc_poll_queue();
		}
		read_comm_pipe_u32_blocking(&ppcreturn);
		ppc_state = PPC_STATE_STOP;
		write_log(_T("PPC stopped.\n"));
	}
#endif
}

void uae_ppc_cpu_reboot(void)
{
	write_log("---- uae_ppc_cpu_reboot ----\n");
#ifdef WITH_PPC
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
#endif
}

void uae_ppc_reset(bool hardreset)
{
	write_log("---- uae_ppc_reset ----\n");
#ifdef WITH_PPC
	uae_ppc_cpu_stop();
	ppc_main_thread = false;
	if (hardreset) {
		if (ppc_init_done)
			ppc_cpu_free();
		ppc_init_done = false;
	}
#endif
}

void uae_ppc_cpu_lock(void)
{
#ifdef WITH_PPC
	// when called, lock was already set by other CPU
	if (ppc_access) {
		// ppc accessing but m68k already locked
		ppc_cpu_lock_state = -1;
	} else {
		// m68k accessing but ppc already locked
		ppc_cpu_lock_state = 1;
	}
#endif
}
bool uae_ppc_cpu_unlock(void)
{
#ifdef WITH_PPC
	if (!ppc_cpu_lock_state)
		return true;
	ppc_cpu_lock_state = 0;
	return false;
#endif
}

void uae_ppc_wakeup(void)
{
	write_log("---- uae_ppc_wakeup ----\n");
#ifdef WITH_PPC
	if (ppc_state == PPC_STATE_SLEEP)
		ppc_state = PPC_STATE_ACTIVE;
#endif
}

void uae_ppc_interrupt(bool active)
{
	write_log("---- uae_ppc_interrupt ----\n");
#ifdef WITH_PPC
	if (active) {
		ppc_cpu_atomic_raise_ext_exception();
		uae_ppc_wakeup();
	} else {
		ppc_cpu_atomic_cancel_ext_exception();
	}
#endif
}

// sleep until interrupt (or PPC stopped)
void uae_ppc_doze(void)
{
	write_log("---- uae_ppc_doze ----\n");
#ifdef WITH_PPC
	if (!ppc_thread_running)
		return;
	ppc_state = PPC_STATE_SLEEP;
	while (ppc_state == PPC_STATE_SLEEP) {
		sleep_millis(2);
	}
#endif
}

void uae_ppc_crash(void)
{
	write_log("---- uae_ppc_crash ----\n");
#ifdef WITH_PPC
	ppc_state = PPC_STATE_CRASH;
	ppc_cpu_stop();
#endif
}

void uae_ppc_hsync_handler(void)
{
#ifdef WITH_PPC
	if (ppc_state != PPC_STATE_SLEEP)
		return;
	if (ppc_cpu_get_dec() == 0) {
		uae_ppc_wakeup();
	} else {
		ppc_cpu_do_dec(ppc_cycle_count);
	}
#endif
}

#ifdef WITH_PEARPC_CPU

typedef void * sys_mutex;

int sys_lock_mutex(sys_mutex m)
{
	uae_sem_wait(&m);
	return 1;
}

void sys_unlock_mutex(sys_mutex m)
{
	uae_sem_post(&m);
}

int sys_create_mutex(sys_mutex *m)
{
	if (!(*m))
		uae_sem_init(m, 0, 1);
	return 1;
}

void sys_destroy_mutex(sys_mutex m)
{
	uae_sem_destroy(&m);
}

#endif

#ifndef _MSV_VER
#define __cdecl
#endif

#ifdef __cplusplus
extern "C" {
#endif

void __cdecl pixman_format_supported_source(void);
void __cdecl pixman_format_supported_source(void) { }

void __cdecl pixman_image_composite(void);
void __cdecl pixman_image_composite(void) { }

void __cdecl pixman_image_create_bits(void);
void __cdecl pixman_image_create_bits(void) { }

void __cdecl pixman_image_create_solid_fill(void);
void __cdecl pixman_image_create_solid_fill(void) { }

void __cdecl pixman_image_fill_rectangles(void);
void __cdecl pixman_image_fill_rectangles(void) { }

void __cdecl pixman_image_get_data(void);
void __cdecl pixman_image_get_data(void) { }

void __cdecl pixman_image_get_height(void);
void __cdecl pixman_image_get_height(void) { }

void __cdecl pixman_image_get_width(void);
void __cdecl pixman_image_get_width(void) { }

void __cdecl pixman_image_get_stride(void);
void __cdecl pixman_image_get_stride(void) { }

void __cdecl pixman_image_unref(void);
void __cdecl pixman_image_unref(void) { }

#ifdef __cplusplus
}
#endif
