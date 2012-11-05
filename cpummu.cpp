/*
 * cpummu.cpp -  MMU emulation
 *
 * Copyright (c) 2001-2004 Milan Jurik of ARAnyM dev team (see AUTHORS)
 * 
 * Inspired by UAE MMU patch
 *
 * This file is part of the ARAnyM project which builds a new and powerful
 * TOS/FreeMiNT compatible virtual machine running on almost any hardware.
 *
 * ARAnyM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * ARAnyM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ARAnyM; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define DEBUG 0
#define USETAG 0

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "debug.h"
#include "cpummu.h"

static void REGPARAM3 mmu_flush_atc(uaecptr addr, bool super, bool global) REGPARAM;
static void REGPARAM3 mmu_flush_atc_all(bool global) REGPARAM;

#define DBG_MMU_VERBOSE	1
#define DBG_MMU_SANITY	1

#ifdef FULLMMU

mmu_atc_l1_array atc_l1[2];
mmu_atc_l1_array *current_atc;
static struct mmu_atc_line atc_l2[2][ATC_L2_SIZE];

# ifdef ATC_STATS
static unsigned int mmu_atc_hits[ATC_L2_SIZE];
# endif


static void mmu_dump_ttr(const TCHAR * label, uae_u32 ttr)
{
	DUNUSED(label);
	uae_u32 from_addr, to_addr;

	from_addr = ttr & MMU_TTR_LOGICAL_BASE;
	to_addr = (ttr & MMU_TTR_LOGICAL_MASK) << 8;

	D(bug(_T("%s: [%08lx] %08lx - %08lx enabled=%d supervisor=%d wp=%d cm=%02d\n"),
			label, ttr,
			from_addr, to_addr,
			ttr & MMU_TTR_BIT_ENABLED ? 1 : 0,
			(ttr & (MMU_TTR_BIT_SFIELD_ENABLED | MMU_TTR_BIT_SFIELD_SUPER)) >> MMU_TTR_SFIELD_SHIFT,
			ttr & MMU_TTR_BIT_WRITE_PROTECT ? 1 : 0,
			(ttr & MMU_TTR_CACHE_MASK) >> MMU_TTR_CACHE_SHIFT
		  ));
}

void mmu_make_transparent_region(uaecptr baseaddr, uae_u32 size, int datamode)
{
	uae_u32 * ttr;
	uae_u32 * ttr0 = datamode ? &regs.dtt0 : &regs.itt0;
	uae_u32 * ttr1 = datamode ? &regs.dtt1 : &regs.itt1;

	if ((*ttr1 & MMU_TTR_BIT_ENABLED) == 0)
		ttr = ttr1;
	else if ((*ttr0 & MMU_TTR_BIT_ENABLED) == 0)
		ttr = ttr0;
	else
		return;

	*ttr = baseaddr & MMU_TTR_LOGICAL_BASE;
	*ttr |= ((baseaddr + size - 1) & MMU_TTR_LOGICAL_BASE) >> 8;
	*ttr |= MMU_TTR_BIT_ENABLED;

	D(bug(_T("MMU: map transparent mapping of %08x\n"), *ttr));
}

/* check if an address matches a ttr */
static int mmu_do_match_ttr(uae_u32 ttr, uaecptr addr, bool super)
{
	if (ttr & MMU_TTR_BIT_ENABLED)	{	/* TTR enabled */
		uae_u8 msb, mask;

		msb = ((addr ^ ttr) & MMU_TTR_LOGICAL_BASE) >> 24;
		mask = (ttr & MMU_TTR_LOGICAL_MASK) >> 16;

		if (!(msb & ~mask)) {

			if ((ttr & MMU_TTR_BIT_SFIELD_ENABLED) == 0) {
				if (((ttr & MMU_TTR_BIT_SFIELD_SUPER) == 0) != (super == 0)) {
					return TTR_NO_MATCH;
				}
			}

			return (ttr & MMU_TTR_BIT_WRITE_PROTECT) ? TTR_NO_WRITE : TTR_OK_MATCH;
		}
	}
	return TTR_NO_MATCH;
}

static inline int mmu_match_ttr(uaecptr addr, bool super, bool data)
{
	int res;

	if (data) {
		res = mmu_do_match_ttr(regs.dtt0, addr, super);
		if (res == TTR_NO_MATCH)
			res = mmu_do_match_ttr(regs.dtt1, addr, super);
	} else {
		res = mmu_do_match_ttr(regs.itt0, addr, super);
		if (res == TTR_NO_MATCH)
			res = mmu_do_match_ttr(regs.itt1, addr, super);
	}
	return res;
}

