/*
 * Multi-platform virtual memory functions for UAE.
 * Copyright (C) 2015 Frode Solheim
 *
 * Licensed under the terms of the GNU General Public License version 2.
 * See the file 'COPYING' for full license text.
 */

#include "sysconfig.h"
#include "sysdeps.h"
#include "uae/vm.h"
#include "uae/log.h"
#ifdef _WIN32

#else
#include <sys/mman.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

// #define TRACK_ALLOCATIONS

#ifdef TRACK_ALLOCATIONS

struct alloc_size {
	void *address;
	uae_u32 size;
};

#define MAX_ALLOCATIONS 2048
/* A bit inefficient, but good enough for few and rare allocs. Storing
 * the size at the start of the allocated memory would be better, but this
 * could be awkward if/when you want to allocate page-aligned memory. */
static struct alloc_size alloc_sizes[MAX_ALLOCATIONS];

static void add_allocation(void *address, uae_u32 size)
{
	uae_log("add_allocation %p (%d)\n", address, size);
	for (int i = 0; i < MAX_ALLOCATIONS; i++) {
		if (alloc_sizes[i].address == NULL) {
			alloc_sizes[i].address = address;
			alloc_sizes[i].size = size;
			return;
		}
	}
	abort();
}

static uae_u32 find_allocation(void *address)
{
	for (int i = 0; i < MAX_ALLOCATIONS; i++) {
		if (alloc_sizes[i].address == address) {
			return alloc_sizes[i].size;
		}
	}
	abort();
}

static uae_u32 remove_allocation(void *address)
{
	for (int i = 0; i < MAX_ALLOCATIONS; i++) {
		if (alloc_sizes[i].address == address) {
			alloc_sizes[i].address = NULL;
			uae_u32 size = alloc_sizes[i].size;
			alloc_sizes[i].size = 0;
			return size;
		}
	}
	abort();
}

#endif /* TRACK_ALLOCATIONS */

static int protect_to_native(int protect)
{
#ifdef _WIN32
	if (protect == UAE_VM_NO_ACCESS) return PAGE_NOACCESS;
	if (protect == UAE_VM_READ) return PAGE_READONLY;
	if (protect == UAE_VM_READ_WRITE) return PAGE_READWRITE;
	if (protect == UAE_VM_READ_EXECUTE) return PAGE_EXECUTE_READ;
	if (protect == UAE_VM_READ_WRITE_EXECUTE) return PAGE_EXECUTE_READWRITE;
	uae_log("ERROR: invalid protect value %d\n", protect);
	return PAGE_NOACCESS;
#else
	if (protect == UAE_VM_NO_ACCESS) return PROT_NONE;
	if (protect == UAE_VM_READ) return PROT_READ;
	if (protect == UAE_VM_READ_WRITE) return PROT_READ | PROT_WRITE;
	if (protect == UAE_VM_READ_EXECUTE) return PROT_READ | PROT_EXEC;
	if (protect == UAE_VM_READ_WRITE_EXECUTE) {
		return PROT_READ | PROT_WRITE | PROT_EXEC;
	}
	uae_log("ERROR: invalid protect value %d\n", protect);
	return PROT_NONE;
#endif
}

int uae_vm_page_size(void)
{
	static int page_size = 0;
	if (page_size == 0) {
#ifdef _WIN32
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		page_size = si.dwPageSize;
#else
		page_size = sysconf(_SC_PAGESIZE);
#endif
	}
	return page_size;
}

static void *uae_vm_alloc_with_flags(uae_u32 size, int flags, int protect)
{
	void *address = NULL;
	uae_log("uae_vm_alloc(%u, %d, %d)\n", size, flags, protect);
#ifdef _WIN32
	int va_type = MEM_COMMIT;
	int va_protect = protect_to_native(protect);
	address = VirtualAlloc(NULL, size, va_type, va_protect);
#else
	//size = size < uae_vm_page_size() ? uae_vm_page_size() : size;
	int mmap_flags = MAP_PRIVATE | MAP_ANON;
	int mmap_prot = protect_to_native(protect);
#ifdef CPU_64_BIT
	if (flags & UAE_VM_32BIT) {
		mmap_flags |= MAP_32BIT;
	}
#endif
	address = mmap(0, size, mmap_prot, mmap_flags, -1, 0);
	if (address == MAP_FAILED) {
		uae_log("uae_vm_alloc(%u, %d, %d) mmap failed (%d)\n",
				size, flags, protect, errno);
		return NULL;
	}
#endif
#ifdef TRACK_ALLOCATIONS
	add_allocation(address, size);
#endif
	return address;
}

#if 0

void *uae_vm_alloc(uae_u32 size)
{
	return uae_vm_alloc_with_flags(size, UAE_VM_32BIT, UAE_VM_READ_WRITE);
}

void *uae_vm_alloc(uae_u32 size, int flags)
{
	return uae_vm_alloc_with_flags(size, flags, UAE_VM_READ_WRITE);
}

#endif

void *uae_vm_alloc(uae_u32 size, int flags, int protect)
{
	return uae_vm_alloc_with_flags(size, flags, protect);
}

void uae_vm_protect(void *address, int size, int protect)
{
	uae_log("uae_vm_protect(%p, %d, %d)\n", address, size, protect);
#ifdef TRACK_ALLOCATIONS
	uae_u32 allocated_size = find_allocation(address);
	assert(allocated_size == size);
#endif
#ifdef _WIN32
	DWORD old;
	if (VirtualProtect(address, size, protect_to_native(protect), &old) == 0) {
		uae_log("uae_vm_protect(%p, %d, %d) VirtualProtect failed (%d)\n",
				address, size, protect, GetLastError());
	}
#else
	if (mprotect(address, size, protect_to_native(protect)) != 0) {
		uae_log("uae_vm_protect(%p, %d, %d) mprotect failed (%d)\n",
				address, size, protect, errno);
	}
#endif
}

void uae_vm_free(void *address, int size)
{
	uae_log("uae_vm_free(%p, %d)\n", address, size);
#ifdef TRACK_ALLOCATIONS
	uae_u32 allocated_size = remove_allocation(address);
	assert(allocated_size == size);
#endif
#ifdef _WIN32
	VirtualFree(address, 0, MEM_RELEASE);
#else
	if (munmap(address, size) != 0) {
		uae_log("uae_vm_free(%p, %d) munmap failed (%d)\n",
				address, size, errno);
	}
#endif
}
