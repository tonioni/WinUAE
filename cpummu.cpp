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


#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "cpummu.h"

#define DBG_MMU_VERBOSE	1
#define DBG_MMU_SANITY	1
#if 0
#define write_log printf
#endif

#ifdef FULLMMU


uae_u32 mmu_is_super;
uae_u32 mmu_tagmask, mmu_pagemask;
struct mmu_atc_line mmu_atc_array[ATC_TYPE][ATC_WAYS][ATC_SLOTS];
int mmu060_state;
bool mmu_pagesize_8k;

static void mmu_dump_ttr(const TCHAR * label, uae_u32 ttr)
{
	DUNUSED(label);
	uae_u32 from_addr, to_addr;

	from_addr = ttr & MMU_TTR_LOGICAL_BASE;
	to_addr = (ttr & MMU_TTR_LOGICAL_MASK) << 8;

	
#if MMUDEBUG > 0
	write_log(_T("%s: [%08lx] %08lx - %08lx enabled=%d supervisor=%d wp=%d cm=%02d\n"),
			label, ttr,
			from_addr, to_addr,
			ttr & MMU_TTR_BIT_ENABLED ? 1 : 0,
			(ttr & (MMU_TTR_BIT_SFIELD_ENABLED | MMU_TTR_BIT_SFIELD_SUPER)) >> MMU_TTR_SFIELD_SHIFT,
			ttr & MMU_TTR_BIT_WRITE_PROTECT ? 1 : 0,
			(ttr & MMU_TTR_CACHE_MASK) >> MMU_TTR_CACHE_SHIFT
		  );
#endif
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

#if MMUDEBUG > 0
	write_log(_T("MMU: map transparent mapping of %08x\n"), *ttr);
#endif
}