#if DEBUG
/* {{{ mmu_dump_table */
static void mmu_dump_table(const char * label, uaecptr root_ptr)
{
	DUNUSED(label);
	const int ROOT_TABLE_SIZE = 128,
		PTR_TABLE_SIZE = 128,
		PAGE_TABLE_SIZE = 64,
		ROOT_INDEX_SHIFT = 25,
		PTR_INDEX_SHIFT = 18;
	// const int PAGE_INDEX_SHIFT = 12;
	int root_idx, ptr_idx, page_idx;
	uae_u32 root_des, ptr_des, page_des;
	uaecptr ptr_des_addr, page_addr,
		root_log, ptr_log, page_log;

	D(bug(_T("%s: root=%lx\n"), label, root_ptr));

	for (root_idx = 0; root_idx < ROOT_TABLE_SIZE; root_idx++) {
		root_des = phys_get_long(root_ptr + root_idx);

		if ((root_des & 2) == 0)
			continue;	/* invalid */

		D(bug(_T("ROOT: %03d U=%d W=%d UDT=%02d\n"), root_idx,
				root_des & 8 ? 1 : 0,
				root_des & 4 ? 1 : 0,
				root_des & 3
			  ));

		root_log = root_idx << ROOT_INDEX_SHIFT;

		ptr_des_addr = root_des & MMU_ROOT_PTR_ADDR_MASK;

		for (ptr_idx = 0; ptr_idx < PTR_TABLE_SIZE; ptr_idx++) {
			struct {
				uaecptr	log, phys;
				int start_idx, n_pages;	/* number of pages covered by this entry */
				uae_u32 match;
			} page_info[PAGE_TABLE_SIZE];
			int n_pages_used;

			ptr_des = phys_get_long(ptr_des_addr + ptr_idx);
			ptr_log = root_log | (ptr_idx << PTR_INDEX_SHIFT);

			if ((ptr_des & 2) == 0)
				continue; /* invalid */

			page_addr = ptr_des & (regs.mmu_pagesize_8k ? MMU_PTR_PAGE_ADDR_MASK_8 : MMU_PTR_PAGE_ADDR_MASK_4);

			n_pages_used = -1;
			for (page_idx = 0; page_idx < PAGE_TABLE_SIZE; page_idx++) {

				page_des = phys_get_long(page_addr + page_idx);
				page_log = ptr_log | (page_idx << 2);		// ??? PAGE_INDEX_SHIFT

				switch (page_des & 3) {
					case 0: /* invalid */
						continue;
					case 1: case 3: /* resident */
					case 2: /* indirect */
						if (n_pages_used == -1 || page_info[n_pages_used].match != page_des) {
							/* use the next entry */
							n_pages_used++;

							page_info[n_pages_used].match = page_des;
							page_info[n_pages_used].n_pages = 1;
							page_info[n_pages_used].start_idx = page_idx;
							page_info[n_pages_used].log = page_log;
						} else {
							page_info[n_pages_used].n_pages++;
						}
						break;
				}
			}

			if (n_pages_used == -1)
				continue;

			D(bug(_T(" PTR: %03d U=%d W=%d UDT=%02d\n"), ptr_idx,
				ptr_des & 8 ? 1 : 0,
				ptr_des & 4 ? 1 : 0,
				ptr_des & 3
			  ));


			for (page_idx = 0; page_idx <= n_pages_used; page_idx++) {
				page_des = page_info[page_idx].match;

				if ((page_des & MMU_PDT_MASK) == 2) {
					D(bug(_T("  PAGE: %03d-%03d log=%08lx INDIRECT --> addr=%08lx\n"),
							page_info[page_idx].start_idx,
							page_info[page_idx].start_idx + page_info[page_idx].n_pages - 1,
							page_info[page_idx].log,
							page_des & MMU_PAGE_INDIRECT_MASK
						  ));

				} else {
					D(bug(_T("  PAGE: %03d-%03d log=%08lx addr=%08lx UR=%02d G=%d U1/0=%d S=%d CM=%d M=%d U=%d W=%d\n"),
							page_info[page_idx].start_idx,
							page_info[page_idx].start_idx + page_info[page_idx].n_pages - 1,
							page_info[page_idx].log,
							page_des & (regs.mmu_pagesize_8k ? MMU_PAGE_ADDR_MASK_8 : MMU_PAGE_ADDR_MASK_4),
							(page_des & (regs.mmu_pagesize_8k ? MMU_PAGE_UR_MASK_8 : MMU_PAGE_UR_MASK_4)) >> MMU_PAGE_UR_SHIFT,
							page_des & MMU_DES_GLOBAL ? 1 : 0,
							(page_des & MMU_TTR_UX_MASK) >> MMU_TTR_UX_SHIFT,
							page_des & MMU_DES_SUPER ? 1 : 0,
							(page_des & MMU_TTR_CACHE_MASK) >> MMU_TTR_CACHE_SHIFT,
							page_des & MMU_DES_MODIFIED ? 1 : 0,
							page_des & MMU_DES_USED ? 1 : 0,
							page_des & MMU_DES_WP ? 1 : 0
						  ));
				}
			}
		}

	}
}
/* }}} */
#endif

