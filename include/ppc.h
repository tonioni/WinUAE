#ifndef UAE_PPC_H
#define UAE_PPC_H

/*
 * This file only consists of default includes, so it can safely be included
 * in other projects (PearPC, QEmu) without pulling in lots of headers.
 */

#include <stdint.h>
#include <stdbool.h>

/* PPC_EXTERN_C is defined (on the UAE side) when compiling with QEmu CPU. */
#ifndef PPC_EXTERN_C
/*
 * If it is not defined, we are either compiling "on the QEmu side (C)", or
 * the file is used with the PearPC implementation. In both cases,
 * PPC_EXTERN_C should expand to nothing.
 */
#define PPC_EXTERN_C
#endif

/* UAE PPC functions */

void uae_ppc_doze(void);
void uae_ppc_sync (void);
void uae_ppc_crash(void);

void uae_ppc_cpu_reboot(void);
void uae_ppc_cpu_stop(void);
bool uae_ppc_poll_queue(void);
void uae_ppc_interrupt(bool active);
void uae_ppc_cpu_lock(void);
bool uae_ppc_cpu_unlock(void);
void uae_ppc_to_main_thread(void);
void uae_ppc_emulate(void);
void uae_ppc_reset(bool hardreset);
void uae_ppc_hsync_handler(void);
void uae_ppc_wakeup(void);

#define PPC_STATE_STOP 0
#define PPC_STATE_ACTIVE 1
#define PPC_STATE_SLEEP 2
#define PPC_STATE_CRASH 3

extern volatile int ppc_state;

#ifdef __cplusplus
bool uae_ppc_direct_physical_memory_handle(uint32_t addr, uint8_t *&ptr);
#endif

PPC_EXTERN_C bool uae_ppc_io_mem_read(uint32_t addr, uint32_t *data, int size);
PPC_EXTERN_C bool uae_ppc_io_mem_write(uint32_t addr, uint32_t data, int size);

bool uae_ppc_io_mem_read64(uint32_t addr, uint64_t *data);
bool uae_ppc_io_mem_write64(uint32_t addr, uint64_t data);

extern int ppc_cycle_count;

/* PPC CPU implementation */

PPC_EXTERN_C bool ppc_cpu_init(uint32_t pvr);
PPC_EXTERN_C void ppc_cpu_free(void);

PPC_EXTERN_C void ppc_cpu_stop(void);

PPC_EXTERN_C void ppc_cpu_atomic_raise_ext_exception(void);
PPC_EXTERN_C void ppc_cpu_atomic_cancel_ext_exception(void);

PPC_EXTERN_C void ppc_cpu_map_memory(uint32_t addr, uint32_t size, void *memory, const char *name);

PPC_EXTERN_C void ppc_cpu_set_pc(int cpu, uint32_t value);
PPC_EXTERN_C void ppc_cpu_run_continuous(void);
PPC_EXTERN_C void ppc_cpu_run_single(int count);

PPC_EXTERN_C uint64_t ppc_cpu_get_dec(void);
PPC_EXTERN_C void ppc_cpu_do_dec(int value);

#if 0
uint32	ppc_cpu_get_gpr(int cpu, int i);
void	ppc_cpu_set_gpr(int cpu, int i, uint32 newvalue);
void	ppc_cpu_set_msr(int cpu, uint32 newvalue);
uint32	ppc_cpu_get_pc(int cpu);
uint32	ppc_cpu_get_pvr(int cpu);
#endif

#endif // UAE_PPC_H
