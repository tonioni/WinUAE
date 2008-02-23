// Implement mprotect() for Win32
// Copyright (C) 2000, Brian King
// GNU Public License

#include <float.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "sys/mman.h"
#include "memory.h"
#include "options.h"
#include "autoconf.h"
#include "win32.h"

#if defined(NATMEM_OFFSET)

#define BARRIER 32

static struct shmid_ds shmids[MAX_SHMID];

extern int p96mode;

static int memorylocking = 0;

uae_u8 *natmem_offset = NULL;

static uae_u8 *p96mem_offset;
static uae_u8 *p96fakeram;
static int p96fakeramsize;

static void *virtualallocwithlock(LPVOID addr, SIZE_T size, DWORD allocationtype, DWORD protect)
{
    void *p = VirtualAlloc (addr, size, allocationtype, protect);
    if (p && memorylocking)
	VirtualLock(p, size);
    return p;
}
static void virtualfreewithlock(LPVOID addr, SIZE_T size, DWORD freetype)
{
    if (memorylocking)
	VirtualUnlock(addr, size);
    VirtualFree(addr, size, freetype);
}

void cache_free(void *cache)
{
    virtualfreewithlock(cache, 0, MEM_RELEASE);
}

void *cache_alloc(int size)
{
    return virtualallocwithlock(NULL, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
}

#if 0
static void setworkingset(void)
{
    typedef BOOL (CALLBACK* SETPROCESSWORKINGSETSIZE)(HANDLE,SIZE_T,SIZE_T);
    SETPROCESSWORKINGSETSIZE pSetProcessWorkingSetSize;
    pSetProcessWorkingSetSize = (SETPROCESSWORKINGSETSIZE)GetProcAddress(GetModuleHandle("kernal32.dll", "GetProcessWorkingSetSize");
    if (!pSetProcessWorkingSetSize)
	return;
    pSetProcessWorkingSetSize(GetCurrentProcess (),
);
#endif

static uae_u32 lowmem (void)
{
    uae_u32 change = 0;
    if (currprefs.z3fastmem_size >= 8 * 1024 * 1024 && currprefs.gfxmem_size < 256 * 1024 * 1024) {
        change = currprefs.z3fastmem_size - currprefs.z3fastmem_size / 2;
        currprefs.z3fastmem_size >>= 1;
	changed_prefs.z3fastmem_size = currprefs.z3fastmem_size;
    } else if (currprefs.gfxmem_size >= 8 * 1024 * 1024) {
        change = currprefs.gfxmem_size - currprefs.gfxmem_size / 2;
        currprefs.gfxmem_size >>= 1;
	changed_prefs.gfxmem_size = currprefs.gfxmem_size;
    }
    return change;
}

static uae_u32 max_allowed_mman;
static uae_u64 size64;
	typedef BOOL (CALLBACK* GLOBALMEMORYSTATUSEX)(LPMEMORYSTATUSEX);

void preinit_shm (void)
{
    uae_u64 total64;
    uae_u64 totalphys64;
    MEMORYSTATUS memstats;
    GLOBALMEMORYSTATUSEX pGlobalMemoryStatusEx;
    MEMORYSTATUSEX memstatsex;

    max_allowed_mman = 1536;
    if (os_64bit)
        max_allowed_mman = 2048;

    memstats.dwLength = sizeof(memstats);
    GlobalMemoryStatus(&memstats);
    totalphys64 = memstats.dwTotalPhys;
    total64 = (uae_u64)memstats.dwAvailPageFile + (uae_u64)memstats.dwAvailPhys;
    pGlobalMemoryStatusEx = (GLOBALMEMORYSTATUSEX)GetProcAddress(GetModuleHandle("kernel32.dll"), "GlobalMemoryStatusEx");
    if (pGlobalMemoryStatusEx) {
        memstatsex.dwLength = sizeof (MEMORYSTATUSEX);
        if (pGlobalMemoryStatusEx(&memstatsex)) {
	    totalphys64 = memstatsex.ullTotalPhys;
	    total64 = memstatsex.ullAvailPageFile + memstatsex.ullAvailPhys;
	}
    }
    size64 = total64 - (total64 >> 3);
    if (os_64bit) {
	if (size64 > 0x7f000000)
	    size64 = 0x7f000000;
    } else {
	if (size64 > 0x7f000000)
	    size64 = 0x7f000000;
    }
    if (size64 < 8 * 1024 * 1024)
	size64 = 8 * 1024 * 1024;
    if (max_allowed_mman * 1024 * 1024 > size64)
	max_allowed_mman = size64 / (1024 * 1024);
    max_z3fastmem = (max_allowed_mman - (max_allowed_mman >> 3)) * 1024 * 1024;

    write_log ("Max Z3FastRAM %dM. Total physical RAM %uM\n", max_z3fastmem >> 20, totalphys64 >> 20);
    canbang = 1;
}

int init_shm (void)
{
    int i;
    LPVOID blah = NULL;
    uae_u32 size, totalsize, z3size, natmemsize;

    if (natmem_offset)
	VirtualFree(natmem_offset, 0, MEM_RELEASE);
    natmem_offset = NULL;
    canbang = 0;

    size = 0x1000000;
    if (currprefs.cpu_model >= 68020)
	size = 0x10000000;
    if (currprefs.z3fastmem_size) {
	z3size = currprefs.z3fastmem_size + (currprefs.z3fastmem_start -  0x10000000);
	if (currprefs.gfxmem_size)
	    size += 16 * 1024 * 1024;
    }

    totalsize = size + z3size + currprefs.gfxmem_size;
    while (totalsize > size64) {
	int change = lowmem ();
	if (!change)
	    return 0;
	totalsize -= change;
    }

    shm_start = 0;
    for (i = 0; i < MAX_SHMID; i++) {
	shmids[i].attached = 0;
	shmids[i].key = -1;
	shmids[i].size = 0;
	shmids[i].addr = NULL;
	shmids[i].name[0] = 0;
    }
    natmemsize = p96mode ? size + z3size : totalsize;

    for (;;) {
	int change;
	blah = VirtualAlloc(NULL, natmemsize, MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (blah)
	    break;
	write_log ("NATMEM: %dM area failed to allocate, err=%d\n", natmemsize >> 20, GetLastError());
	change = lowmem ();
	totalsize -= change;
	if (change == 0 || totalsize < 0x10000000) {
	    write_log ("NATMEM: No special area could be allocated (2)!\n");
	    return 0;
	}
    }
    natmem_offset = blah;
    if (p96mode) {
        p96mem_offset = VirtualAlloc(natmem_offset + size + z3size, currprefs.gfxmem_size + 4096, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_EXECUTE_READWRITE);
        if (!p96mem_offset) {
	    write_log ("NATMEM: failed to allocate special Picasso96 GFX RAM\n");
	    p96mode = 0;
	}
    }

    if (!natmem_offset) {
	write_log ("NATMEM: No special area could be allocated! (1)\n");
    } else {
	write_log ("NATMEM: Our special area: 0x%p-0x%p (%08x %dM)\n",
	    natmem_offset, (uae_u8*)natmem_offset + natmemsize,
	    natmemsize, natmemsize >> 20);
	if (p96mode)
	    write_log ("NATMEM: P96 special area: 0x%p-0x%p (%08x %dM)\n",
		p96mem_offset, (uae_u8*)p96mem_offset + currprefs.gfxmem_size,
		currprefs.gfxmem_size, currprefs.gfxmem_size >> 20);
	canbang = 1;
    }

    return canbang;
}


void mapped_free(uae_u8 *mem)
{
    shmpiece *x = shm_start;

    if (mem == filesysory || (!p96mode && mem == p96fakeram)) {
	xfree (p96fakeram);
	p96fakeram = NULL;
	while(x) {
	    if (mem == x->native_address) {
		int shmid = x->id;
		shmids[shmid].key = -1;
		shmids[shmid].name[0] = '\0';
		shmids[shmid].size = 0;
		shmids[shmid].attached = 0;
	    }
	    x = x->next;
	}
	return;
    }

    while(x) {
	if(mem == x->native_address)
	    shmdt(x->native_address);
	x = x->next;
    }
    x = shm_start;
    while(x) {
	struct shmid_ds blah;
	if (mem == x->native_address) {
	    if (shmctl(x->id, IPC_STAT, &blah) == 0)
		shmctl(x->id, IPC_RMID, &blah);
	}
	x = x->next;
    }
}

static key_t get_next_shmkey(void)
{
    key_t result = -1;
    int i;
    for (i = 0; i < MAX_SHMID; i++) {
	if (shmids[i].key == -1) {
	    shmids[i].key = i;
	    result = i;
	    break;
	}
    }
    return result;
}

STATIC_INLINE key_t find_shmkey(key_t key)
{
    int result = -1;
    if(shmids[key].key == key) {
	result = key;
    }
    return result;
}

int mprotect(void *addr, size_t len, int prot)
{
    int result = 0;
    return result;
}

void *shmat(int shmid, void *shmaddr, int shmflg)
{
    void *result = (void *)-1;
    BOOL got = FALSE;
    int p96special = FALSE;

#ifdef NATMEM_OFFSET
    unsigned int size=shmids[shmid].size;
    if(shmids[shmid].attached)
	return shmids[shmid].attached;
    if ((uae_u8*)shmaddr<natmem_offset) {
	if(!strcmp(shmids[shmid].name,"chip")) {
	    shmaddr=natmem_offset;
	    got = TRUE;
	    if (currprefs.fastmem_size == 0 || currprefs.chipmem_size < 2 * 1024 * 1024)
		size += BARRIER;
	}
	if(!strcmp(shmids[shmid].name,"kick")) {
	    shmaddr=natmem_offset + 0xf80000;
	    got = TRUE;
	    size += BARRIER;
	}
	if(!strcmp(shmids[shmid].name,"rom_a8")) {
	    shmaddr=natmem_offset + 0xa80000;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"rom_e0")) {
	    shmaddr=natmem_offset + 0xe00000;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"rom_f0")) {
	    shmaddr=natmem_offset + 0xf00000;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"rtarea")) {
	    shmaddr=natmem_offset + rtarea_base;
	    got = TRUE;
	    size += BARRIER;
	}
	if(!strcmp(shmids[shmid].name,"fast")) {
	    shmaddr=natmem_offset + 0x200000;
	    got = TRUE;
	    size += BARRIER;
	}
	if(!strcmp(shmids[shmid].name,"ramsey_low")) {
	    shmaddr=natmem_offset + a3000lmem_start;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"ramsey_high")) {
	    shmaddr=natmem_offset + a3000hmem_start;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"z3")) {
	    shmaddr=natmem_offset + currprefs.z3fastmem_start;
	    size += BARRIER;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"gfx")) {
	    got = TRUE;
	    if (p96mode) {
		p96special = TRUE;
		p96ram_start = p96mem_offset - natmem_offset;
		shmaddr = natmem_offset + p96ram_start;
		size += BARRIER;
	    } else {
		extern void p96memstart(void);
		p96memstart();
		shmaddr = natmem_offset + p96ram_start;
		virtualfreewithlock(shmaddr, size, MEM_DECOMMIT);
		xfree(p96fakeram);
		result = p96fakeram = xcalloc (size + 4096, 1);
		shmids[shmid].attached = result;
		return result;
	    }
	}
	if(!strcmp(shmids[shmid].name,"bogo")) {
	    shmaddr=natmem_offset+0x00C00000;
	    got = TRUE;
	    if (currprefs.bogomem_size <= 0x100000)
		size += BARRIER;
	}
	if(!strcmp(shmids[shmid].name,"filesys")) {
	    result = xcalloc (size, 1);
	    shmids[shmid].attached=result;
	    return result;
	}
	if(!strcmp(shmids[shmid].name,"hrtmon")) {
	    shmaddr=natmem_offset + 0x00a10000;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"arhrtmon")) {
	    shmaddr=natmem_offset + 0x00800000;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"superiv")) {
	    shmaddr=natmem_offset + 0x00d00000;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"superiv_2")) {
	    shmaddr=natmem_offset + 0x00b00000;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"superiv_3")) {
	    shmaddr=natmem_offset + 0x00e00000;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"custmem1")) {
	    shmaddr=natmem_offset + currprefs.custom_memory_addrs[0];
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"custmem2")) {
	    shmaddr=natmem_offset + currprefs.custom_memory_addrs[1];
	    got = TRUE;
	}
}
#endif

    if ((shmids[shmid].key == shmid) && shmids[shmid].size) {
	got = FALSE;
	if (got == FALSE) {
	    if (shmaddr)
		virtualfreewithlock(shmaddr, size, MEM_DECOMMIT);
	    result = virtualallocwithlock(shmaddr, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	    if (result == NULL) {
		result = (void*)-1;
		write_log ("VirtualAlloc %08.8X - %08.8X %x (%dk) failed %d\n",
		    (uae_u8*)shmaddr - natmem_offset, (uae_u8*)shmaddr - natmem_offset + size,
		    size, size >> 10, GetLastError());
	    } else {
		if (memorylocking)
		    VirtualLock(shmaddr, size);
		shmids[shmid].attached = result;
		write_log ("VirtualAlloc %08.8X - %08.8X %x (%dk) ok%s\n",
		    (uae_u8*)shmaddr - natmem_offset, (uae_u8*)shmaddr - natmem_offset + size,
		    size, size >> 10, p96special ? " P96" : "");
	    }
	} else {
	    shmids[shmid].attached = shmaddr;
	    result = shmaddr;
	}
    }
    return result;
}

