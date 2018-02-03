// Implement mprotect() for Win32
// Copyright (C) 2000, Brian King
// GNU Public License

#include <float.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"
#include "uae/mman.h"
#include "uae/vm.h"
#include "autoconf.h"
#include "gfxboard.h"
#include "cpuboard.h"
#include "rommgr.h"
#include "newcpu.h"
#include "gui.h"
#ifdef WINUAE
#include "win32.h"
#endif

#if defined(NATMEM_OFFSET)

#define WIN32_NATMEM_TEST 0

uae_u32 max_z3fastmem;

/* BARRIER is used in case Amiga memory is access across memory banks,
 * for example move.l $1fffffff,d0 when $10000000-$1fffffff is mapped and
 * $20000000+ is not mapped.
 * Note: BARRIER will probably effectively be rounded up the host memory
 * page size.
 */
#define BARRIER 32

#define MAXZ3MEM32 0x7F000000
#define MAXZ3MEM64 0xF0000000

static struct uae_shmid_ds shmids[MAX_SHMID];
uae_u8 *natmem_reserved, *natmem_offset;
uae_u32 natmem_reserved_size;
static uae_u8 *p96mem_offset;
static int p96mem_size;
static uae_u32 p96base_offset;
static SYSTEM_INFO si;
static uaecptr start_rtg = 0;
static uaecptr end_rtg = 0;
int maxmem;
bool jit_direct_compatible_memory;

static uae_u8 *virtualallocwithlock (LPVOID addr, SIZE_T size, DWORD allocationtype, DWORD protect)
{
	uae_u8 *p = (uae_u8*)VirtualAlloc (addr, size, allocationtype, protect);
	return p;
}
static void virtualfreewithlock (LPVOID addr, SIZE_T size, DWORD freetype)
{
	VirtualFree(addr, size, freetype);
}

static uae_u32 lowmem (void)
{
	uae_u32 change = 0;
#if 0
	struct rtgboardconfig *rbc = &changed_prefs.rtgboards[0];
	struct rtgboardconfig *crbc = &currprefs.rtgboards[0];

	if (currprefs.z3fastmem_size + currprefs.z3fastmem2_size + currprefs.z3chipmem_size >= 8 * 1024 * 1024) {
		if (currprefs.z3fastmem2_size) {
			change = currprefs.z3fastmem2_size;
			currprefs.z3fastmem2_size = 0;
		} else if (currprefs.z3chipmem_size) {
			if (currprefs.z3chipmem_size <= 16 * 1024 * 1024) {
				change = currprefs.z3chipmem_size;
				currprefs.z3chipmem_size = 0;
			} else {
				change = currprefs.z3chipmem_size / 2;
				currprefs.z3chipmem_size /= 2;
			}
		} else {
			change = currprefs.z3fastmem_size - currprefs.z3fastmem_size / 4;
			currprefs.z3fastmem2_size = changed_prefs.z3fastmem2_size = currprefs.z3fastmem_size / 4;
			currprefs.z3fastmem_size /= 2;
			changed_prefs.z3fastmem_size = currprefs.z3fastmem_size;
		}
	} else if (crbc->rtgmem_type == GFXBOARD_UAE_Z3 && crbc->rtgmem_size >= 1 * 1024 * 1024) {
		change = crbc->rtgmem_size - crbc->rtgmem_size / 2;
		crbc->rtgmem_size /= 2;
		rbc->rtgmem_size = crbc->rtgmem_size;
	}
	if (currprefs.z3fastmem2_size < 128 * 1024 * 1024)
		currprefs.z3fastmem2_size = changed_prefs.z3fastmem2_size = 0;
#endif
	return change;
}

int mman_GetWriteWatch (PVOID lpBaseAddress, SIZE_T dwRegionSize, PVOID *lpAddresses, PULONG_PTR lpdwCount, PULONG lpdwGranularity)
{
	return GetWriteWatch (WRITE_WATCH_FLAG_RESET, lpBaseAddress, dwRegionSize, lpAddresses, lpdwCount, lpdwGranularity);
}
void mman_ResetWatch (PVOID lpBaseAddress, SIZE_T dwRegionSize)
{
	if (ResetWriteWatch (lpBaseAddress, dwRegionSize))
		write_log (_T("ResetWriteWatch() failed, %d\n"), GetLastError ());
}

static uae_u64 size64;
#ifdef _WIN32
typedef BOOL (CALLBACK* GLOBALMEMORYSTATUSEX)(LPMEMORYSTATUSEX);
#endif

static void clear_shm (void)
{
	shm_start = NULL;
	for (int i = 0; i < MAX_SHMID; i++) {
		memset (&shmids[i], 0, sizeof(struct uae_shmid_ds));
		shmids[i].key = -1;
	}
}

