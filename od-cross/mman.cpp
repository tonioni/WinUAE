#include "sysconfig.h"
#include "sysdeps.h"
#include "uae/memory.h"
#include "uae/mman.h"
#include "uae/vm.h"
#include "options.h"
#include "autoconf.h"
#include "gfxboard.h"
#include "cpuboard.h"
#include "rommgr.h"
#include "newcpu.h"

#ifdef __x86_64__
static int os_64bit = 1;
typedef uint64_t ULONG_PTR; 
#else
static int os_64bit = 0;
#ifndef _WIN32
 typedef uint32_t ULONG_PTR;
#endif
#endif

#ifndef _WIN32

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#include <sys/mman.h>

#define MEM_COMMIT       0x00001000
#define MEM_RESERVE      0x00002000
#define MEM_DECOMMIT         0x4000
#define MEM_RELEASE          0x8000
#define MEM_WRITE_WATCH  0x00200000
#define MEM_TOP_DOWN     0x00100000

#define PAGE_NOACCESS          0x01
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_READONLY          0x02
#define PAGE_READWRITE         0x04

typedef void *  PVOID;
typedef void * LPVOID;
typedef size_t SIZE_T;
#undef ULONG
typedef uint32_t ULONG;
typedef ULONG_PTR *PULONG_PTR;
typedef ULONG *PULONG;

typedef struct {
	int dwPageSize;
} SYSTEM_INFO;

static void GetSystemInfo(SYSTEM_INFO *si)
{
	si->dwPageSize = sysconf(_SC_PAGESIZE);
}

#define USE_MMAP

#ifdef USE_MMAP
#ifdef MACOSX
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

static void *VirtualAlloc(void *lpAddress, size_t dwSize, int flAllocationType,
		int flProtect)
{
	write_log("- VirtualAlloc addr=%p size=%zu type=%d prot=%d\n",
			  lpAddress, dwSize, flAllocationType, flProtect);
	if (flAllocationType & MEM_RESERVE) {
		write_log("  MEM_RESERVE\n");
	}
	if (flAllocationType & MEM_COMMIT) {
		write_log("  MEM_COMMIT\n");
	}

	int prot = 0;
	if (flProtect == PAGE_READWRITE) {
		write_log("  PAGE_READWRITE\n");
		prot = UAE_VM_READ_WRITE;
	} else if (flProtect == PAGE_READONLY) {
		write_log("  PAGE_READONLY\n");
		prot = UAE_VM_READ;
	} else if (flProtect == PAGE_EXECUTE_READWRITE) {
		write_log("  PAGE_EXECUTE_READWRITE\n");
		prot = UAE_VM_READ_WRITE_EXECUTE;
	} else {
		write_log("  WARNING: unknown protection\n");
	}

	void *address = NULL;

	if (flAllocationType == MEM_COMMIT && lpAddress == NULL) {
		write_log("NATMEM: Allocated non-reserved memory size %zu\n", dwSize);
		void *memory = uae_vm_alloc(dwSize, 0, UAE_VM_READ_WRITE);
		if (memory == NULL) {
			write_log("memory allocated failed errno %d\n", errno);
		}
		return memory;
	}

	if (flAllocationType & MEM_RESERVE) {
		address = uae_vm_reserve(dwSize, 0);
	} else {
		address = lpAddress;
	}

	if (flAllocationType & MEM_COMMIT) {
		write_log("commit prot=%d\n", prot);
		uae_vm_commit(address, dwSize, prot);
	}

	return address;
}

static int VirtualProtect(void *lpAddress, int dwSize, int flNewProtect,
						  unsigned int *lpflOldProtect)
{
	write_log("- VirtualProtect addr=%p size=%d prot=%d\n",
			  lpAddress, dwSize, flNewProtect);
	int prot = 0;
	if (flNewProtect == PAGE_READWRITE) {
		write_log("  PAGE_READWRITE\n");
		prot = UAE_VM_READ_WRITE;
	} else if (flNewProtect == PAGE_READONLY) {
		write_log("  PAGE_READONLY\n");
		prot = UAE_VM_READ;
	} else {
		write_log("  -- unknown protection --\n");
	}
	if (uae_vm_protect(lpAddress, dwSize, prot) == 0) {
		write_log("mprotect failed errno %d\n", errno);
		return 0;
	}
	return 1;
}

static bool VirtualFree(void *lpAddress, size_t dwSize, int dwFreeType)
{
	int result = 0;
	if (dwFreeType == MEM_DECOMMIT) {
		return uae_vm_decommit(lpAddress, dwSize);
	}
	else if (dwFreeType == MEM_RELEASE) {
		return uae_vm_free(lpAddress, dwSize);
	}
	return 0;
}

static int GetLastError()
{
	return errno;
}

static int my_getpagesize (void)
{
	return uae_vm_page_size();
}

#define getpagesize my_getpagesize

#define WRITE_WATCH_FLAG_RESET 1

typedef uint32_t UINT;
typedef uint32_t DWORD;
typedef DWORD *LPDWORD;

UINT GetWriteWatch(
  DWORD     dwFlags,
  PVOID     lpBaseAddress,
  SIZE_T    dwRegionSize,
  PVOID     *lpAddresses,
  ULONG_PTR *lpdwCount,
  LPDWORD   lpdwGranularity
)
{
	return 0;
}

UINT ResetWriteWatch(
  LPVOID lpBaseAddress,
  SIZE_T dwRegionSize
)
{
	return 0;
}

#endif /* !WIN32 */

/* Prevent od-win32/win32.h from being included */
#define __WIN32_H__

#include "../od-win32/mman.cpp"
