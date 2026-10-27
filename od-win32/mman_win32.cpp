// Windows parts of the natmem memory manager (see od-generic/mman.cpp)
// Copyright (C) 2000, Brian King
// GNU Public License

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"
#include "od-generic/mman_host.h"
#include "win32.h"

typedef BOOL (CALLBACK* GLOBALMEMORYSTATUSEX)(LPMEMORYSTATUSEX);

void host_mman_get_memory(uae_u64 *totalphys, uae_u64 *total, bool *is_64bit)
{
	MEMORYSTATUS memstats;
	GLOBALMEMORYSTATUSEX pGlobalMemoryStatusEx;
	MEMORYSTATUSEX memstatsex;

	memstats.dwLength = sizeof(memstats);
	GlobalMemoryStatus(&memstats);
	*totalphys = memstats.dwTotalPhys;
	*total = (uae_u64)memstats.dwAvailPageFile + (uae_u64)memstats.dwTotalPhys;
	pGlobalMemoryStatusEx = (GLOBALMEMORYSTATUSEX)GetProcAddress (GetModuleHandle (_T("kernel32.dll")), "GlobalMemoryStatusEx");
	if (pGlobalMemoryStatusEx) {
		memstatsex.dwLength = sizeof (MEMORYSTATUSEX);
		if (pGlobalMemoryStatusEx(&memstatsex)) {
			*totalphys = memstatsex.ullTotalPhys;
			*total = memstatsex.ullAvailPageFile + memstatsex.ullTotalPhys;
		}
	}
	*is_64bit = os_64bit != 0;
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

// Mark indirect regions (indirect VRAM) as non-accessible when JIT direct is active.
// Beginning of region might have barrier region which is not marked as non-accessible,
// allowing JIT direct to think it is directly accessible VRAM.
// This stays Windows specific: it relies on VirtualProtect() returning the
// previous protection and failing on decommitted pages.
void mman_set_barriers(bool disable)
{
	addrbank *abprev = NULL;
	int maxbank = currprefs.address_space_24 ? MEMORY_BANKS_24 : MEMORY_BANKS;
	for (int i = 0; i < maxbank; i++) {
		uaecptr addr = i * 0x10000;
		addrbank *ab = &get_mem_bank(addr);
		if (ab == abprev) {
			continue;
		}
		int size = 0x10000;
		for (int j = i + 1; j < maxbank; j++) {
			uaecptr addr2 = j * 0x10000;
			addrbank *ab2 = &get_mem_bank(addr2);
			if (ab2 != ab) {
				break;
			}
			size += 0x10000;
		}
		abprev = ab;
		if (ab && ab->baseaddr == NULL && (ab->flags & ABFLAG_ALLOCINDIRECT)) {
			DWORD old;
			if (disable || !currprefs.cachesize || currprefs.comptrustbyte || currprefs.comptrustword || currprefs.comptrustlong) {
				if (!ab->protectmode) {
					ab->protectmode = PAGE_READWRITE;
				}
				if (!VirtualProtect(addr + natmem_offset, size, ab->protectmode, &old)) {
					size = 0x1000;
					VirtualProtect(addr + natmem_offset, size, ab->protectmode, &old);
				}
				write_log("%08x-%08x = access restored (%08x)\n", addr, size, ab->protectmode);
			} else {
				if (VirtualProtect(addr + natmem_offset, size, PAGE_NOACCESS, &old)) {
					ab->protectmode = old;
					write_log("%08x-%08x = set to no access\n", addr, addr + size);
				} else {
					size = 0x1000;
					if (VirtualProtect(addr + natmem_offset, size, PAGE_NOACCESS, &old)) {
						ab->protectmode = old;
						write_log("%08x-%08x = set to no access\n", addr, addr + size);
					}
				}
			}
		}
	}
}

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