bool preinit_shm (void)
{
	uae_u64 total64;
	uae_u64 totalphys64;
#ifdef _WIN32
	MEMORYSTATUS memstats;
	GLOBALMEMORYSTATUSEX pGlobalMemoryStatusEx;
	MEMORYSTATUSEX memstatsex;
#endif
	uae_u32 max_allowed_mman;

	if (natmem_reserved)
#ifdef _WIN32
		VirtualFree (natmem_reserved, 0, MEM_RELEASE);
#else
#endif
	natmem_reserved = NULL;
	natmem_offset = NULL;
#if 0
	if (p96mem_offset) {
#ifdef _WIN32
		VirtualFree (p96mem_offset, 0, MEM_RELEASE);
#else
#endif
	}
	p96mem_offset = NULL;
#endif
	GetSystemInfo (&si);
	max_allowed_mman = 512 + 256;
#if 1
	if (os_64bit) {
//#ifdef WIN64
//		max_allowed_mman = 3072;
//#else
		max_allowed_mman = 2048;
//#endif
	}
#endif
	if (maxmem > max_allowed_mman)
		max_allowed_mman = maxmem;

#ifdef _WIN32
	memstats.dwLength = sizeof(memstats);
	GlobalMemoryStatus(&memstats);
	totalphys64 = memstats.dwTotalPhys;
	total64 = (uae_u64)memstats.dwAvailPageFile + (uae_u64)memstats.dwTotalPhys;
	pGlobalMemoryStatusEx = (GLOBALMEMORYSTATUSEX)GetProcAddress (GetModuleHandle (_T("kernel32.dll")), "GlobalMemoryStatusEx");
	if (pGlobalMemoryStatusEx) {
		memstatsex.dwLength = sizeof (MEMORYSTATUSEX);
		if (pGlobalMemoryStatusEx(&memstatsex)) {
			totalphys64 = memstatsex.ullTotalPhys;
			total64 = memstatsex.ullAvailPageFile + memstatsex.ullTotalPhys;
		}
	}
#else
#endif
	size64 = total64;
	if (os_64bit) {
		if (size64 > MAXZ3MEM64)
			size64 = MAXZ3MEM64;
	} else {
		if (size64 > MAXZ3MEM32)
			size64 = MAXZ3MEM32;
	}
	if (maxmem < 0) {
		size64 = MAXZ3MEM64;
		if (!os_64bit) {
			if (totalphys64 < 1536 * 1024 * 1024)
				max_allowed_mman = 256;
			if (max_allowed_mman < 256)
				max_allowed_mman = 256;
		}
	} else if (maxmem > 0) {
		size64 = maxmem * 1024 * 1024;
	}
	if (size64 < 8 * 1024 * 1024)
		size64 = 8 * 1024 * 1024;
	if (max_allowed_mman * 1024 * 1024 > size64)
		max_allowed_mman = size64 / (1024 * 1024);

	uae_u32 natmem_size = (max_allowed_mman + 1) * 1024 * 1024;
	if (natmem_size < 17 * 1024 * 1024)
		natmem_size = 17 * 1024 * 1024;

#if WIN32_NATMEM_TEST
	natmem_size = WIN32_NATMEM_TEST * 1024 * 1024;
#endif

	if (natmem_size > 0x80000000) {
		natmem_size = 0x80000000;
	}

	write_log (_T("MMAN: Total physical RAM %llu MB, all RAM %llu MB\n"),
				  totalphys64 >> 20, total64 >> 20);
	write_log(_T("MMAN: Attempting to reserve: %u MB\n"), natmem_size >> 20);

#if 1
	natmem_reserved = (uae_u8 *) uae_vm_reserve(natmem_size, UAE_VM_32BIT | UAE_VM_WRITE_WATCH);
#else
	natmem_size = 0x20000000;
	natmem_reserved = (uae_u8 *) uae_vm_reserve_fixed(
		(void *) 0x90000000, natmem_size, UAE_VM_32BIT | UAE_VM_WRITE_WATCH);
#endif

	if (!natmem_reserved) {
		if (natmem_size <= 768 * 1024 * 1024) {
			uae_u32 p = 0x78000000 - natmem_size;
			for (;;) {
				natmem_reserved = (uae_u8*) VirtualAlloc((void*)(intptr_t)p, natmem_size, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE);
				if (natmem_reserved)
					break;
				p -= 128 * 1024 * 1024;
				if (p <= 128 * 1024 * 1024)
					break;
			}
		}
	}
	if (!natmem_reserved) {
		DWORD vaflags = MEM_RESERVE | MEM_WRITE_WATCH;
#ifdef _WIN32
#ifndef _WIN64
		if (!os_vista)
			vaflags |= MEM_TOP_DOWN;
#endif
#endif
		for (;;) {
			natmem_reserved = (uae_u8*)VirtualAlloc (NULL, natmem_size, vaflags, PAGE_READWRITE);
			if (natmem_reserved)
				break;
			natmem_size -= 64 * 1024 * 1024;
			if (!natmem_size) {
				write_log (_T("MMAN: Can't allocate 257M of virtual address space!?\n"));
				natmem_size = 17 * 1024 * 1024;
				natmem_reserved = (uae_u8*)VirtualAlloc (NULL, natmem_size, vaflags, PAGE_READWRITE);
				if (!natmem_size) {
					write_log (_T("MMAN: Can't allocate 17M of virtual address space!? Something is seriously wrong\n"));
					notify_user(NUMSG_NOMEMORY);
					return false;
				}
				break;
			}
		}
	}
	natmem_reserved_size = natmem_size;
	natmem_offset = natmem_reserved;
	if (natmem_size <= 257 * 1024 * 1024) {
		max_z3fastmem = 0;
	} else {
		max_z3fastmem = natmem_size;
	}
	write_log (_T("MMAN: Reserved %p-%p (0x%08x %dM)\n"),
			   natmem_reserved, (uae_u8 *) natmem_reserved + natmem_reserved_size,
			   natmem_reserved_size, natmem_reserved_size / (1024 * 1024));

	clear_shm ();

//	write_log (_T("Max Z3FastRAM %dM. Total physical RAM %uM\n"), max_z3fastmem >> 20, totalphys64 >> 20);

	canbang = 1;
	return true;
}