#if 0
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

	write_log(_T("%s: root=%lx\n", label, root_ptr);

	for (root_idx = 0; root_idx < ROOT_TABLE_SIZE; root_idx++) {
		root_des = phys_get_long(root_ptr + root_idx);

		if ((root_des & 2) == 0)
			continue;	/* invalid */

		write_log(_T("ROOT: %03d U=%d W=%d UDT=%02d\n", root_idx,
				root_des & 8 ? 1 : 0,
				root_des & 4 ? 1 : 0,
				root_des & 3
			  );

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

			page_addr = ptr_des & (mmu_pagesize_8k ? MMU_PTR_PAGE_ADDR_MASK_8 : MMU_PTR_PAGE_ADDR_MASK_4);

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

			write_log(_T(" PTR: %03d U=%d W=%d UDT=%02d\n", ptr_idx,
				ptr_des & 8 ? 1 : 0,
				ptr_des & 4 ? 1 : 0,
				ptr_des & 3
			  );


			for (page_idx = 0; page_idx <= n_pages_used; page_idx++) {
				page_des = page_info[page_idx].match;

				if ((page_des & MMU_PDT_MASK) == 2) {
					write_log(_T("  PAGE: %03d-%03d log=%08lx INDIRECT --> addr=%08lx\n",
							page_info[page_idx].start_idx,
							page_info[page_idx].start_idx + page_info[page_idx].n_pages - 1,
							page_info[page_idx].log,
							page_des & MMU_PAGE_INDIRECT_MASK
						  );

				} else {
					write_log(_T("  PAGE: %03d-%03d log=%08lx addr=%08lx UR=%02d G=%d U1/0=%d S=%d CM=%d M=%d U=%d W=%d\n",
							page_info[page_idx].start_idx,
							page_info[page_idx].start_idx + page_info[page_idx].n_pages - 1,
							page_info[page_idx].log,
							page_des & (mmu_pagesize_8k ? MMU_PAGE_ADDR_MASK_8 : MMU_PAGE_ADDR_MASK_4),
							(page_des & (mmu_pagesize_8k ? MMU_PAGE_UR_MASK_8 : MMU_PAGE_UR_MASK_4)) >> MMU_PAGE_UR_SHIFT,
							page_des & MMU_DES_GLOBAL ? 1 : 0,
							(page_des & MMU_TTR_UX_MASK) >> MMU_TTR_UX_SHIFT,
							page_des & MMU_DES_SUPER ? 1 : 0,
							(page_des & MMU_TTR_CACHE_MASK) >> MMU_TTR_CACHE_SHIFT,
							page_des & MMU_DES_MODIFIED ? 1 : 0,
							page_des & MMU_DES_USED ? 1 : 0,
							page_des & MMU_DES_WP ? 1 : 0
						  );
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

}
/* }}} */

/* {{{ mmu_dump_tables */
void mmu_dump_tables(void)
{
	write_log(_T("URP: %08x   SRP: %08x  MMUSR: %x  TC: %x\n"), regs.urp, regs.srp, regs.mmusr, regs.tcr);
	mmu_dump_ttr(L"DTT0", regs.dtt0);
	mmu_dump_ttr(L"DTT1", regs.dtt1);
	mmu_dump_ttr(L"ITT0", regs.itt0);
	mmu_dump_ttr(L"ITT1", regs.itt1);
	mmu_dump_atc();
#if MMUDEBUG
	// mmu_dump_table("SRP", regs.srp);
#endif
}
/* }}} */

static uaecptr REGPARAM2 mmu_lookup_pagetable(uaecptr addr, bool super, bool write, uae_u32 *status);

static ALWAYS_INLINE int mmu_get_fc(bool super, bool data)
{
	return (super ? 4 : 0) | (data ? 1 : 2);
}

static void mmu_bus_error(uaecptr addr, int fc, bool write, int size, uae_u32 status)
{
	if (currprefs.mmu_model == 68040) {
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

		regs.mmu_ssw = ssw | MMU_SSW_ATC;

#if MMUDEBUG > 2
		write_log(_T("040 BUS ERROR: fc=%d w=%d logical=%08x ssw=%04x PC=%08x\n"), fc, write, addr, ssw, m68k_getpc());
#endif
	} else {
		uae_u32 fslw = 0;

		fslw |= write ? MMU_FSLW_W : MMU_FSLW_R;
		switch (size) {
		case sz_byte:
			fslw |= MMU_FSLW_SIZE_B;
			break;
		case sz_word:
			fslw |= MMU_FSLW_SIZE_W;
			break;
		case sz_long:
			fslw |= MMU_FSLW_SIZE_L;
			break;
		case 16: // MOVE16
			addr &= ~15;
			fslw |= MMU_FSLW_SIZE_D;
			break;
		}
		if (mmu060_state == 0) {
			fslw |= MMU_FSLW_IO; // opword fetch
		} else if ((fc & 2)) {
			fslw |= MMU_FSLW_IO | MMU_FSLW_MA; // extension word
		}
		fslw |= status;
		regs.mmu_fslw = fslw;

#if MMUDEBUG > 2
		write_log(_T("060 BUS ERROR: fc=%d w=%d logical=%08x ssw=%08x PC=%08x\n"), fc, write, addr, fslw, m68k_getpc());
#endif

	}

	regs.mmu_fault_addr = addr;

	THROW(2);
}

void mmu_bus_error_ttr_write_fault(uaecptr addr, bool super, bool data, uae_u32 val, int size)
{
	uae_u32 status = 0;

	if (currprefs.mmu_model == 68060) {
		status |= MMU_FSLW_TTR;
	}
	regs.wb3_data = val;
	mmu_bus_error(addr, mmu_get_fc (super, data), true, size, status);
}


/*
 * Update the atc line for a given address by doing a mmu lookup.
 */
static uaecptr mmu_fill_atc(uaecptr addr, bool super, bool data, bool write, struct mmu_atc_line *l, uae_u32 *status)
{
	uae_u32 desc;

	*status = 0;
	SAVE_EXCEPTION;
	TRY(prb) {
		desc = mmu_lookup_pagetable(addr, super, write, status);
#if MMUDEBUG > 2
		write_log(_T("translate: %x,%u,%u,%u -> %x\n"), addr, super, write, data, desc);
#endif
		RESTORE_EXCEPTION;
	}
	CATCH(prb) {
		RESTORE_EXCEPTION;
		/* bus error during table search */
		desc = 0;
		*status = MMU_FSLW_TWE;
		// goto fail;
	} ENDTRY
	if ((desc & 1) && (!super && desc & MMU_MMUSR_S)) {
		*status |= MMU_FSLW_SP;
		l->valid = 0;
		l->global = 0;
	} else if ((desc & 1) == 0) {
		l->valid = 0;
		l->global = 0;
	} else {
		l->valid = 1;
		if (mmu_pagesize_8k)
			l->phys = (desc & ~0x1fff);
		else
			l->phys = (desc & ~0xfff);
		l->global = (desc & MMU_MMUSR_G) != 0;
		l->modified = (desc & MMU_MMUSR_M) != 0;
		l->write_protect = (desc & MMU_MMUSR_W) != 0;
	}

	return desc;
}

static ALWAYS_INLINE bool mmu_fill_atc_try(uaecptr addr, bool super, bool data, bool write, struct mmu_atc_line *l1, uae_u32 *status)
{
	mmu_fill_atc(addr,super,data,write,l1, status);
	if (!(l1->valid)) {
#if MMUDEBUG > 1
		write_log(_T("MMU: non-resident page (%x,%x)!\n"), addr, regs.pc);
#endif
		goto fail;
	}
	if (write) {
		if (l1->write_protect) {
			*status |= MMU_FSLW_WP;
#if MMUDEBUG > 1
			write_log(_T("MMU: write protected %lx by atc \n"), addr);
#endif
			mmu_dump_atc();
			goto fail;
		}

	}
	return true;

fail:
	return false;
}

uaecptr REGPARAM2 mmu_translate(uaecptr addr, bool super, bool data, bool write)
{
	struct mmu_atc_line *l;
	uae_u32 status;

	// this should return a miss but choose a valid line
	mmu_user_lookup(addr, super, data, write, &l);

	mmu_fill_atc(addr, super, data, write, l, &status);
	if (!l->valid) {
#if MMUDEBUG > 0
		write_log(_T("[MMU] mmu_translate error"));
#endif
		THROW(2);
	}

    return l->phys | (addr & (mmu_pagesize_8k?0x00001fff:0x00000fff));

}

/*
 * Lookup the address by walking the page table and updating
 * the page descriptors accordingly. Returns the found descriptor
 * or produces a bus error.
 */
static uaecptr REGPARAM2 mmu_lookup_pagetable(uaecptr addr, bool super, bool write, uae_u32 *status)
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
#if MMUDEBUG > 1
		write_log(_T("MMU: invalid root descriptor %s for %x desc at %x desc=%x %s at %d\n"), super ? _T("srp"):_T("urp"),
				addr,desc_addr,desc,__FILE__,__LINE__);
#endif
		*status |= MMU_FSLW_PTA;
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
#if MMUDEBUG > 1
		write_log(_T("MMU: invalid ptr descriptor %s for %x desc at %x desc=%x %s at %d\n"), super ? _T("srp"):_T("urp"), 
				addr,desc_addr,desc,__FILE__,__LINE__);
#endif
		*status |= MMU_FSLW_PTB;
		return 0;
	}
	wp |= desc;
	if ((desc & MMU_DES_USED) == 0)
		phys_put_long(desc_addr, desc | MMU_DES_USED);

	/* fetch page table descriptor */
	if (mmu_pagesize_8k) {
		i = (addr >> 11) & 0x7c;
		desc_addr = (desc & MMU_PTR_PAGE_ADDR_MASK_8) + i;
	} else {
		i = (addr >> 10) & 0xfc;
		desc_addr = (desc & MMU_PTR_PAGE_ADDR_MASK_4) + i;
	}

	desc = phys_get_long(desc_addr);
	if ((desc & 3) == 2) {
		/* indirect */
		desc_addr = desc & MMU_PAGE_INDIRECT_MASK;
		desc = phys_get_long(desc_addr);
	}
	if ((desc & 1) == 0) {
#if MMUDEBUG > 1
		write_log(_T("MMU: invalid page descriptor log=%0lx desc=%08x @%08x %s at %d\n"), addr, desc, desc_addr,__FILE__,__LINE__);
#endif
		if ((desc & 3) == 2)
			*status |= MMU_FSLW_IL;
		else
			*status |= MMU_FSLW_PF;
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
		regs.mmu_fslw |= MMU_FSLW_MA;
		regs.mmu_ssw |= MMU_SSW_MA;
		THROW_AGAIN(prb);
	} ENDTRY
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
		} ENDTRY
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
			regs.mmu_fslw |= MMU_FSLW_MA;
			regs.mmu_ssw |= MMU_SSW_MA;
			THROW_AGAIN(prb);
		} ENDTRY
	}
	return res;
}

uae_u8 REGPARAM2 mmu_get_byte_slow(uaecptr addr, bool super, bool data,
						 int size, struct mmu_atc_line *cl)
{
	uae_u32 status;
	if (!mmu_fill_atc_try(addr, super, data, 0, cl, &status)) {
		mmu_bus_error(addr, mmu_get_fc(super, data), 0, size, status);
		return 0;
	}
	return phys_get_byte(mmu_get_real_address(addr, cl));
}

uae_u16 REGPARAM2 mmu_get_word_slow(uaecptr addr, bool super, bool data,
						  int size, struct mmu_atc_line *cl)
{
	uae_u32 status;
	if (!mmu_fill_atc_try(addr, super, data, 0, cl, &status)) {
		mmu_bus_error(addr, mmu_get_fc(super, data), 0, size, status);
		return 0;
	}
	return phys_get_word(mmu_get_real_address(addr, cl));
}

uae_u32 REGPARAM2 mmu_get_long_slow(uaecptr addr, bool super, bool data,
						  int size, struct mmu_atc_line *cl)
{
	uae_u32 status;
	if (!mmu_fill_atc_try(addr, super, data, 0, cl, &status)) {
		mmu_bus_error(addr, mmu_get_fc(super, data), 0, size, status);
		return 0;
	}
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
			regs.mmu_fslw |= MMU_FSLW_MA;
			regs.mmu_ssw |= MMU_SSW_MA;
		}
		THROW_AGAIN(prb);
	} ENDTRY
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
			regs.mmu_fslw |= MMU_FSLW_MA;
			regs.mmu_ssw |= MMU_SSW_MA;
		}
		THROW_AGAIN(prb);
	} ENDTRY
}