/* {{{ mmu_dump_atc */
void mmu_dump_atc(void)
{
	int i, j;
	for (i = 0; i < 2; i++) {
		for (j = 0; j < ATC_L2_SIZE; j++) {
			if (atc_l2[i][j].tag == 0x8000)
				continue;
			D(bug(_T("ATC[%02d] G=%d TT=%d M=%d WP=%d VD=%d VI=%d tag=%08x --> phys=%08x\n"),
				j, atc_l2[i][j].global, atc_l2[i][j].tt, atc_l2[i][j].modified,
				atc_l2[i][j].write_protect, atc_l2[i][j].valid_data, atc_l2[i][j].valid_inst,
				atc_l2[i][j].tag, atc_l2[i][j].phys));
		}
	}
}
/* }}} */

/* {{{ mmu_dump_tables */
void mmu_dump_tables(void)
{
	D(bug(_T("URP: %08x   SRP: %08x  MMUSR: %x  TC: %x\n"), regs.urp, regs.srp, regs.mmusr, regs.tcr));
	mmu_dump_ttr(_T("DTT0"), regs.dtt0);
	mmu_dump_ttr(_T("DTT1"), regs.dtt1);
	mmu_dump_ttr(_T("ITT0"), regs.itt0);
	mmu_dump_ttr(_T("ITT1"), regs.itt1);
	mmu_dump_atc();
#if DEBUG
	mmu_dump_table("SRP", regs.srp);
#endif
}
/* }}} */

static uaecptr REGPARAM2 mmu_lookup_pagetable(uaecptr addr, bool super, bool write);

static ALWAYS_INLINE int mmu_get_fc(bool super, bool data)
{
	return (super ? 4 : 0) | (data ? 1 : 2);
}

static void mmu_bus_error(uaecptr addr, int fc, bool write, int size)
{
	uae_u16 ssw = 0;

	ssw |= fc & MMU_SSW_TM;				/* Copy TM */
	switch (size) {
	case sz_byte:
		ssw |= MMU_SSW_SIZE_B;
		break;
	case sz_word:
		ssw |= MMU_SSW_SIZE_W;
		break;
	case sz_long:
		ssw |= MMU_SSW_SIZE_L;
		break;
	}

	regs.wb3_status = write ? 0x80 | ssw : 0;
	if (!write)
		ssw |= MMU_SSW_RW;

	regs.mmu_fault_addr = addr;
	regs.mmu_ssw = ssw | MMU_SSW_ATC;

	D(bug(_T("BUS ERROR: fc=%d w=%d log=%08x ssw=%04x PC=%08x\n"), fc, write, addr, ssw, m68k_getpc()));

	//write_log(_T("BUS ERROR: fc=%d w=%d log=%08x ssw=%04x PC=%08x\n"), fc, write, addr, ssw, m68k_getpc());
	//activate_debugger();

	THROW(2);
}

/*
 * Update the atc line for a given address by doing a mmu lookup.
 */
static uaecptr mmu_fill_atc_l2(uaecptr addr, bool super, bool data, bool write, struct mmu_atc_line *l)
{
	int res;
	uae_u32 desc;

	l->tag = ATC_TAG(addr);
	l->hw = l->bus_fault = 0;

	/* check ttr0 */
	res = mmu_match_ttr(addr, super, data);
	if (res != TTR_NO_MATCH) {
		l->tt = 1;
		if (data) {
			l->valid_data = 1;
			l->valid_inst = mmu_match_ttr(addr, super, 0) == res;
		} else {
			l->valid_inst = 1;
			l->valid_data = mmu_match_ttr(addr, super, 1) == res;
		}
		l->global = 1;
		l->modified = 1;
		l->write_protect = (res == TTR_NO_WRITE);
		l->phys = 0;

		return 0;
	}

	l->tt = 0;
	if (!regs.mmu_enabled) {
		l->valid_data = l->valid_inst = 1;
		l->global = 1;
		l->modified = 1;
		l->write_protect = 0;
		l->phys = 0;
		return 0;
	}

	SAVE_EXCEPTION;
	TRY(prb) {
		desc = mmu_lookup_pagetable(addr, super, write);
#if DEBUG > 2
		D(bug(_T("translate: %x,%u,%u,%u -> %x\n"), addr, super, write, data, desc));
#endif
		RESTORE_EXCEPTION;
	}
	CATCH(prb) {
		RESTORE_EXCEPTION;
		/* bus error during table search */
		desc = 0;
		goto fail;
	}

	if ((desc & 1) == 0 || (!super && desc & MMU_MMUSR_S)) {
	fail:
		l->valid_data = l->valid_inst = 0;
		l->global = 0;
	} else {
		l->valid_data = l->valid_inst = 1;
		if (regs.mmu_pagesize_8k)
			l->phys = (desc & ~0x1fff) - (addr & ~0x1fff);
		else
			l->phys = (desc & ~0xfff) - (addr & ~0xfff);
		l->global = (desc & MMU_MMUSR_G) != 0;
		l->modified = (desc & MMU_MMUSR_M) != 0;
		l->write_protect = (desc & MMU_MMUSR_W) != 0;
	}

	return desc;
}