static void resetmem (bool decommit)
{
	int i;

	if (!shm_start)
		return;
	for (i = 0; i < MAX_SHMID; i++) {
		struct uae_shmid_ds *s = &shmids[i];
		int size = s->size;
		uae_u8 *shmaddr;
		uae_u8 *result;

		if (!s->attached)
			continue;
		if (!s->natmembase)
			continue;
		if (s->fake)
			continue;
		if (!decommit && ((uae_u8*)s->attached - (uae_u8*)s->natmembase) >= 0x10000000)
			continue;
		shmaddr = natmem_offset + ((uae_u8*)s->attached - (uae_u8*)s->natmembase);
		if (decommit) {
			VirtualFree (shmaddr, size, MEM_DECOMMIT);
		} else {
			result = virtualallocwithlock (shmaddr, size, decommit ? MEM_DECOMMIT : MEM_COMMIT, PAGE_READWRITE);
			if (result != shmaddr)
				write_log (_T("MMAN: realloc(%p-%p,%d,%d,%s) failed, err=%d\n"), shmaddr, shmaddr + size, size, s->mode, s->name, GetLastError ());
			else
				write_log (_T("MMAN: rellocated(%p-%p,%d,%s)\n"), shmaddr, shmaddr + size, size, s->name);
		}
	}
}

static uae_u8 *va (uae_u32 offset, uae_u32 len, DWORD alloc, DWORD protect)
{
	uae_u8 *addr;

	addr = (uae_u8*)VirtualAlloc (natmem_offset + offset, len, alloc, protect);
	if (addr) {
		write_log (_T("VA(%p - %p, %4uM, %s)\n"),
			natmem_offset + offset, natmem_offset + offset + len, len >> 20, (alloc & MEM_WRITE_WATCH) ? _T("WATCH") : _T("RESERVED"));
		return addr;
	}
	write_log (_T("VA(%p - %p, %4uM, %s) failed %d\n"),
		natmem_offset + offset, natmem_offset + offset + len, len >> 20, (alloc & MEM_WRITE_WATCH) ? _T("WATCH") : _T("RESERVED"), GetLastError ());
	return NULL;
}

