
/* I made this originally for AROS m68k */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <proto/exec.h>

#include "header.h"

#define MMU030 1
#define MMU040 2
#define MMU060 3

#define CM_WRITETHROUGH 0
#define CM_COPYBACK 1
#define CM_SERIALIZED 2
#define CM_NONCACHEABLE 3

#define LEVELA_SIZE 7
#define LEVELB_SIZE 7
#define LEVELC_SIZE 6
#define PAGE_SIZE 12 // = 1 << 12 = 4096

/* Macros that hopefully make MMU magic a bit easier to understand.. */

#define LEVELA_VAL(x) ((((ULONG)(x)) >> (32 - (LEVELA_SIZE							))) & ((1 << LEVELA_SIZE) - 1))
#define LEVELB_VAL(x) ((((ULONG)(x)) >> (32 - (LEVELA_SIZE + LEVELB_SIZE			  ))) & ((1 << LEVELB_SIZE) - 1))
#define LEVELC_VAL(x) ((((ULONG)(x)) >> (32 - (LEVELA_SIZE + LEVELB_SIZE + LEVELC_SIZE))) & ((1 << LEVELC_SIZE) - 1))

#define LEVELA(root, x) (root[LEVELA_VAL(x)])
#define LEVELB(a, x) (((ULONG*)(((ULONG)a) & ~((1 << (LEVELB_SIZE + 2)) - 1)))[LEVELB_VAL(x)])
#define LEVELC(b, x) (((ULONG*)(((ULONG)b) & ~((1 << (LEVELC_SIZE + 2)) - 1)))[LEVELC_VAL(x)])

#define INVALID_DESCRIPTOR 0xDEAD0000
#define ISINVALID(x) ((((ULONG)x) & 3) == 0)

static BOOL map_region2(struct uaestate *st, void *addr, void *physaddr, ULONG size, BOOL invalid, BOOL writeprotect, BOOL supervisor, UBYTE cachemode);


static void map_pagetable(struct uaestate *st, void *addr, ULONG size)
{
	/* 68040+ MMU tables should be serialized */
	map_region2(st, addr, NULL, size, FALSE, FALSE, FALSE, CM_SERIALIZED);
}

/* Allocate MMU descriptor page, it needs to be (1 << bits) * sizeof(ULONG) aligned */
static ULONG alloc_descriptor(struct uaestate *st, UBYTE bits, UBYTE level)
{
	ULONG *desc, dout;
	ULONG size = sizeof(ULONG) * (1 << bits);
	ULONG ps = 1 << PAGE_SIZE;
	UWORD i;

	while (st->page_free >= size && (((ULONG)st->page_ptr) & (size - 1))) {
		st->page_ptr += 0x100;
		st->page_free -= 0x100;
	}
	while (st->page_free < size) {
		/* allocate in aligned blocks of PAGE_SIZE */
		UBYTE *mem, *newmem, *pagemem;

 		// by design fail if no FAST RAM available
		mem = AllocMem(2 * ps, MEMF_FAST);
		if (!mem)
				return 0;
		Forbid();
		FreeMem(mem, 2 * ps);
		newmem = (UBYTE*)((((ULONG)mem) + ps - 1) & ~(ps - 1));
		pagemem = allocate_abs(ps, (ULONG)newmem, st);
		Permit();
		if (!pagemem)
				return 0;
		st->page_ptr = pagemem;
		st->page_free = ps;
		if (level > 0 && st->mmutype >= MMU040)
			map_pagetable(st, pagemem, ps);
	}
	desc = (ULONG*)st->page_ptr;
	for (i = 0; i < (1 << bits); i++)
		desc[i] = INVALID_DESCRIPTOR;
	dout = (ULONG)desc;
	if (st->mmutype == MMU030)
		dout |= 2; /* Valid 4 byte descriptor */
	else
		dout |= 3; /* Resident descriptor */
	st->page_ptr += size;
	st->page_free -= size;
	return dout;
}	
		