static ALWAYS_INLINE bool mmu_fill_atc_l1(uaecptr addr, bool super, bool data, bool write, struct mmu_atc_line *l1)
{
	int idx = ATC_L2_INDEX(addr);
	int tag = ATC_TAG(addr);
	struct mmu_atc_line *l = &atc_l2[super ? 1 : 0][idx];

	if (l->tag != tag) {
	restart:
		mmu_fill_atc_l2(addr, super, data, write, l);
	}
	if (!(data ? l->valid_data : l->valid_inst)) {
		D(bug(_T("MMU: non-resident page (%x,%x,%x)!\n"), addr, regs.pc, regs.instruction_pc));
		goto fail;
	}
	if (write) {
		if (l->write_protect) {
			D(bug(_T("MMU: write protected (via %s) %lx\n"), l->tt ? "ttr" : "atc", addr));
			goto fail;
		}
		if (!l->modified)
			goto restart;
	}
	*l1 = *l;
#if 0
	uaecptr phys_addr = addr + l1->phys;
	if ((phys_addr & 0xfff00000) == 0x00f00000) {
		l1->hw = 1;
		goto fail;
	}
	if ((phys_addr & 0xfff00000) == 0xfff00000) {
		l1->hw = 1;
		l1->phys -= 0xff000000;
		goto fail;
	}

	if (!test_ram_boundary(phys_addr, 1, super, write)) {
		l1->bus_fault = 1;
		goto fail;
	}
#endif
	return true;

fail:
	l1->tag = ~l1->tag;
	return false;
}

uaecptr REGPARAM2 mmu_translate(uaecptr addr, bool super, bool data, bool write)
{
	struct mmu_atc_line *l;

	l = &atc_l2[super ? 1 : 0][ATC_L2_INDEX(addr)];
	mmu_fill_atc_l2(addr, super, data, write, l);
	if (!(data ? l->valid_data : l->valid_inst))
		THROW(2);

	return addr + l->phys;
}

/*
 * Lookup the address by walking the page table and updating
 * the page descriptors accordingly. Returns the found descriptor
 * or produces a bus error.
 */
static uaecptr REGPARAM2 mmu_lookup_pagetable(uaecptr addr, bool super, bool write)
{
	uae_u32 desc, desc_addr, wp;
	int i;

	wp = 0;
	desc = super ? regs.srp : regs.urp;

	/* fetch root table descriptor */
	i = (addr >> 23) & 0x1fc;
	desc_addr = (desc & MMU_ROOT_PTR_ADDR_MASK) | i;
	desc = phys_get_long(desc_addr);
	if ((desc & 2) == 0) {
		D(bug(_T("MMU: invalid root descriptor for %lx\n"), addr));
		return 0;
	}

	wp |= desc;
	if ((desc & MMU_DES_USED) == 0)
		phys_put_long(desc_addr, desc | MMU_DES_USED);

	/* fetch pointer table descriptor */
	i = (addr >> 16) & 0x1fc;
	desc_addr = (desc & MMU_ROOT_PTR_ADDR_MASK) | i;
	desc = phys_get_long(desc_addr);
	if ((desc & 2) == 0) {
		D(bug(_T("MMU: invalid ptr descriptor for %lx\n"), addr));
		return 0;
	}
	wp |= desc;
	if ((desc & MMU_DES_USED) == 0)
		phys_put_long(desc_addr, desc | MMU_DES_USED);

	/* fetch page table descriptor */
	if (regs.mmu_pagesize_8k) {
		i = (addr >> 11) & 0x7c;
		desc_addr = (desc & MMU_PTR_PAGE_ADDR_MASK_8) | i;
	} else {
		i = (addr >> 10) & 0xfc;
		desc_addr = (desc & MMU_PTR_PAGE_ADDR_MASK_4) | i;
	}

	desc = phys_get_long(desc_addr);
	if ((desc & 3) == 2) {
		/* indirect */
		desc_addr = desc & MMU_PAGE_INDIRECT_MASK;
		desc = phys_get_long(desc_addr);
	}
	if ((desc & 1) == 0) {
		D(bug(_T("MMU: invalid page descriptor log=%08lx desc=%08lx @%08lx\n"), addr, desc, desc_addr));
		return desc;
	}

	desc |= wp & MMU_DES_WP;
	if (write) {
		if (desc & MMU_DES_WP) {
			if ((desc & MMU_DES_USED) == 0) {
				desc |= MMU_DES_USED;
				phys_put_long(desc_addr, desc);
			}
		} else if ((desc & (MMU_DES_USED|MMU_DES_MODIFIED)) !=
				   (MMU_DES_USED|MMU_DES_MODIFIED)) {
			desc |= MMU_DES_USED|MMU_DES_MODIFIED;
			phys_put_long(desc_addr, desc);
		}
	} else {
		if ((desc & MMU_DES_USED) == 0) {
			desc |= MMU_DES_USED;
			phys_put_long(desc_addr, desc);
		}
	}
	return desc;
}