static int doinit_shm (void)
{
	uae_u32 totalsize, totalsize_z3;
	uae_u32 align;
	uae_u32 z3rtgmem_size;
	struct rtgboardconfig *rbc = &changed_prefs.rtgboards[0];
	struct rtgboardconfig *crbc = &currprefs.rtgboards[0];
	uae_u32 extra = 65536;
	struct uae_prefs *p = &changed_prefs;

	changed_prefs.z3autoconfig_start = currprefs.z3autoconfig_start = 0;
	set_expamem_z3_hack_mode(0);
	expansion_scan_autoconfig(&currprefs, true);

	canbang = 1;
	natmem_offset = natmem_reserved;

	align = 16 * 1024 * 1024 - 1;
	totalsize = 0x01000000;

	z3rtgmem_size = gfxboard_get_configtype(rbc) == 3 ? rbc->rtgmem_size : 0;

	if (p->cpu_model >= 68020)
		totalsize = 0x10000000;
	totalsize += (p->z3chipmem_size + align) & ~align;
	totalsize_z3 = totalsize;

	start_rtg = 0;
	end_rtg = 0;

	jit_direct_compatible_memory = p->cachesize && (!p->comptrustbyte || !p->comptrustword || !p->comptrustlong);
#if 0
	if (jit_direct_compatible_memory && expamem_z3_highram_uae > 0x80000000) {
		write_log(_T("MMAN: RAM outside of 31-bit address space. Switching off JIT Direct.\n"));
		jit_direct_compatible_memory = false;
	}
#endif
	// 1G Z3chip?
	if ((Z3BASE_UAE + p->z3chipmem_size > Z3BASE_REAL) ||
		// real wrapped around
		(expamem_z3_highram_real == 0xffffffff) ||
		// Real highram > 0x80000000 && UAE highram <= 0x80000000 && Automatic
		(expamem_z3_highram_real > 0x80000000 && expamem_z3_highram_uae <= 0x80000000 && p->z3_mapping_mode == Z3MAPPING_AUTO) ||
		// Wanted UAE || Blizzard RAM
		p->z3_mapping_mode == Z3MAPPING_UAE || cpuboard_memorytype(&changed_prefs) == BOARD_MEMORY_BLIZZARD_12xx ||
		// JIT && Automatic && Real does not fit in NATMEM && UAE fits in NATMEM
		(expamem_z3_highram_real + extra >= natmem_reserved_size && expamem_z3_highram_uae + extra <= natmem_reserved_size && p->z3_mapping_mode == Z3MAPPING_AUTO && jit_direct_compatible_memory)) {
		changed_prefs.z3autoconfig_start = currprefs.z3autoconfig_start = Z3BASE_UAE;
		if (p->z3_mapping_mode == Z3MAPPING_AUTO)
			write_log(_T("MMAN: Selected UAE Z3 mapping mode\n"));
		set_expamem_z3_hack_mode(Z3MAPPING_UAE);
		if (expamem_z3_highram_uae > totalsize_z3) {
			totalsize_z3 = expamem_z3_highram_uae;
		}
	} else {
		if (p->z3_mapping_mode == Z3MAPPING_AUTO)
			write_log(_T("MMAN: Selected REAL Z3 mapping mode\n"));
		changed_prefs.z3autoconfig_start = currprefs.z3autoconfig_start = Z3BASE_REAL;
		set_expamem_z3_hack_mode(Z3MAPPING_REAL);
		if (expamem_z3_highram_real > totalsize_z3 && jit_direct_compatible_memory) {
			totalsize_z3 = expamem_z3_highram_real;
			if (totalsize_z3 + extra >= natmem_reserved_size) {
				jit_direct_compatible_memory = false;
				write_log(_T("MMAN: Not enough direct memory for Z3REAL. Switching off JIT Direct.\n"));
			}
		}
	}
	write_log(_T("Total %uM Z3 Total %uM, HM %uM\n"), totalsize >> 20, totalsize_z3 >> 20, expamem_highmem_pointer >> 20);

	if (totalsize_z3 < expamem_highmem_pointer)
		totalsize_z3 = expamem_highmem_pointer;

	expansion_scan_autoconfig(&currprefs, true);

	if (jit_direct_compatible_memory && (totalsize > size64 || totalsize + extra >= natmem_reserved_size)) {
		jit_direct_compatible_memory = false;
		write_log(_T("MMAN: Not enough direct memory. Switching off JIT Direct.\n"));
	}

	int idx = 0;
	for (;;) {
		struct autoconfig_info *aci = expansion_get_autoconfig_data(&currprefs, idx++);
		if (!aci)
			break;
		addrbank *ab = aci->addrbank;
		if (!ab)
			continue;
		if (aci->direct_vram && aci->start != 0xffffffff) {
			if (!start_rtg)
				start_rtg = aci->start;
			end_rtg = aci->start + aci->size;
		}
	}

	// rtg outside of natmem?
	if (start_rtg > 0 && start_rtg < 0xffffffff && end_rtg > natmem_reserved_size) {
		if (jit_direct_compatible_memory) {
			write_log(_T("MMAN: VRAM outside of natmem (%08x > %08x), switching off JIT Direct.\n"), end_rtg, natmem_reserved_size);
			jit_direct_compatible_memory = false;
		}
		if (end_rtg - start_rtg > natmem_reserved_size) {
			write_log(_T("MMAN: VRAMs don't fit in natmem space! (%08x > %08x)\n"), end_rtg - start_rtg, natmem_reserved_size);
			notify_user(NUMSG_NOMEMORY);
			return -1;
		}
#ifdef _WIN64
		// 64-bit can't do natmem_offset..
		notify_user(NUMSG_NOMEMORY);
		return -1;
#else

		p96base_offset = start_rtg;
		p96mem_size = end_rtg - start_rtg;
		write_log("MMAN: rtgbase_offset = %08x, size %08x\n", p96base_offset, p96mem_size);
		// adjust p96mem_offset to beginning of natmem
		// by subtracting start of original p96mem_offset from natmem_offset
		if (p96base_offset >= 0x10000000) {
			natmem_offset = natmem_reserved - p96base_offset;
			p96mem_offset = natmem_offset + p96base_offset;
		}
#endif
	} else {
		start_rtg = 0;
		end_rtg = 0;
	}

	idx = 0;
	for (;;) {
		struct autoconfig_info *aci = expansion_get_autoconfig_data(&currprefs, idx++);
		if (!aci)
			break;
		addrbank *ab = aci->addrbank;
		// disable JIT direct from Z3 boards that are outside of natmem
		for (int i = 0; i < MAX_RAM_BOARDS; i++) {
			if (&z3fastmem_bank[i] == ab) {
				ab->flags &= ~ABFLAG_ALLOCINDIRECT;
				ab->jit_read_flag = 0;
				ab->jit_write_flag = 0;
				if (aci->start + aci->size > natmem_reserved_size) {
					write_log(_T("%s %08x-%08x: not JIT direct capable (>%08x)!\n"), ab->name, aci->start, aci->start + aci->size - 1, natmem_reserved_size);
					ab->flags |= ABFLAG_ALLOCINDIRECT;
					ab->jit_read_flag = S_READ;
					ab->jit_write_flag = S_WRITE;
				}
			}
		}
	}

#if 0
	z3offset = 0;
	if (changed_prefs.z3_mapping_mode != Z3MAPPING_UAE && cpuboard_memorytype(&changed_prefs) != BOARD_MEMORY_BLIZZARD_12xx) {
		if (1 && natmem_reserved_size > 0x40000000 && natmem_reserved_size - 0x40000000 >= (totalsize - 0x10000000 - ((changed_prefs.z3chipmem_size + align) & ~align)) && changed_prefs.z3chipmem_size <= 512 * 1024 * 1024) {
			changed_prefs.z3autoconfig_start = currprefs.z3autoconfig_start = Z3BASE_REAL;
			z3offset += Z3BASE_REAL - Z3BASE_UAE - ((changed_prefs.z3chipmem_size + align) & ~align);
			z3offset += cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype].z3extra;
			set_expamem_z3_hack_override(true);
			startbarrier = 0;
			write_log(_T("Z3 REAL mapping. JIT direct compatible.\n"));
			jit_direct_compatible_memory = true;
		} else if (changed_prefs.z3_mapping_mode == Z3MAPPING_AUTO && currprefs.cachesize) {
			changed_prefs.z3autoconfig_start = currprefs.z3autoconfig_start = Z3BASE_UAE;
			jit_direct_compatible_memory = true;
			write_log(_T("Z3 UAE mapping (auto).\n"));
		} else {
			changed_prefs.z3autoconfig_start = currprefs.z3autoconfig_start = Z3BASE_REAL;
			write_log(_T("Z3 REAL mapping. Not JIT direct compatible.\n"));
			jit_direct_compatible_memory = false;
		}
	} else {
		currprefs.z3autoconfig_start = changed_prefs.z3autoconfig_start = Z3BASE_UAE;
		jit_direct_compatible_memory = true;
		write_log(_T("Z3 UAE mapping.\n"));
	}