void REGPARAM2 mmu_put_byte_slow(uaecptr addr, uae_u8 val, bool super, bool data,
								 int size, struct mmu_atc_line *cl)
{
	uae_u32 status;
	if (!mmu_fill_atc_try(addr, super, data, 1, cl, &status)) {
		regs.wb3_data = val;
		mmu_bus_error(addr, mmu_get_fc(super, data), 1, size, status);
		return;
	}
	phys_put_byte(mmu_get_real_address(addr, cl), val);
}

void REGPARAM2 mmu_put_word_slow(uaecptr addr, uae_u16 val, bool super, bool data,
								 int size, struct mmu_atc_line *cl)
{
	uae_u32 status;
	if (!mmu_fill_atc_try(addr, super, data, 1, cl, &status)) {
		regs.wb3_data = val;
		mmu_bus_error(addr, mmu_get_fc(super, data), 1, size, status);
		return;
	}
	phys_put_word(mmu_get_real_address(addr, cl), val);
}

void REGPARAM2 mmu_put_long_slow(uaecptr addr, uae_u32 val, bool super, bool data,
								 int size, struct mmu_atc_line *cl)
{
	uae_u32 status;
	if (!mmu_fill_atc_try(addr, super, data, 1, cl, &status)) {
		regs.wb3_data = val;
		mmu_bus_error(addr, mmu_get_fc(super, data), 1, size, status);
		return;
	}
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
			regs.mmu_fslw |= MMU_FSLW_MA;
			regs.mmu_ssw |= MMU_SSW_MA;
			THROW_AGAIN(prb);
		} ENDTRY
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
			regs.mmu_fslw |= MMU_FSLW_MA;
			regs.mmu_ssw |= MMU_SSW_MA;
			THROW_AGAIN(prb);
		} ENDTRY
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
		regs.mmu_fslw |= MMU_FSLW_MA;
		regs.mmu_ssw |= MMU_SSW_MA;
		THROW_AGAIN(prb);
	} ENDTRY
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
			regs.mmu_fslw |= MMU_FSLW_MA;
			regs.mmu_ssw |= MMU_SSW_MA;
		}
		THROW_AGAIN(prb);
	} ENDTRY
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
			regs.mmu_fslw |= MMU_FSLW_MA;
			regs.mmu_ssw |= MMU_SSW_MA;
		}
		THROW_AGAIN(prb);
	} ENDTRY
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
	} ENDTRY
}