uae_u16 REGPARAM2 mmu_get_word_unaligned(uaecptr addr, bool data)
{
	uae_u16 res;

	res = (uae_u16)mmu_get_byte(addr, data, sz_word) << 8;
	SAVE_EXCEPTION;
	TRY(prb) {
		res |= mmu_get_byte(addr + 1, data, sz_word);
		RESTORE_EXCEPTION;
	}
	CATCH(prb) {
		RESTORE_EXCEPTION;
		regs.mmu_fault_addr = addr;
		regs.mmu_ssw |= MMU_SSW_MA;
		THROW_AGAIN(prb);
	}
	return res;
}

uae_u32 REGPARAM2 mmu_get_long_unaligned(uaecptr addr, bool data)
{
	uae_u32 res;

	if (likely(!(addr & 1))) {
		res = (uae_u32)mmu_get_word(addr, data, sz_long) << 16;
		SAVE_EXCEPTION;
		TRY(prb) {
			res |= mmu_get_word(addr + 2, data, sz_long);
			RESTORE_EXCEPTION;
		}
		CATCH(prb) {
			RESTORE_EXCEPTION;
			regs.mmu_fault_addr = addr;
			regs.mmu_ssw |= MMU_SSW_MA;
			THROW_AGAIN(prb);
		}
	} else {
		res = (uae_u32)mmu_get_byte(addr, data, sz_long) << 8;
		SAVE_EXCEPTION;
		TRY(prb) {
			res = (res | mmu_get_byte(addr + 1, data, sz_long)) << 8;
			res = (res | mmu_get_byte(addr + 2, data, sz_long)) << 8;
			res |= mmu_get_byte(addr + 3, data, sz_long);
			RESTORE_EXCEPTION;
		}
		CATCH(prb) {
			RESTORE_EXCEPTION;
			regs.mmu_fault_addr = addr;
			regs.mmu_ssw |= MMU_SSW_MA;
			THROW_AGAIN(prb);
		}
	}
	return res;
}

uae_u8 REGPARAM2 mmu_get_byte_slow(uaecptr addr, bool super, bool data,
						 int size, struct mmu_atc_line *cl)
{
	uae_u32 tag = ATC_TAG(addr);

	if (USETAG && cl->tag == (uae_u16)~tag) {
	redo:
		if (cl->hw)
			return HWget_b(cl->phys + addr);
		mmu_bus_error(addr, mmu_get_fc(super, data), 0, size);
		return 0;
	}

	if (!mmu_fill_atc_l1(addr, super, data, 0, cl))
		goto redo;

	return phys_get_byte(mmu_get_real_address(addr, cl));
}

uae_u16 REGPARAM2 mmu_get_word_slow(uaecptr addr, bool super, bool data,
						  int size, struct mmu_atc_line *cl)
{
	uae_u32 tag = ATC_TAG(addr);

	if (USETAG && cl->tag == (uae_u16)~tag) {
	redo:
		if (cl->hw)
			return HWget_w(cl->phys + addr);
		mmu_bus_error(addr, mmu_get_fc(super, data), 0, size);
		return 0;
	}

	if (!mmu_fill_atc_l1(addr, super, data, 0, cl))
		goto redo;

	return phys_get_word(mmu_get_real_address(addr, cl));
}

uae_u32 REGPARAM2 mmu_get_long_slow(uaecptr addr, bool super, bool data,
						  int size, struct mmu_atc_line *cl)
{
	uae_u32 tag = ATC_TAG(addr);

	if (USETAG && cl->tag == (uae_u16)~tag) {
	redo:
		if (cl->hw)
			return HWget_l(cl->phys + addr);
		mmu_bus_error(addr, mmu_get_fc(super, data), 0, size);
		return 0;
	}

	if (!mmu_fill_atc_l1(addr, super, data, 0, cl))
		goto redo;

	return phys_get_long(mmu_get_real_address(addr, cl));
}