#endif

#if 0
	p96mem_offset = NULL;
	p96mem_size = z3rtgmem_size;
	p96base_offset = 0;
	uae_u32 z3rtgallocsize = 0;
	if (rbc->rtgmem_size && gfxboard_get_configtype(rbc) == 3) {
		z3rtgallocsize = gfxboard_get_autoconfig_size(rbc) < 0 ? rbc->rtgmem_size : gfxboard_get_autoconfig_size(rbc);
		if (changed_prefs.z3autoconfig_start == Z3BASE_UAE)
			p96base_offset = natmemsize + startbarrier + z3offset;
		else
			p96base_offset = expansion_startaddress(natmemsize + startbarrier + z3offset, z3rtgallocsize);
	} else if (rbc->rtgmem_size && gfxboard_get_configtype(rbc) == 2) {
		p96base_offset = getz2rtgaddr (rbc);
	} else if (rbc->rtgmem_size && gfxboard_get_configtype(rbc) == 1) {
		p96base_offset = 0xa80000;
	}
	if (p96base_offset) {
		if (jit_direct_compatible_memory) {
			p96mem_offset = natmem_offset + p96base_offset;
		} else {
			if (changed_prefs.cachesize) {
				crbc->rtgmem_size = rbc->rtgmem_size = 0;
				crbc->rtgmem_type = rbc->rtgmem_type = 0;
				error_log(_T("RTG board is not anymore supported when JIT is enabled and RTG VRAM is located outside of NATMEM (Real Z3 mode under 32-bit Windows)."));
			} else {
				// calculate Z3 alignment (argh, I thought only Z2 needed this..)
				uae_u32 addr = Z3BASE_REAL;
				int z3off = cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype].z3extra;
				if (z3off) {
					addr = expansion_startaddress(addr, z3off);
					addr += z3off;
				}
				addr = expansion_startaddress(addr, changed_prefs.z3fastmem_size);
				addr += changed_prefs.z3fastmem_size;
				addr = expansion_startaddress(addr, changed_prefs.z3fastmem2_size);
				addr += changed_prefs.z3fastmem2_size;
				addr = expansion_startaddress(addr, z3rtgallocsize);
				if (gfxboard_get_configtype(rbc) == 3) {
					p96base_offset = addr;
					write_log("NATMEM: p96base_offset = 0x%x\n", p96base_offset);
					// adjust p96mem_offset to beginning of natmem
					// by subtracting start of original p96mem_offset from natmem_offset
					if (p96base_offset >= 0x10000000) {
						natmem_offset = natmem_reserved - p96base_offset;
						p96mem_offset = natmem_offset + p96base_offset;
					}
				}
			}
		}
	}
#endif

	if (!natmem_offset) {
		write_log (_T("MMAN: No special area could be allocated! err=%d\n"), GetLastError ());
	} else {
		write_log(_T("MMAN: Our special area: %p-%p (0x%08x %dM)\n"),
			natmem_offset, (uae_u8*)natmem_offset + totalsize,
			totalsize, totalsize / (1024 * 1024));
#if 0
		if (rbc->rtgmem_size)
			write_log (_T("NATMEM: RTG special area: %p-%p (0x%08x %dM)\n"),
				p96mem_offset, (uae_u8*)p96mem_offset + rbc->rtgmem_size,
				rbc->rtgmem_size, rbc->rtgmem_size >> 20);
#endif
		canbang = jit_direct_compatible_memory ? 1 : 0;
	}

	return canbang;
}

static uae_u32 oz3fastmem_size[MAX_RAM_BOARDS];
static uae_u32 ofastmem_size[MAX_RAM_BOARDS];
static uae_u32 oz3chipmem_size;
static uae_u32 ortgmem_size[MAX_RTG_BOARDS];
static int ortgmem_type[MAX_RTG_BOARDS];