static BOOL map_region2(struct uaestate *st, void *addr, void *physaddr, ULONG size, BOOL invalid, BOOL writeprotect, BOOL supervisor, UBYTE cachemode)
{
	ULONG desca, descb, descc, pagedescriptor;
	ULONG page_size = 1 << PAGE_SIZE;
	ULONG page_mask = page_size - 1;

	if ((size & page_mask) || (((ULONG)addr) & page_mask) || (((ULONG)physaddr) & page_mask))
			return FALSE;
	if (physaddr == NULL)
			physaddr = addr;

	while (size) {
		desca = LEVELA(st->MMU_Level_A, addr);
		if (ISINVALID(desca))
				desca = LEVELA(st->MMU_Level_A, addr) = alloc_descriptor(st, LEVELB_SIZE, 1);
		if (ISINVALID(desca))
				return FALSE;
		descb = LEVELB(desca, addr);
		if (ISINVALID(descb))
				descb = LEVELB(desca, addr) = alloc_descriptor(st, LEVELC_SIZE, 2);
		if (ISINVALID(descb))
				return FALSE;
		descc = LEVELC(descb, addr);

		if (invalid) {
			pagedescriptor = INVALID_DESCRIPTOR;
    } else {
			pagedescriptor = ((ULONG)physaddr) & ~page_mask;
			BOOL wasinvalid = ISINVALID(descc);
			if (st->mmutype == MMU030) {
				pagedescriptor |= 1; // page descriptor
				if (writeprotect || (!wasinvalid && (descc & 4)))
					pagedescriptor |= 4; // write-protected
				/* 68030 can only enable or disable caching */
				if (cachemode >= CM_SERIALIZED || (!wasinvalid && (descc & (1 << 6))))
					pagedescriptor |= 1 << 6;
			} else {
				pagedescriptor |= 3; // resident page
				if (writeprotect || (!wasinvalid && (descc & 4)))
					pagedescriptor |= 4; // write-protected
				if (supervisor || (!wasinvalid && (descc & (1 << 7))))
					pagedescriptor |= 1 << 7;
				// do not override non-cached
				if (wasinvalid || cachemode > ((descc >> 5) & 3))
					pagedescriptor |= cachemode << 5;
				else
					pagedescriptor |= ((descc >> 5) & 3) << 5;
				if (addr != 0 || size != page_size)
					pagedescriptor |= 1 << 10; // global if not zero page
			}
		}

		LEVELC(descb, addr) = pagedescriptor;
		size -= page_size;
		addr += page_size;
		physaddr += page_size;
	}
	return TRUE;
}

BOOL map_region(struct uaestate *st, void *addr, void *physaddr, ULONG size, BOOL invalid, BOOL writeprotect, BOOL supervisor, UBYTE cachemode)
{
	if (addr != physaddr)
		printf("MMU: Remap %08lx-%08lx -> %08lx (I=%d,WP=%d,S=%d)\n", addr, addr + size - 1, physaddr, invalid, writeprotect, supervisor);
	if (!map_region2(st, addr, physaddr, size, invalid, writeprotect, supervisor, cachemode)) {
		printf("MMU: Remap error\n");
		return FALSE;
	}
	return TRUE;
}

BOOL unmap_region(struct uaestate *st, void *addr, ULONG size)
{
	printf("MMU: Unmapped %08lx-%08lx\n", addr, addr + size - 1);
	return map_region2(st, addr, NULL, size, TRUE, FALSE, FALSE, 0);
}

BOOL init_mmu(struct uaestate *st)
{
	st->MMU_Level_A = (ULONG*)(alloc_descriptor(st, LEVELA_SIZE, 0) & ~3);
	if (!st->MMU_Level_A)
		return FALSE;
		st->mmutype = MMU030;
	if (SysBase->AttnFlags & AFF_68040)	
		st->mmutype = MMU040;
	if (st->mmutype >= MMU040)
		map_pagetable(st, st->MMU_Level_A, 1 << PAGE_SIZE);
		
	// Create default 1:1 mapping
	
	// memory
	Forbid();
	struct MemHeader *mh = (struct MemHeader*)SysBase->MemList.lh_Head;
	while (mh->mh_Node.ln_Succ) {
		ULONG mstart = ((ULONG)mh->mh_Lower) & 0xffff0000;
		ULONG msize = ((((ULONG)mh->mh_Upper) + 0xffff) & 0xffff0000) - mstart;
		map_region(st, (void*)mstart, (void*)mstart, msize, FALSE, FALSE, FALSE, CM_WRITETHROUGH);
		mh = (struct MemHeader*)mh->mh_Node.ln_Succ;
	}
	Permit();
	// io
	map_region(st, (void*)0xa00000, (void*)0xa00000, 0xc00000 - 0xa00000, FALSE, FALSE, FALSE, CM_NONCACHEABLE);
	map_region(st, (void*)0xd80000, (void*)0xd80000, 0xe00000 - 0xd80000, FALSE, FALSE, FALSE, CM_NONCACHEABLE);
	map_region(st, (void*)0xe80000, (void*)0xe80000, 0x080000, FALSE, FALSE, FALSE, CM_NONCACHEABLE);
	// rom
	map_region(st, (void*)0xe00000, (void*)0xe00000, 0x080000, FALSE, FALSE, FALSE, 0);
	map_region(st, (void*)0xf80000, (void*)0xf80000, 0x080000, FALSE, FALSE, FALSE, 0);
		
	return TRUE;
}