void REGPARAM2 mmu_put_long_unaligned(uaecptr addr, uae_u32 val, bool data)
{
	SAVE_EXCEPTION;
	TRY(prb) {
		if (likely(!(addr & 1))) {
			mmu_put_word(addr, val >> 16, data, sz_long);
			mmu_put_word(addr + 2, val, data, sz_long);
		} else {
			mmu_put_byte(addr, val >> 24, data, sz_long);
			mmu_put_byte(addr + 1, val >> 16, data, sz_long);
			mmu_put_byte(addr + 2, val >> 8, data, sz_long);
			mmu_put_byte(addr + 3, val, data, sz_long);
		}
		RESTORE_EXCEPTION;
	}
	CATCH(prb) {
		RESTORE_EXCEPTION;
		regs.wb3_data = val;
		if (regs.mmu_fault_addr != addr) {
			regs.mmu_fault_addr = addr;
			regs.mmu_ssw |= MMU_SSW_MA;
		}
		THROW_AGAIN(prb);
	}
}

void REGPARAM2 mmu_put_word_unaligned(uaecptr addr, uae_u16 val, bool data)
{
	SAVE_EXCEPTION;
	TRY(prb) {
		mmu_put_byte(addr, val >> 8, data, sz_word);
		mmu_put_byte(addr + 1, val, data, sz_word);
		RESTORE_EXCEPTION;
	}
	CATCH(prb) {
		RESTORE_EXCEPTION;
		regs.wb3_data = val;
		if (regs.mmu_fault_addr != addr) {
			regs.mmu_fault_addr = addr;
			regs.mmu_ssw |= MMU_SSW_MA;
		}
		THROW_AGAIN(prb);
	}
}

void REGPARAM2 mmu_put_byte_slow(uaecptr addr, uae_u8 val, bool super, bool data,
								 int size, struct mmu_atc_line *cl)
{
	uae_u32 tag = ATC_TAG(addr);

	if (USETAG && cl->tag == (uae_u16)~tag) {
	redo:
		if (cl->hw) {
			HWput_b(cl->phys + addr, val);
			return;
		}
		regs.wb3_data = val;
		mmu_bus_error(addr, mmu_get_fc(super, data), 1, size);
		return;
	}

	if (!mmu_fill_atc_l1(addr, super, data, 1, cl))
		goto redo;

	phys_put_byte(mmu_get_real_address(addr, cl), val);
}

void REGPARAM2 mmu_put_word_slow(uaecptr addr, uae_u16 val, bool super, bool data,
								 int size, struct mmu_atc_line *cl)
{
	uae_u32 tag = ATC_TAG(addr);

	if (USETAG && cl->tag == (uae_u16)~tag) {
	redo:
		if (cl->hw) {
			HWput_w(cl->phys + addr, val);
			return;
		}
		regs.wb3_data = val;
		mmu_bus_error(addr, mmu_get_fc(super, data), 1, size);
		return;
	}

	if (!mmu_fill_atc_l1(addr, super, data, 1, cl))
		goto redo;

	phys_put_word(mmu_get_real_address(addr, cl), val);
}

void REGPARAM2 mmu_put_long_slow(uaecptr addr, uae_u32 val, bool super, bool data,
								 int size, struct mmu_atc_line *cl)
{
	uae_u32 tag = ATC_TAG(addr);

	if (USETAG && cl->tag == (uae_u16)~tag) {
	redo:
		if (cl->hw) {
			HWput_l(cl->phys + addr, val);
			return;
		}
		regs.wb3_data = val;
		mmu_bus_error(addr, mmu_get_fc(super, data), 1, size);
		return;
	}

	if (!mmu_fill_atc_l1(addr, super, data, 1, cl))
		goto redo;

	phys_put_long(mmu_get_real_address(addr, cl), val);
}

uae_u32 REGPARAM2 sfc_get_long(uaecptr addr)
{
	bool super = (regs.sfc & 4) != 0;
	bool data = (regs.sfc & 3) != 2;
	uae_u32 res;

	if (likely(!is_unaligned(addr, 4)))
		return mmu_get_user_long(addr, super, data, sz_long);

	if (likely(!(addr & 1))) {
		res = (uae_u32)mmu_get_user_word(addr, super, data, sz_long) << 16;
		SAVE_EXCEPTION;
		TRY(prb) {
			res |= mmu_get_user_word(addr + 2, super, data, sz_long);
			RESTORE_EXCEPTION;
		}
		CATCH(prb) {
			RESTORE_EXCEPTION;
			regs.mmu_fault_addr = addr;
			regs.mmu_ssw |= MMU_SSW_MA;
			THROW_AGAIN(prb);
		}
	} else {
		res = (uae_u32)mmu_get_user_byte(addr, super, data, sz_long) << 8;
		SAVE_EXCEPTION;
		TRY(prb) {
			res = (res | mmu_get_user_byte(addr + 1, super, data, sz_long)) << 8;
			res = (res | mmu_get_user_byte(addr + 2, super, data, sz_long)) << 8;
			res |= mmu_get_user_byte(addr + 3, super, data, sz_long);
			RESTORE_EXCEPTION;
		}
		CATCH(prb) {
			RESTORE_EXCEPTION;
			regs.mmu_fault_addr = addr;
			regs.mmu_ssw |= MMU_SSW_MA;
			THROW_AGAIN(prb);
		}
	}
	return res;
}

