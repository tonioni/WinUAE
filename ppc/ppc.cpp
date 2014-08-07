
#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "threaddep/thread.h"
#include "ppc.h"
#include "memory.h"
#include "cpuboard.h"
#include "debug.h"
#include "custom.h"
#include "uae.h"

#include "cpu/cpu.h"

#define PPC_SYNC_WRITE 0
#define PPC_ACCESS_LOG 0

volatile int ppc_state;

static bool ppc_thread_running;
static smp_comm_pipe ppcrequests, ppcreturn;
static smp_comm_pipe ppcquery, ppcreply;
int ppc_cycle_count;

#define CSPPC_PVR 0x00090204
#define BLIZZPPC_PVR 0x00070101

static void *ppc_thread(void *v)
{
	for (;;) {
		uae_u32 v = read_comm_pipe_u32_blocking(&ppcrequests);
		if (v == 0xfffffff)
			break;
		ppc_cpu_init(currprefs.cpuboard_type == BOARD_BLIZZARDPPC ? BLIZZPPC_PVR : CSPPC_PVR);
		ppc_cpu_set_pc(0, 0xfff00100);
		ppc_cycle_count = 240000000 / (hblank_hz * 4);
		ppc_state = PPC_STATE_ACTIVE;
		ppc_cpu_run();
		if (ppc_state == PPC_STATE_ACTIVE || ppc_state == PPC_STATE_SLEEP)
			ppc_state = PPC_STATE_STOP;
		write_log(_T("ppc_cpu_run() exited.\n"));
		write_comm_pipe_u32(&ppcreturn, 0, 0);
	}

	ppc_thread_running = false;
	return NULL;
}

void uae_ppc_poll_queue(void)
{
	if (comm_pipe_has_data(&ppcquery)) {
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
	}
}

void uae_ppc_sync (void)
{
	while (comm_pipe_has_data(&ppcquery));
}

bool uae_ppc_direct_physical_memory_handle(uint32 addr, byte *&ptr)
{
	if (valid_address(addr, 0x1000)) {
		ptr = get_real_address(addr);
		return true;
	}
	return false;
}

bool uae_ppc_io_mem_write(uint32 addr, uint32 data, int size)
{
	if (!valid_address(addr, size)) {
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

bool uae_ppc_io_mem_read(uint32 addr, uint32 &data, int size)
{
	uint32 v;

	if (!valid_address(addr, size)) {
		write_comm_pipe_u32(&ppcquery, addr, 0);
		write_comm_pipe_u32(&ppcquery, size, 1);
		v = read_comm_pipe_u32_blocking(&ppcreply);
#if PPC_ACCESS_LOG > 0
		if (addr != 0xbfe001)
			write_log(_T("PPC io read %08x=%08x %d\n"), addr, v, size);
#endif
		data = v;
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
	data = v;

#if PPC_ACCESS_LOG > 2
	write_log(_T("PPC mem read %08x=%08x %d\n"), addr, v, size);
#endif
	return true;
}

bool uae_ppc_io_mem_write64(uint32 addr, uint64 data)
{
	if (!valid_address(addr, 8)) {
#if PPC_ACCESS_LOG > 0
		write_log(_T("PPC io write64 %08x = %08llx\n"), addr, data);
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

bool uae_ppc_io_mem_read64(uint32 addr, uint64 &data)
{
	uae_u32 v1, v2;
	if (!valid_address(addr, 8)) {
		write_comm_pipe_u32(&ppcquery, addr, 0);
		write_comm_pipe_u32(&ppcquery, 8, 0);
		v1 = read_comm_pipe_u32_blocking(&ppcreply);
		v2 = read_comm_pipe_u32_blocking(&ppcreply);
		data = ((uint64)v1 << 32) | v2;
#if PPC_ACCESS_LOG > 0
		write_log(_T("PPC io read64 %08x = %08llx\n"), addr, data);
#endif
		return true;
	}
	v1 = get_long(addr + 0);
	v2 = get_long(addr + 4);
	data = ((uint64)v1 << 32) | v2;
#if PPC_ACCESS_LOG > 2
	write_log(_T("PPC mem read64 %08x = %08llx\n"), addr, data);
#endif
	return true;
}

void ppc_stop(void)
{
	if (ppc_thread_running && ppc_state) {
		if (ppc_state == PPC_STATE_SLEEP)
			ppc_state = PPC_STATE_ACTIVE;
		write_log(_T("Stopping PPC.\n"));
		ppc_cpu_stop();
		while (ppc_state != PPC_STATE_STOP && ppc_state != PPC_STATE_CRASH) {
			uae_ppc_poll_queue();
		}
		read_comm_pipe_u32_blocking(&ppcreturn);
		write_log(_T("PPC stopped.\n"));
	}
}

void ppc_reboot(void)
{
	write_log(_T("Starting PPC thread.\n"));
	if (!ppc_thread_running) {
		ppc_thread_running = true;
		init_comm_pipe(&ppcrequests, 10, 1);
		init_comm_pipe(&ppcreturn, 10, 1);
		init_comm_pipe(&ppcreply, 100, 1);
		init_comm_pipe(&ppcquery, 100, 1);
		uae_start_thread(_T("ppc"), ppc_thread, NULL, NULL);
	}
	write_comm_pipe_u32(&ppcrequests, 1, 1);
}

void ppc_wakeup(void)
{
	if (ppc_state == PPC_STATE_SLEEP)
		ppc_state = PPC_STATE_ACTIVE;
}

void ppc_cpu_atomic_raise_ext_exception(void);
void ppc_cpu_atomic_cancel_ext_exception(void);

void ppc_interrupt(bool active)
{
	if (active) {
		ppc_cpu_atomic_raise_ext_exception();
		ppc_wakeup();
	} else {
		ppc_cpu_atomic_cancel_ext_exception();
	}
}


// sleep until interrupt (or PPC stopped)
void ppc_doze(void)
{
	ppc_state = PPC_STATE_SLEEP;
	while (ppc_state == PPC_STATE_SLEEP) {
		sleep_millis(2);
	}
}

void ppc_crash(void)
{
	ppc_state = PPC_STATE_CRASH;
	ppc_cpu_stop();
}

typedef void * sys_mutex;

int	sys_lock_mutex(sys_mutex m)
{
	uae_sem_wait(&m);
	return 1;
}
void sys_unlock_mutex(sys_mutex m)
{
	uae_sem_post(&m);
}
int	sys_create_mutex(sys_mutex *m)
{
	if (!(*m))
		uae_sem_init(m, 0, 1);
	return 1;
}