void REGPARAM2 mmu_op_real(uae_u32 opcode, uae_u16 extra)
{
	bool super = (regs.dfc & 4) != 0;
	DUNUSED(extra);
	if ((opcode & 0xFE0) == 0x0500) { // PFLUSH
		bool glob;
		int regno;
		//D(didflush = 0);
		uae_u32 addr;
		/* PFLUSH */
		regno = opcode & 7;
		glob = (opcode & 8) != 0;

		if (opcode & 16) {
#if MMUDEBUG > 1
			write_log(_T("pflusha(%u,%u)\n"), glob, regs.dfc);
#endif
			mmu_flush_atc_all(glob);
		} else {
			addr = m68k_areg(regs, regno);
#if MMUDEBUG > 1
			write_log(_T("pflush(%u,%u,%x)\n"), glob, regs.dfc, addr);
#endif
			mmu_flush_atc(addr, super, glob);
		}
		flush_internals();
#ifdef USE_JIT
		flush_icache(0);
#endif
	} else if ((opcode & 0x0FD8) == 0x0548) { // PTEST (68040)
		bool write;
		int regno;
		uae_u32 addr;

		regno = opcode & 7;
		write = (opcode & 32) == 0;
		addr = m68k_areg(regs, regno);
#if MMUDEBUG > 1
		write_log(_T("PTEST%c (A%d) %08x DFC=%d\n"), write ? 'W' : 'R', regno, addr, regs.dfc);
#endif
		mmu_flush_atc(addr, super, true);
		SAVE_EXCEPTION;
		TRY(prb) {
			struct mmu_atc_line *l;
			uae_u32 desc;
			bool data = (regs.dfc & 3) != 2;

			if (mmu_match_ttr(addr,super,data)!=TTR_NO_MATCH) 
				regs.mmusr = MMU_MMUSR_T | MMU_MMUSR_R;
			else {
				uae_u32 status;
				mmu_user_lookup(addr, super, data, write, &l);
				desc = mmu_fill_atc(addr, super, data, write, l, &status);
				if (!(l->valid))
					regs.mmusr = MMU_MMUSR_B;
				else {
					regs.mmusr = desc & (~0xfff|MMU_MMUSR_G|MMU_MMUSR_Ux|MMU_MMUSR_S|
										 MMU_MMUSR_CM|MMU_MMUSR_M|MMU_MMUSR_W);
					regs.mmusr |= MMU_MMUSR_R;
				}
			}
		}
		CATCH(prb) {
			regs.mmusr = MMU_MMUSR_B;
		} ENDTRY
		RESTORE_EXCEPTION;
#if MMUDEBUG > 1
		write_log(_T("PTEST result: mmusr %08x\n"), regs.mmusr);
#endif
	} else if ((opcode & 0xFFB8) == 0xF588) { // PLPA (68060)
		int write = (opcode & 0x40) == 0;
		int regno = opcode & 7;
		uae_u32 addr = m68k_areg (regs, regno);
		bool data = (regs.dfc & 3) != 2;

#if MMUDEBUG > 1
		write_log(_T("PLPA%c param: %08x\n"), write ? 'W' : 'R', addr);
#endif
		if (mmu_match_ttr(addr,super,data)==TTR_NO_MATCH) {
			m68k_areg (regs, regno) = mmu_translate (addr, super, data, write != 0);
		}
#if MMUDEBUG > 1
		write_log(_T("PLPA%c result: %08x\n"), write ? 'W' : 'R', m68k_areg (regs, regno));
#endif
	} else {
		op_illg (opcode);
	}
}