uae_u16 REGPARAM2 sfc_get_word(uaecptr addr)
{
	bool super = (regs.sfc & 4) != 0;
	bool data = (regs.sfc & 3) != 2;
	uae_u16 res;

	if (likely(!is_unaligned(addr, 2)))
		return mmu_get_user_word(addr, super, data, sz_word);

	res = (uae_u16)mmu_get_user_byte(addr, super, data, sz_word) << 8;
	SAVE_EXCEPTION;
	TRY(prb) {
		res |= mmu_get_user_byte(addr + 1, super, data, sz_word);
		RESTORE_EXCEPTION;
	}
	CATCH(prb) {
		RESTORE_EXCEPTION;
		regs.mmu_fault_addr = addr;
		regs.mmu_ssw |= MMU_SSW_MA;
		THROW_AGAIN(prb);
	}
	return res;
}

uae_u8 REGPARAM2 sfc_get_byte(uaecptr addr)
{
	bool super = (regs.sfc & 4) != 0;
	bool data = (regs.sfc & 3) != 2;

	return mmu_get_user_byte(addr, super, data, sz_byte);
}

void REGPARAM2 dfc_put_long(uaecptr addr, uae_u32 val)
{
	bool super = (regs.dfc & 4) != 0;
	bool data = (regs.dfc & 3) != 2;

	SAVE_EXCEPTION;
	TRY(prb) {
		if (likely(!is_unaligned(addr, 4)))
			mmu_put_user_long(addr, val, super, data, sz_long);
		else if (likely(!(addr & 1))) {
			mmu_put_user_word(addr, val >> 16, super, data, sz_long);
			mmu_put_user_word(addr + 2, val, super, data, sz_long);
		} else {
			mmu_put_user_byte(addr, val >> 24, super, data, sz_long);
			mmu_put_user_byte(addr + 1, val >> 16, super, data, sz_long);
			mmu_put_user_byte(addr + 2, val >> 8, super, data, sz_long);
			mmu_put_user_byte(addr + 3, val, super, data, sz_long);
		}
		RESTORE_EXCEPTION;
	}
	CATCH(prb) {
		RESTORE_EXCEPTION;
		regs.wb3_data = val;
		if (regs.mmu_fault_addr != addr) {
			regs.mmu_fault_addr = addr;
			regs.mmu_ssw |= MMU_SSW_MA;
		}
		THROW_AGAIN(prb);
	}
}

void REGPARAM2 dfc_put_word(uaecptr addr, uae_u16 val)
{
	bool super = (regs.dfc & 4) != 0;
	bool data = (regs.dfc & 3) != 2;

	SAVE_EXCEPTION;
	TRY(prb) {
		if (likely(!is_unaligned(addr, 2)))
			mmu_put_user_word(addr, val, super, data, sz_word);
		else {
			mmu_put_user_byte(addr, val >> 8, super, data, sz_word);
			mmu_put_user_byte(addr + 1, val, super, data, sz_word);
		}
		RESTORE_EXCEPTION;
	}
	CATCH(prb) {
		RESTORE_EXCEPTION;
		regs.wb3_data = val;
		if (regs.mmu_fault_addr != addr) {
			regs.mmu_fault_addr = addr;
			regs.mmu_ssw |= MMU_SSW_MA;
		}
		THROW_AGAIN(prb);
	}
}

void REGPARAM2 dfc_put_byte(uaecptr addr, uae_u8 val)
{
	bool super = (regs.dfc & 4) != 0;
	bool data = (regs.dfc & 3) != 2;

	SAVE_EXCEPTION;
	TRY(prb) {
		mmu_put_user_byte(addr, val, super, data, sz_byte);
		RESTORE_EXCEPTION;
	}
	CATCH(prb) {
		RESTORE_EXCEPTION;
		regs.wb3_data = val;
		THROW_AGAIN(prb);
	}
}