int shmdt(const void *shmaddr)
{
    return 0;
}

int shmget(key_t key, size_t size, int shmflg, char *name)
{
    int result = -1;

    if((key == IPC_PRIVATE) || ((shmflg & IPC_CREAT) && (find_shmkey(key) == -1))) {
	write_log ("shmget of size %d (%dk) for %s\n", size, size >> 10, name);
	if ((result = get_next_shmkey()) != -1) {
	    shmids[result].size = size;
	    strcpy(shmids[result].name, name);
	} else {
	    result = -1;
	}
    }
    return result;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
    int result = -1;

    if ((find_shmkey(shmid) != -1) && buf) {
	switch(cmd)
	{
	    case IPC_STAT:
		*buf = shmids[shmid];
		result = 0;
	    break;
	    case IPC_RMID:
		VirtualFree(shmids[shmid].attached, shmids[shmid].size, MEM_DECOMMIT);
		shmids[shmid].key = -1;
		shmids[shmid].name[0] = '\0';
		shmids[shmid].size = 0;
		shmids[shmid].attached = 0;
		result = 0;
	    break;
	}
    }
    return result;
}

#endif

int isinf(double x)
{
    const int nClass = _fpclass(x);
    int result;
    if (nClass == _FPCLASS_NINF || nClass == _FPCLASS_PINF)
	result = 1;
    else
	result = 0;
    return result;
}