bool init_shm (void)
{
	bool changed = false;

	for (int i = 0; i < MAX_RAM_BOARDS; i++) {
		if (oz3fastmem_size[i] != changed_prefs.z3fastmem[i].size)
			changed = true;
		if (ofastmem_size[i] != changed_prefs.fastmem[i].size)
			changed = true;
	}
	for (int i = 0; i < MAX_RTG_BOARDS; i++) {
		if (ortgmem_size[i] != changed_prefs.rtgboards[i].rtgmem_size)
			changed = true;
		if (ortgmem_type[i] != changed_prefs.rtgboards[i].rtgmem_type)
			changed = true;
	}
	if (!changed && oz3chipmem_size == changed_prefs.z3chipmem_size)
		return true;

	for (int i = 0; i < MAX_RAM_BOARDS;i++) {
		oz3fastmem_size[i] = changed_prefs.z3fastmem[i].size;
		ofastmem_size[i] = changed_prefs.fastmem[i].size;
	}
	for (int i = 0; i < MAX_RTG_BOARDS; i++) {
		ortgmem_size[i] = changed_prefs.rtgboards[i].rtgmem_size;
		ortgmem_type[i] = changed_prefs.rtgboards[i].rtgmem_type;
	}
	oz3chipmem_size = changed_prefs.z3chipmem_size;

	if (doinit_shm () < 0)
		return false;

	resetmem (false);
	clear_shm ();

	memory_hardreset (2);
	return true;
}

void free_shm (void)
{
	resetmem (true);
	clear_shm ();
	for (int i = 0; i < MAX_RAM_BOARDS; i++) {
		ortgmem_type[i] = -1;
	}
}

void mapped_free (addrbank *ab)
{
	shmpiece *x = shm_start;
	bool rtgmem = (ab->flags & ABFLAG_RTG) != 0;

	ab->flags &= ~ABFLAG_MAPPED;
	if (ab->baseaddr == NULL)
		return;

	if (ab->flags & ABFLAG_INDIRECT) {
		while(x) {
			if (ab->baseaddr == x->native_address) {
				int shmid = x->id;
				shmids[shmid].key = -1;
				shmids[shmid].name[0] = '\0';
				shmids[shmid].size = 0;
				shmids[shmid].attached = 0;
				shmids[shmid].mode = 0;
				shmids[shmid].natmembase = 0;
				if (!(ab->flags & ABFLAG_NOALLOC)) {
					xfree(ab->baseaddr);
					ab->baseaddr = NULL;
				}
			}
			x = x->next;
		}
		ab->baseaddr = NULL;
		ab->flags &= ~ABFLAG_DIRECTMAP;
		ab->allocated_size = 0;
		write_log(_T("mapped_free indirect %s\n"), ab->name);
		return;
	}

	if (!(ab->flags & ABFLAG_DIRECTMAP)) {
		if (!(ab->flags & ABFLAG_NOALLOC)) {
			xfree(ab->baseaddr);
		}
		ab->baseaddr = NULL;
		ab->allocated_size = 0;
		write_log(_T("mapped_free nondirect %s\n"), ab->name);
		return;
	}

	while(x) {
		if(ab->baseaddr == x->native_address)
			uae_shmdt (x->native_address);
		x = x->next;
	}
	x = shm_start;
	while(x) {
		struct uae_shmid_ds blah;
		if (ab->baseaddr == x->native_address) {
			if (uae_shmctl (x->id, UAE_IPC_STAT, &blah) == 0)
				uae_shmctl (x->id, UAE_IPC_RMID, &blah);
		}
		x = x->next;
	}
	ab->baseaddr = NULL;
	ab->allocated_size = 0;
	write_log(_T("mapped_free direct %s\n"), ab->name);
}