void REGPARAM2 mmu_op_real(uae_u32 opcode, uae_u16 extra)
{
	bool super = (regs.dfc & 4) != 0;
	DUNUSED(extra);
	if ((opcode & 0xFE0) == 0x0500) {
		bool glob;
		int regno;
		//D(didflush = 0);
		uae_u32 addr;
		/* PFLUSH */
		regno = opcode & 7;
		glob = (opcode & 8) != 0;

		if (opcode & 16) {
			D(bug(_T("pflusha(%u,%u)\n"), glob, regs.dfc));
			mmu_flush_atc_all(glob);
		} else {
			addr = m68k_areg(regs, regno);
			D(bug(_T("pflush(%u,%u,%x)\n"), glob, regs.dfc, addr));
			mmu_flush_atc(addr, super, glob);
		}
		flush_internals();
#ifdef USE_JIT
		flush_icache(0);
#endif
	} else if ((opcode & 0x0FD8) == 0x548) {
		bool write;
		int regno;
		uae_u32 addr;

		regno = opcode & 7;
		write = (opcode & 32) == 0;
		addr = m68k_areg(regs, regno);
		D(bug(_T("PTEST%c (A%d) %08x DFC=%d\n"), write ? 'W' : 'R', regno, addr, regs.dfc));
		mmu_flush_atc(addr, super, true);
		SAVE_EXCEPTION;
		TRY(prb) {
			struct mmu_atc_line *l;
			uae_u32 desc;
			bool data = (regs.dfc & 3) != 2;

			l = &atc_l2[super ? 1 : 0][ATC_L2_INDEX(addr)];
			desc = mmu_fill_atc_l2(addr, super, data, write, l);
			if (!(data ? l->valid_data : l->valid_inst))
				regs.mmusr = MMU_MMUSR_B;
			else if (l->tt)
				regs.mmusr = MMU_MMUSR_T | MMU_MMUSR_R;
			else {
				regs.mmusr = desc & (~0xfff|MMU_MMUSR_G|MMU_MMUSR_Ux|MMU_MMUSR_S|
									 MMU_MMUSR_CM|MMU_MMUSR_M|MMU_MMUSR_W);
				regs.mmusr |= MMU_MMUSR_R;
			}
		}
		CATCH(prb) {
			regs.mmusr = MMU_MMUSR_B;
		}
		RESTORE_EXCEPTION;
		D(bug(_T("PTEST result: mmusr %08x\n"), regs.mmusr));
	} else
		op_illg (opcode);
}

static void REGPARAM2 mmu_flush_atc(uaecptr addr, bool super, bool global)
{
	struct mmu_atc_line *l;
	int i, j;

	l = atc_l1[super ? 1 : 0][0][0];
	i = ATC_L1_INDEX(addr);
	for (j = 0; j < 4; j++) {
		if (global || !l[i].global)
			l[i].tag = 0x8000;
		l += ATC_L1_SIZE;
	}
	if (regs.mmu_pagesize_8k) {
		i = ATC_L1_INDEX(addr) ^ 1;
		for (j = 0; j < 4; j++) {
			if (global || !l[i].global)
				l[i].tag = 0x8000;
			l += ATC_L1_SIZE;
		}
	}
	l = atc_l2[super ? 1 : 0];
	i = ATC_L2_INDEX(addr);
	if (global || !l[i].global)
		l[i].tag = 0x8000;
	if (regs.mmu_pagesize_8k) {
		i ^= 1;
		if (global || !l[i].global)
			l[i].tag = 0x8000;
	}
}

static void REGPARAM2 mmu_flush_atc_all(bool global)
{
	struct mmu_atc_line *l;
	unsigned int i;

	l = atc_l1[0][0][0];
	for (i = 0; i < sizeof(atc_l1) / sizeof(*l); l++, i++) {
		if (global || !l->global)
			l->tag = 0x8000;
	}

	l = atc_l2[0];
	for (i = 0; i < sizeof(atc_l2) / sizeof(*l); l++, i++) {
		if (global || !l->global)
			l->tag = 0x8000;
	}
}

void REGPARAM2 mmu_reset(void)
{
	mmu_flush_atc_all(true);
#if 0
	regs.urp = regs.srp = 0;
	regs.itt0 = regs.itt1 = 0;
	regs.dtt0 = regs.dtt1 = 0;
	regs.mmusr = 0;
#endif
}


void REGPARAM2 mmu_set_tc(uae_u16 tc)
{
#if 0
	if (regs.tcr == tc)
		return;
	regs.tcr = tc;
#endif
	regs.mmu_enabled = tc & 0x8000 ? 1 : 0;
	regs.mmu_pagesize_8k = tc & 0x4000 ? 1 : 0;
	mmu_flush_atc_all(true);

	write_log(_T("MMU: enabled=%d page8k=%d\n"), regs.mmu_enabled, regs.mmu_pagesize_8k);
}

void REGPARAM2 mmu_set_super(bool super)
{
	current_atc = &atc_l1[super ? 1 : 0];
}

#else

void mmu_op(uae_u32 opcode, uae_u16 /*extra*/)
{
	if ((opcode & 0xFE0) == 0x0500) {
		/* PFLUSH instruction */
		flush_internals();
	} else if ((opcode & 0x0FD8) == 0x548) {
		/* PTEST instruction */
	} else
		op_illg(opcode);
}

#endif

/*
vim:ts=4:sw=4:
*/