// fixme : global parameter?
void REGPARAM2 mmu_flush_atc(uaecptr addr, bool super, bool global)
{
	int way,type,index;

	uaecptr tag = (mmu_is_super | (addr >> 1)) & mmu_tagmask;
	if (mmu_pagesize_8k)
		index=(addr & 0x0001E000)>>13;
	else
		index=(addr & 0x0000F000)>>12;
	for (type=0;type<ATC_TYPE;type++)
	for (way=0;way<ATC_WAYS;way++) {
		if (!global && mmu_atc_array[type][way][index].global)
			continue;
		// if we have this 
		if ((tag == mmu_atc_array[type][way][index].tag) && (mmu_atc_array[type][way][index].valid)) {
			mmu_atc_array[type][way][index].valid=false;
		}
	}	
}

void REGPARAM2 mmu_flush_atc_all(bool global)
{
	unsigned int way,slot,type;
	for (type=0;type<ATC_TYPE;type++) 
	for (way=0;way<ATC_WAYS;way++) 
	for (slot=0;slot<ATC_SLOTS;slot++) {
		if (!global && mmu_atc_array[type][way][slot].global)
			continue;
		mmu_atc_array[type][way][slot].valid=false;
	}
}

void REGPARAM2 mmu_reset(void)
{
	mmu_flush_atc_all(true);
}


