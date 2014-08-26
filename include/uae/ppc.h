/*
 * Platform-independent loadable module functions for UAE
 * Copyright (C) 2014 Toni Wilen, Frode Solheim
 *
 * Licensed under the terms of the GNU General Public License version 2.
 * See the file 'COPYING' for full license text.
 */

#ifndef UAE_PPC_H
#define UAE_PPC_H

/* This file is intended to be included by external libraries as well,
 * so don't pull in too much UAE-specific stuff. */

#include "uae/api.h"
#include "uae/types.h"

#ifdef UAE
#define PPCAPI UAE_EXTERN_C UAE_IMPORT
#else
#define PPCAPI UAE_EXTERN_C UAE_EXPORT
#endif
#define PPCCALL UAECALL

/* UAE PPC functions and variables only visible to UAE */

#ifdef UAE

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

#ifdef __cplusplus
bool uae_ppc_direct_physical_memory_handle(uint32_t addr, uint8_t *&ptr);
#endif

extern volatile int ppc_state;
extern int ppc_cycle_count;

#endif /* UAE */

/* Exported UAE functions which external PPC implementations can use */

typedef bool (UAECALL *uae_ppc_io_mem_read_function)(uint32_t addr, uint32_t *data, int size);
typedef bool (UAECALL *uae_ppc_io_mem_write_function)(uint32_t addr, uint32_t data, int size);
typedef bool (UAECALL *uae_ppc_io_mem_read64_function)(uint32_t addr, uint64_t *data);
typedef bool (UAECALL *uae_ppc_io_mem_write64_function)(uint32_t addr, uint64_t data);

#ifdef UAE

bool UAECALL uae_ppc_io_mem_read(uint32_t addr, uint32_t *data, int size);
bool UAECALL uae_ppc_io_mem_write(uint32_t addr, uint32_t data, int size);
bool UAECALL uae_ppc_io_mem_read64(uint32_t addr, uint64_t *data);
bool UAECALL uae_ppc_io_mem_write64(uint32_t addr, uint64_t data);

#else

extern uae_ppc_io_mem_read_function uae_ppc_io_mem_read;
extern uae_ppc_io_mem_write_function uae_ppc_io_mem_write;
extern uae_ppc_io_mem_read64_function uae_ppc_io_mem_read64;
extern uae_ppc_io_mem_write64_function uae_ppc_io_mem_write64;

#endif

/* Prototypes for PPC CPU implementation, used by PearPC and QEmu */

bool PPCCALL ppc_cpu_init(uint32_t pvr);
void PPCCALL ppc_cpu_free(void);
void PPCCALL ppc_cpu_stop(void);
void PPCCALL ppc_cpu_atomic_raise_ext_exception(void);
void PPCCALL ppc_cpu_atomic_cancel_ext_exception(void);
void PPCCALL ppc_cpu_map_memory(uint32_t addr, uint32_t size, void *memory, const char *name);
void PPCCALL ppc_cpu_set_pc(int cpu, uint32_t value);
void PPCCALL ppc_cpu_run_continuous(void);
void PPCCALL ppc_cpu_run_single(int count);
uint64_t PPCCALL ppc_cpu_get_dec(void);
void PPCCALL ppc_cpu_do_dec(int value);

/* Other PPC defines */

#define PPC_IMPLEMENTATION_AUTO 0
#define PPC_IMPLEMENTATION_DUMMY 1
#define PPC_IMPLEMENTATION_PEARPC 2
#define PPC_IMPLEMENTATION_QEMU 3

#define PPC_STATE_STOP 0
#define PPC_STATE_ACTIVE 1
#define PPC_STATE_SLEEP 2
#define PPC_STATE_CRASH 3

#endif /* UAE_PPC_H */