static uae_key_t get_next_shmkey (void)
{
	uae_key_t result = -1;
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

STATIC_INLINE uae_key_t find_shmkey (uae_key_t key)
{
	int result = -1;
	if(shmids[key].key == key) {
		result = key;
	}
	return result;
}

bool uae_mman_info(addrbank *ab, struct uae_mman_data *md)
{
	bool got = false;
	bool readonly = false, maprom = false;
	bool directsupport = true;
	uaecptr start;
	uae_u32 size = ab->reserved_size;
	uae_u32 readonlysize = size;
	bool barrier = false;

	if (!_tcscmp(ab->label, _T("*"))) {
		start = ab->start;
		got = true;
		if (expansion_get_autoconfig_by_address(&currprefs, ab->start) && !expansion_get_autoconfig_by_address(&currprefs, ab->start + size))
			barrier = true;
	} else if (!_tcscmp(ab->label, _T("*B"))) {
		start = ab->start;
		got = true;
		barrier = true;
	} else if (!_tcscmp(ab->label, _T("chip"))) {
		start = 0;
		got = true;
		if (!expansion_get_autoconfig_by_address(&currprefs, 0x00200000) && currprefs.chipmem_size == 2 * 1024 * 1024)
			barrier = true;
		if (currprefs.chipmem_size != 2 * 1024 * 1024)
			barrier = true;
	} else if (!_tcscmp(ab->label, _T("kick"))) {
		start = 0xf80000;
		got = true;
		barrier = true;
		readonly = true;
		maprom = true;
	} else if (!_tcscmp(ab->label, _T("rom_a8"))) {
		start = 0xa80000;
		got = true;
		readonly = true;
		maprom = true;
	} else if (!_tcscmp(ab->label, _T("rom_e0"))) {
		start = 0xe00000;
		got = true;
		readonly = true;
		maprom = true;
	} else if (!_tcscmp(ab->label, _T("rom_f0"))) {
		start = 0xf00000;
		got = true;
		readonly = true;
	} else if (!_tcscmp(ab->label, _T("rom_f0_ppc"))) {
		// this is flash and also contains IO
		start = 0xf00000;
		got = true;
		readonly = false;
	} else if (!_tcscmp(ab->label, _T("rtarea"))) {
		start = rtarea_base;
		got = true;
		readonly = true;
		readonlysize = RTAREA_TRAPS;
	} else if (!_tcscmp(ab->label, _T("ramsey_low"))) {
		start = a3000lmem_bank.start;
		if (!a3000hmem_bank.start)
			barrier = true;
		got = true;
	} else if (!_tcscmp(ab->label, _T("csmk1_maprom"))) {
		start = 0x07f80000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("25bitram"))) {
		start = 0x01000000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("ramsey_high"))) {
		start = 0x08000000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("dkb"))) {
		start = 0x10000000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("fusionforty"))) {
		start = 0x11000000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("blizzard_40"))) {
		start = 0x40000000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("blizzard_48"))) {
		start = 0x48000000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("blizzard_68"))) {
		start = 0x68000000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("blizzard_70"))) {
		start = 0x70000000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("cyberstorm"))) {
		start = 0x0c000000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("cyberstormmaprom"))) {
		start = 0xfff00000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("bogo"))) {
		start = 0x00C00000;
		got = true;
		if (currprefs.bogomem_size <= 0x100000)
			barrier = true;
	} else if (!_tcscmp(ab->label, _T("custmem1"))) {
		start = currprefs.custom_memory_addrs[0];
		got = true;
	} else if (!_tcscmp(ab->label, _T("custmem2"))) {
		start = currprefs.custom_memory_addrs[1];
		got = true;
	} else if (!_tcscmp(ab->label, _T("hrtmem"))) {
		start = 0x00a10000;
		got = true;
	} else if (!_tcscmp(ab->label, _T("arhrtmon"))) {
		start = 0x00800000;
		barrier = true;
		got = true;
	} else if (!_tcscmp(ab->label, _T("xpower_e2"))) {
		start = 0x00e20000;
		barrier = true;
		got = true;
	} else if (!_tcscmp(ab->label, _T("xpower_f2"))) {
		start = 0x00f20000;
		barrier = true;
		got = true;
	} else if (!_tcscmp(ab->label, _T("nordic_f0"))) {
		start = 0x00f00000;
		barrier = true;
		got = true;
	} else if (!_tcscmp(ab->label, _T("nordic_f4"))) {
		start = 0x00f40000;
		barrier = true;
		got = true;
	} else if (!_tcscmp(ab->label, _T("nordic_f6"))) {
		start = 0x00f60000;
		barrier = true;
		got = true;
	} else if (!_tcscmp(ab->label, _T("superiv_b0"))) {
		start = 0x00b00000;
		barrier = true;
		got = true;
	} else if (!_tcscmp(ab->label, _T("superiv_d0"))) {
		start = 0x00d00000;
		barrier = true;
		got = true;
	} else if (!_tcscmp(ab->label, _T("superiv_e0"))) {
		start = 0x00e00000;
		barrier = true;
		got = true;
	} else if (!_tcscmp(ab->label, _T("ram_a8"))) {
		start = 0x00a80000;
		barrier = true;
		got = true;
	} else {
		directsupport = false;
	}
	if (got) {
		md->start = start;
		md->size = size;
		md->readonly = readonly;
		md->readonlysize = readonlysize;
		md->maprom = maprom;
		md->hasbarrier = barrier;

		if (start_rtg && end_rtg) {
			if (start < start_rtg || start + size > end_rtg)
				directsupport = false;
		} else if (start >= natmem_reserved_size || start + size > natmem_reserved_size) {
			// start + size may cause 32-bit overflow
			directsupport = false;
		}
		md->directsupport = directsupport;
		if (md->hasbarrier) {
			md->size += BARRIER;
		}
	}
	return got;
}

void *uae_shmat (addrbank *ab, int shmid, void *shmaddr, int shmflg, struct uae_mman_data *md)
{
	void *result = (void *)-1;
	bool got = false, readonly = false, maprom = false;
	int p96special = FALSE;
	struct uae_mman_data md2;

#ifdef NATMEM_OFFSET

	unsigned int size = shmids[shmid].size;
	unsigned int readonlysize = size;

	if (shmids[shmid].attached)
		return shmids[shmid].attached;

	if (ab->flags & ABFLAG_INDIRECT) {
		shmids[shmid].attached = ab->baseaddr;
		shmids[shmid].fake = true;
		return shmids[shmid].attached;
	}

	if ((uae_u8*)shmaddr < natmem_offset) {
		if (!md) {
			if (!uae_mman_info(ab, &md2))
				return NULL;
			md = &md2;
		}
		if (!shmaddr) {
			shmaddr = natmem_offset + md->start;
			size = md->size;
			readonlysize = md->readonlysize;
			readonly = md->readonly;
			maprom = md->maprom;
			got = true;
		}
	}

	uintptr_t natmem_end = (uintptr_t) natmem_reserved + natmem_reserved_size;
	if (md && md->hasbarrier && (uintptr_t) shmaddr + size > natmem_end && (uintptr_t)shmaddr <= natmem_end) {
		/* We cannot add a barrier beyond the end of the reserved memory. */
		//assert((uintptr_t) shmaddr + size - natmem_end == BARRIER);
		write_log(_T("NATMEM: Removing barrier (%d bytes) beyond reserved memory\n"), BARRIER);
		size -= BARRIER;
		md->size -= BARRIER;
		md->hasbarrier = false;
	}

#endif

	if (shmids[shmid].key == shmid && shmids[shmid].size) {
		DWORD protect = readonly ? PAGE_READONLY : PAGE_READWRITE;
		shmids[shmid].mode = protect;
		shmids[shmid].rosize = readonlysize;
		shmids[shmid].natmembase = natmem_offset;
		shmids[shmid].maprom = maprom ? 1 : 0;
		if (shmaddr)
			virtualfreewithlock (shmaddr, size, MEM_DECOMMIT);
		result = virtualallocwithlock (shmaddr, size, MEM_COMMIT, PAGE_READWRITE);
		if (result == NULL)
			virtualfreewithlock (shmaddr, 0, MEM_DECOMMIT);
		result = virtualallocwithlock (shmaddr, size, MEM_COMMIT, PAGE_READWRITE);
		if (result == NULL) {
			result = (void*)-1;
			error_log (_T("Memory %s (%s) failed to allocate %p: VA %08X - %08X %x (%dk). Error %d."),
				shmids[shmid].name, ab ? ab->name : _T("?"), shmaddr,
				(uae_u8*)shmaddr - natmem_offset, (uae_u8*)shmaddr - natmem_offset + size,
				size, size >> 10, GetLastError ());
		} else {
			shmids[shmid].attached = result;
			write_log (_T("%p: VA %08lX - %08lX %x (%dk) ok (%p)%s\n"),
				shmaddr, (uae_u8*)shmaddr - natmem_offset, (uae_u8*)shmaddr - natmem_offset + size,
				size, size >> 10, shmaddr, p96special ? _T(" RTG") : _T(""));
		}
	}
	return result;
}