void REGPARAM2 mmu_set_tc(uae_u16 tc)
{
	regs.mmu_enabled = (tc & 0x8000) != 0;
	mmu_pagesize_8k = (tc & 0x4000) != 0;
	mmu_tagmask = mmu_pagesize_8k ? 0xFFFF0000 : 0xFFFF8000;
	mmu_pagemask = mmu_pagesize_8k ? 0x00001FFF : 0x00000FFF;
	regs.mmu_page_size = mmu_pagesize_8k ? 8192 : 4096;

	mmu_flush_atc_all(true);

	write_log(_T("%d MMU: enabled=%d page8k=%d\n"), currprefs.mmu_model, regs.mmu_enabled, mmu_pagesize_8k);
}

void REGPARAM2 mmu_set_super(bool super)
{
	mmu_is_super = super ? 0x80000000 : 0;
}

void m68k_do_rte_mmu040 (uaecptr a7)
{
	uae_u16 ssr = get_word_mmu040 (a7 + 8 + 4);
	if (ssr & MMU_SSW_CT) {
		uaecptr src_a7 = a7 + 8 - 8;
		uaecptr dst_a7 = a7 + 8 + 52;
		put_word_mmu040 (dst_a7 + 0, get_word_mmu040 (src_a7 + 0));
		put_long_mmu040 (dst_a7 + 2, get_long_mmu040 (src_a7 + 2));
		// skip this word
		put_long_mmu040 (dst_a7 + 8, get_long_mmu040 (src_a7 + 8));
	}
}

void flush_mmu040 (uaecptr addr, int n)
{
}
void m68k_do_rts_mmu040 (void)
{
	m68k_setpc (get_long_mmu040 (m68k_areg (regs, 7)));
	m68k_areg (regs, 7) += 4;
}
void m68k_do_bsr_mmu040 (uaecptr oldpc, uae_s32 offset)
{
	put_long_mmu040 (m68k_areg (regs, 7) - 4, oldpc);
	m68k_areg (regs, 7) -= 4;
	m68k_incpci (offset);
}

void flush_mmu060 (uaecptr addr, int n)
{
}
void m68k_do_rts_mmu060 (void)
{
	m68k_setpc (get_long_mmu060 (m68k_areg (regs, 7)));
	m68k_areg (regs, 7) += 4;
}
void m68k_do_bsr_mmu060 (uaecptr oldpc, uae_s32 offset)
{
	put_long_mmu060 (m68k_areg (regs, 7) - 4, oldpc);
	m68k_areg (regs, 7) -= 4;
	m68k_incpci (offset);
}

#ifndef __cplusplus
jmp_buf __exbuf;
int     __exvalue;
#define MAX_TRY_STACK 256
static int s_try_stack_size=0;
static jmp_buf s_try_stack[MAX_TRY_STACK];
jmp_buf* __poptry(void) {
	if (s_try_stack_size>0) {
        s_try_stack_size--;
        if (s_try_stack_size == 0)
            return NULL;
        memcpy(&__exbuf,&s_try_stack[s_try_stack_size-1],sizeof(jmp_buf));
        // fprintf(stderr,"pop jmpbuf=%08x\n",s_try_stack[s_try_stack_size][0]);
        return &s_try_stack[s_try_stack_size-1];
    }
	else {
		fprintf(stderr,"try stack underflow...\n");
	    // return (NULL);
		abort();
	}
}
void __pushtry(jmp_buf* j) {
	if (s_try_stack_size<MAX_TRY_STACK) {
		// fprintf(stderr,"push jmpbuf=%08x\n",(*j)[0]);
		memcpy(&s_try_stack[s_try_stack_size],j,sizeof(jmp_buf));
		s_try_stack_size++;
	} else {
		fprintf(stderr,"try stack overflow...\n");
		abort();
	}
}
int __is_catched(void) {return (s_try_stack_size>0); }
#endif

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