void unprotect_maprom (void)
{
	bool protect = false;
	for (int i = 0; i < MAX_SHMID; i++) {
		struct uae_shmid_ds *shm = &shmids[i];
		if (shm->mode != PAGE_READONLY)
			continue;
		if (!shm->attached || !shm->rosize)
			continue;
		if (shm->maprom <= 0)
			continue;
		shm->maprom = -1;
		DWORD old;
		if (!VirtualProtect (shm->attached, shm->rosize, protect ? PAGE_READONLY : PAGE_READWRITE, &old)) {
			write_log (_T("unprotect_maprom VP %08lX - %08lX %x (%dk) failed %d\n"),
				(uae_u8*)shm->attached - natmem_offset, (uae_u8*)shm->attached - natmem_offset + shm->size,
				shm->size, shm->size >> 10, GetLastError ());
		}
	}
}

void protect_roms (bool protect)
{
	if (protect) {
		// protect only if JIT enabled, always allow unprotect
		if (!currprefs.cachesize || currprefs.comptrustbyte || currprefs.comptrustword || currprefs.comptrustlong)
			return;
	}
	for (int i = 0; i < MAX_SHMID; i++) {
		struct uae_shmid_ds *shm = &shmids[i];
		if (shm->mode != PAGE_READONLY)
			continue;
		if (!shm->attached || !shm->rosize)
			continue;
		if (shm->maprom < 0 && protect)
			continue;
		DWORD old;
		if (!VirtualProtect (shm->attached, shm->rosize, protect ? PAGE_READONLY : PAGE_READWRITE, &old)) {
			write_log (_T("protect_roms VP %08lX - %08lX %x (%dk) failed %d\n"),
				(uae_u8*)shm->attached - natmem_offset, (uae_u8*)shm->attached - natmem_offset + shm->rosize,
				shm->rosize, shm->rosize >> 10, GetLastError ());
		} else {
			write_log(_T("ROM VP %08lX - %08lX %x (%dk) %s\n"),
				(uae_u8*)shm->attached - natmem_offset, (uae_u8*)shm->attached - natmem_offset + shm->rosize,
				shm->rosize, shm->rosize >> 10, protect ? _T("WPROT") : _T("UNPROT"));
		}
	}
}

int uae_shmdt (const void *shmaddr)
{
	return 0;
}

int uae_shmget (uae_key_t key, addrbank *ab, int shmflg)
{
	int result = -1;

	if ((key == UAE_IPC_PRIVATE) || ((shmflg & UAE_IPC_CREAT) && (find_shmkey (key) == -1))) {
		write_log (_T("shmget of size %zd (%zdk) for %s (%s)\n"), ab->reserved_size, ab->reserved_size >> 10, ab->label, ab->name);
		if ((result = get_next_shmkey ()) != -1) {
			shmids[result].size = ab->reserved_size;
			_tcscpy (shmids[result].name, ab->label);
		} else {
			result = -1;
		}
	}
	return result;
}

int uae_shmctl (int shmid, int cmd, struct uae_shmid_ds *buf)
{
	int result = -1;

	if ((find_shmkey (shmid) != -1) && buf) {
		switch (cmd)
		{
		case UAE_IPC_STAT:
			*buf = shmids[shmid];
			result = 0;
			break;
		case UAE_IPC_RMID:
			VirtualFree (shmids[shmid].attached, shmids[shmid].size, MEM_DECOMMIT);
			shmids[shmid].key = -1;
			shmids[shmid].name[0] = '\0';
			shmids[shmid].size = 0;
			shmids[shmid].attached = 0;
			shmids[shmid].mode = 0;
			result = 0;
			break;
		}
	}
	return result;
}

#endif

int isinf (double x)
{
	const int nClass = _fpclass (x);
	int result;
	if (nClass == _FPCLASS_NINF || nClass == _FPCLASS_PINF)
		result = 1;
	else
		result = 0;
	return result;
}
