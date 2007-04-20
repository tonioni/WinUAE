/*
 * cpummu.h - MMU emulation
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

#ifndef CPUMMU_H
#define CPUMMU_H

#define SAVE_EXCEPTION
#define RESTORE_EXCEPTION
#define TRY(x)
#define CATCH(x)
#define THROW(x)
#define THROW_AGAIN(x)
#define likely(x) x

#define MMU_TEST_PTEST					1
#define MMU_TEST_VERBOSE				2
#define MMU_TEST_FORCE_TABLE_SEARCH		4
#define MMU_TEST_NO_BUSERR				8

extern void mmu_dump_tables(void);

#define MMU_TTR_LOGICAL_BASE				0xff000000
#define MMU_TTR_LOGICAL_MASK				0x00ff0000
#define MMU_TTR_BIT_ENABLED					(1 << 15)
#define MMU_TTR_BIT_SFIELD_ENABLED			(1 << 14)
#define MMU_TTR_BIT_SFIELD_SUPER			(1 << 13)
#define MMU_TTR_SFIELD_SHIFT				13
#define MMU_TTR_UX_MASK						((1 << 9) | (1 << 8))
#define MMU_TTR_UX_SHIFT					8
#define MMU_TTR_CACHE_MASK					((1 << 6) | (1 << 5))
#define MMU_TTR_CACHE_SHIFT					5
#define MMU_TTR_BIT_WRITE_PROTECT			(1 << 2)

#define MMU_UDT_MASK	3
#define MMU_PDT_MASK	3

#define MMU_DES_WP			4
#define MMU_DES_USED		8

/* page descriptors only */
#define MMU_DES_MODIFIED	16
#define MMU_DES_SUPER		(1 << 7)
#define MMU_DES_GLOBAL		(1 << 10)

#define MMU_ROOT_PTR_ADDR_MASK			0xfffffe00
#define MMU_PTR_PAGE_ADDR_MASK_8		0xffffff80
#define MMU_PTR_PAGE_ADDR_MASK_4		0xffffff00

#define MMU_PAGE_INDIRECT_MASK			0xfffffffc
#define MMU_PAGE_ADDR_MASK_8			0xffffe000
#define MMU_PAGE_ADDR_MASK_4			0xfffff000
#define MMU_PAGE_UR_MASK_8				((1 << 12) | (1 << 11))
#define MMU_PAGE_UR_MASK_4				(1 << 11)
#define MMU_PAGE_UR_SHIFT				11

#define MMU_MMUSR_ADDR_MASK				0xfffff000
#define MMU_MMUSR_B						(1 << 11)
#define MMU_MMUSR_G						(1 << 10)
#define MMU_MMUSR_U1					(1 << 9)
#define MMU_MMUSR_U0					(1 << 8)
#define MMU_MMUSR_Ux					(MMU_MMUSR_U1 | MMU_MMUSR_U0)
#define MMU_MMUSR_S						(1 << 7)
#define MMU_MMUSR_CM					((1 << 6) | ( 1 << 5))
#define MMU_MMUSR_M						(1 << 4)
#define MMU_MMUSR_W						(1 << 2)
#define MMU_MMUSR_T						(1 << 1)
#define MMU_MMUSR_R						(1 << 0)

/* special status word (access error stack frame) */
#define MMU_SSW_TM		0x0007
#define MMU_SSW_TT		0x0018
#define MMU_SSW_SIZE	0x0060
#define  MMU_SSW_SIZE_B	0x0020
#define  MMU_SSW_SIZE_W	0x0040
#define  MMU_SSW_SIZE_L	0x0000
#define MMU_SSW_RW		0x0100
#define MMU_SSW_LK		0x0200
#define MMU_SSW_ATC		0x0400
#define MMU_SSW_MA		0x0800

#define TTR_I0	4
#define TTR_I1	5
#define TTR_D0	6
#define TTR_D1	7

#define TTR_NO_MATCH	0
#define TTR_NO_WRITE	1
#define TTR_OK_MATCH	2

struct mmu_atc_line {
    uae_u16 tag;
    unsigned tt : 1;
    unsigned valid_data : 1;
    unsigned valid_inst : 1;
    unsigned global : 1;
    unsigned modified : 1;
    unsigned write_protect : 1;
    unsigned hw : 1;
    unsigned bus_fault : 1;
    uaecptr phys;
};

/*
 * We don't need to store the whole logical address in the atc cache, as part of
 * it is encoded as index into the cache. 14 bits of the address are stored in
 * the tag, this means at least 6 bits must go into the index. The upper two
 * bits of the tag define the type of data in the atc line:
 * - 00: a normal memory address
 * - 11: invalid memory address or hardware access
 *       (generated via ~ATC_TAG(addr) in the slow path)
 * - 10: empty atc line
 */

#define ATC_TAG_SHIFT		18
#define ATC_TAG(addr)		((uae_u32)(addr) >> ATC_TAG_SHIFT)


#define ATC_L1_SIZE_LOG		8
#define ATC_L1_SIZE		(1 << ATC_L1_SIZE_LOG)

#define ATC_L1_INDEX(addr)	(((addr) >> 12) % ATC_L1_SIZE)

/*
 * first level atc cache
 * indexed by [super][data][rw][idx]
 */

typedef struct mmu_atc_line mmu_atc_l1_array[2][2][ATC_L1_SIZE];
extern mmu_atc_l1_array atc_l1[2];
extern mmu_atc_l1_array *current_atc;

#define ATC_L2_SIZE_LOG		12
#define ATC_L2_SIZE		(1 << ATC_L2_SIZE_LOG)

#define ATC_L2_INDEX(addr)	((((addr) >> 12) ^ ((addr) >> (32 - ATC_L2_SIZE_LOG))) % ATC_L2_SIZE)

extern struct mmu_atc_line atc_l2[2][ATC_L2_SIZE];

/*
 * lookup address in the level 1 atc cache,
 * the data and write arguments are constant in the common,
 * thus allows gcc to generate a constant offset.
 */
STATIC_INLINE int mmu_lookup(uaecptr addr, int data, int write,
									  struct mmu_atc_line **cl)
{
	addr >>= 12;
	*cl = &(*current_atc)[data][write][addr % ATC_L1_SIZE];
	return (*cl)->tag == addr >> (ATC_TAG_SHIFT - 12);
}

/*
 * similiar to mmu_user_lookup, but for the use of the moves instruction
 */
STATIC_INLINE int mmu_user_lookup(uaecptr addr, int super, int data,
										   int write, struct mmu_atc_line **cl)
{
	addr >>= 12;
	*cl = &atc_l1[super][data][write][addr % ATC_L1_SIZE];
	return (*cl)->tag == addr >> (ATC_TAG_SHIFT - 12);
}

extern uae_u16 REGPARAM3 mmu_get_word_unaligned(uaecptr addr, int data) REGPARAM;
extern uae_u32 REGPARAM3mmu_get_long_unaligned(uaecptr addr, int data) REGPARAM;

extern uae_u8 REGPARAM3 mmu_get_byte_slow(uaecptr addr, int super, int data,
				int size, struct mmu_atc_line *cl) REGPARAM;
extern uae_u16 REGPARAM3 mmu_get_word_slow(uaecptr addr, int super, int data,
					  int size, struct mmu_atc_line *cl) REGPARAM;
extern uae_u32 REGPARAM3 mmu_get_long_slow(uaecptr addr, int super, int data,
										   int size, struct mmu_atc_line *cl) REGPARAM;

extern void REGPARAM3 mmu_put_word_unaligned(uaecptr addr, uae_u16 val, int data) REGPARAM;
extern void REGPARAM3 mmu_put_long_unaligned(uaecptr addr, uae_u32 val, int data) REGPARAM;

extern void REGPARAM3 mmu_put_byte_slow(uaecptr addr, uae_u8 val, int super, int data,
										int size, struct mmu_atc_line *cl) REGPARAM;
extern void REGPARAM3 mmu_put_word_slow(uaecptr addr, uae_u16 val, int super, int data,
										int size, struct mmu_atc_line *cl) REGPARAM;
extern void REGPARAM3 mmu_put_long_slow(uaecptr addr, uae_u32 val, int super, int data,
										int size, struct mmu_atc_line *cl) REGPARAM;

extern void mmu_make_transparent_region(uaecptr baseaddr, uae_u32 size, int datamode);

STATIC_INLINE void mmu_set_ttr(int regno, uae_u32 val)
{
	uae_u32 * ttr;
	switch(regno)	{
		case TTR_I0:	ttr = &regs.itt0;	break;
		case TTR_I1:	ttr = &regs.itt1;	break;
		case TTR_D0:	ttr = &regs.dtt0;	break;
		case TTR_D1:	ttr = &regs.dtt1;	break;
		default: abort();
	}
	*ttr = val;
}

STATIC_INLINE void mmu_set_mmusr(uae_u32 val)
{
	regs.mmusr = val;
}

STATIC_INLINE void mmu_set_root_pointer(int regno, uae_u32 val)
{
	val &= MMU_ROOT_PTR_ADDR_MASK;
	switch (regno) {
	case 0x806:
		regs.urp = val;
		break;
	case 0x807:
		regs.srp = val;
		break;
	default:
		abort();
	}
}

#define FC_DATA		(regs.s ? 5 : 1)
#define FC_INST		(regs.s ? 6 : 2)

extern uaecptr REGPARAM3 mmu_translate(uaecptr addr, int super, int data, int write);

extern uae_u32 REGPARAM3 sfc_get_long(uaecptr addr) REGPARAM;
extern uae_u16 REGPARAM3 sfc_get_word(uaecptr addr) REGPARAM;
extern uae_u8 REGPARAM3 sfc_get_byte(uaecptr addr) REGPARAM;
extern void REGPARAM3 dfc_put_long(uaecptr addr, uae_u32 val) REGPARAM;
extern void REGPARAM3 dfc_put_word(uaecptr addr, uae_u16 val) REGPARAM;
extern void REGPARAM3 dfc_put_byte(uaecptr addr, uae_u8 val) REGPARAM;

extern void REGPARAM3 mmu_flush_atc(uaecptr addr, int super, int global) REGPARAM;
extern void REGPARAM3 mmu_flush_atc_all(int global) REGPARAM;

extern void REGPARAM3 mmu_reset(void) REGPARAM;
extern void REGPARAM3 mmu_set_tc(uae_u16 tc) REGPARAM;
extern void REGPARAM3 mmu_set_super(int super) REGPARAM;


STATIC_INLINE int test_ram_boundary(uaecptr a, int b, int c, int d) { return 1; }
STATIC_INLINE void check_ram_boundary(uaecptr a, int b, int c) { }
STATIC_INLINE int phys_valid_address(uaecptr a,int b, int c) { return 1; }



STATIC_INLINE uae_u32 phys_get_long(uaecptr addr)
{
#if ARAM_PAGE_CHECK
    if (((addr ^ read_page) <= ARAM_PAGE_MASK))
        return do_get_mem_long((uae_u32*)(addr + read_offset));
#endif
#ifndef HW_SIGSEGV
    addr = addr < 0xff000000 ? addr : addr & 0x00ffffff;
    if ((addr & 0xfff00000) == 0x00f00000) return HWget_l(addr);
#endif
    check_ram_boundary(addr, 4, false);
    uae_u32 * const m = (uae_u32 *)phys_get_real_address(addr);
#if ARAM_PAGE_CHECK
    read_page = addr;
    read_offset = (uintptr)m - (uintptr)addr;
#endif
    return do_get_mem_long(m);
}

STATIC_INLINE uae_u32 phys_get_word(uaecptr addr)
{
#if ARAM_PAGE_CHECK
    if (((addr ^ read_page) <= ARAM_PAGE_MASK))
        return do_get_mem_word((uae_u16*)(addr + read_offset));
#endif
#ifndef HW_SIGSEGV
    addr = addr < 0xff000000 ? addr : addr & 0x00ffffff;
    if ((addr & 0xfff00000) == 0x00f00000) return HWget_w(addr);
#endif
    check_ram_boundary(addr, 2, false);
    uae_u16 * const m = (uae_u16 *)phys_get_real_address(addr);
#if ARAM_PAGE_CHECK
    read_page = addr;
    read_offset = (uintptr)m - (uintptr)addr;
#endif
    return do_get_mem_word(m);
}

STATIC_INLINE uae_u32 phys_get_byte(uaecptr addr)
{
#if ARAM_PAGE_CHECK
    if (((addr ^ read_page) <= ARAM_PAGE_MASK))
        return do_get_mem_byte((uae_u8*)(addr + read_offset));
#endif
#ifndef HW_SIGSEGV
    addr = addr < 0xff000000 ? addr : addr & 0x00ffffff;
    if ((addr & 0xfff00000) == 0x00f00000) return HWget_b(addr);
#endif
    check_ram_boundary(addr, 1, false);
    uae_u8 * const m = (uae_u8 *)phys_get_real_address(addr);
#if ARAM_PAGE_CHECK
    read_page = addr;
    read_offset = (uintptr)m - (uintptr)addr;
#endif
    return do_get_mem_byte(m);
}

STATIC_INLINE void phys_put_long(uaecptr addr, uae_u32 l)
{
#if ARAM_PAGE_CHECK
    if (((addr ^ write_page) <= ARAM_PAGE_MASK)) {
        do_put_mem_long((uae_u32*)(addr + write_offset), l);
        return;
    }
#endif
#ifndef HW_SIGSEGV
    addr = addr < 0xff000000 ? addr : addr & 0x00ffffff;
    if ((addr & 0xfff00000) == 0x00f00000) {
        HWput_l(addr, l);
        return;
    } 
#endif
    check_ram_boundary(addr, 4, true);
    uae_u32 * const m = (uae_u32 *)phys_get_real_address(addr);
#if ARAM_PAGE_CHECK
    write_page = addr;
    write_offset = (uintptr)m - (uintptr)addr;
#endif
    do_put_mem_long(m, l);
}

STATIC_INLINE void phys_put_word(uaecptr addr, uae_u32 w)
{
#if ARAM_PAGE_CHECK
    if (((addr ^ write_page) <= ARAM_PAGE_MASK)) {
        do_put_mem_word((uae_u16*)(addr + write_offset), w);
        return;
    }
#endif
#ifndef HW_SIGSEGV
    addr = addr < 0xff000000 ? addr : addr & 0x00ffffff;
    if ((addr & 0xfff00000) == 0x00f00000) {
        HWput_w(addr, w);
        return;
    }
#endif
    check_ram_boundary(addr, 2, true);
    uae_u16 * const m = (uae_u16 *)phys_get_real_address(addr);
#if ARAM_PAGE_CHECK
    write_page = addr;
    write_offset = (uintptr)m - (uintptr)addr;
#endif
    do_put_mem_word(m, w);
}

STATIC_INLINE void phys_put_byte(uaecptr addr, uae_u32 b)
{
#if ARAM_PAGE_CHECK
    if (((addr ^ write_page) <= ARAM_PAGE_MASK)) {
        do_put_mem_byte((uae_u8*)(addr + write_offset), b);
        return;
    }
#endif
#ifndef HW_SIGSEGV
    addr = addr < 0xff000000 ? addr : addr & 0x00ffffff;
    if ((addr & 0xfff00000) == 0x00f00000) {
        HWput_b(addr, b);
        return;
    }
#endif
    check_ram_boundary(addr, 1, true);
    uae_u8 * const m = (uae_u8 *)phys_get_real_address(addr);
#if ARAM_PAGE_CHECK
    write_page = addr;
    write_offset = (uintptr)m - (uintptr)addr;
#endif
    do_put_mem_byte(m, b);
}

#ifdef FULLMMU
STATIC_INLINE int is_unaligned(uaecptr addr, int size)
{
    return unlikely((addr & (size - 1)) && (addr ^ (addr + size - 1)) & 0x1000);
}

STATIC_INLINE uae_u8 *mmu_get_real_address(uaecptr addr, struct mmu_atc_line *cl)
{
	return do_get_real_address(cl->phys + addr);
}

STATIC_INLINE uae_u32 mmu_get_long(uaecptr addr, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, 0, &cl)))
		return do_get_mem_long((uae_u32 *)mmu_get_real_address(addr, cl));
	return mmu_get_long_slow(addr, regs.s, data, size, cl);
}

STATIC_INLINE uae_u32 get_long(uaecptr addr)
{
	if (unlikely(is_unaligned(addr, 4)))
		return mmu_get_long_unaligned(addr, 1);
	return mmu_get_long(addr, 1, sz_long);
}

STATIC_INLINE uae_u16 mmu_get_word(uaecptr addr, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, 0, &cl)))
		return do_get_mem_word((uae_u16 *)mmu_get_real_address(addr, cl));
	return mmu_get_word_slow(addr, regs.s, data, size, cl);
}

STATIC_INLINE uae_u16 get_word(uaecptr addr)
{
	if (unlikely(is_unaligned(addr, 2)))
		return mmu_get_word_unaligned(addr, 1);
	return mmu_get_word(addr, 1, sz_word);
}

STATIC_INLINE uae_u8 mmu_get_byte(uaecptr addr, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, 0, &cl)))
		return do_get_mem_byte((uae_u8 *)mmu_get_real_address(addr, cl));
	return mmu_get_byte_slow(addr, regs.s, data, size, cl);
}

STATIC_INLINE uae_u8 get_byte(uaecptr addr)
{
	return mmu_get_byte(addr, 1, sz_byte);
}

STATIC_INLINE void mmu_put_long(uaecptr addr, uae_u32 val, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, 1, &cl)))
		do_put_mem_long((uae_u32 *)mmu_get_real_address(addr, cl), val);
	else
		mmu_put_long_slow(addr, val, regs.s, data, size, cl);
}

STATIC_INLINE void put_long(uaecptr addr, uae_u32 val)
{
	if (unlikely(is_unaligned(addr, 4)))
		mmu_put_long_unaligned(addr, val, 1);
	else
		mmu_put_long(addr, val, 1, sz_long);
}

STATIC_INLINE void mmu_put_word(uaecptr addr, uae_u16 val, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, 1, &cl)))
		do_put_mem_word((uae_u16 *)mmu_get_real_address(addr, cl), val);
	else
		mmu_put_word_slow(addr, val, regs.s, data, size, cl);
}

STATIC_INLINE void put_word(uaecptr addr, uae_u16 val)
{
	if (unlikely(is_unaligned(addr, 2)))
		mmu_put_word_unaligned(addr, val, 1);
	else
		mmu_put_word(addr, val, 1, sz_word);
}

STATIC_INLINE void mmu_put_byte(uaecptr addr, uae_u8 val, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, 1, &cl)))
		do_put_mem_byte((uae_u8 *)mmu_get_real_address(addr, cl), val);
	else
		mmu_put_byte_slow(addr, val, regs.s, data, size, cl);
}

STATIC_INLINE void put_byte(uaecptr addr, uae_u8 val)
{
	mmu_put_byte(addr, val, 1, sz_byte);
}

STATIC_INLINE uae_u8 *get_real_address(uaecptr addr, int write, int sz)
{
	(void)sz;
    return phys_get_real_address(mmu_translate(addr, regs.s, 1, write));
}

STATIC_INLINE uae_u32 mmu_get_user_long(uaecptr addr, int super, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, 0, &cl)))
		return do_get_mem_long((uae_u32 *)mmu_get_real_address(addr, cl));
	return mmu_get_long_slow(addr, super, data, size, cl);
}

STATIC_INLINE uae_u16 mmu_get_user_word(uaecptr addr, int super, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, 0, &cl)))
		return do_get_mem_word((uae_u16 *)mmu_get_real_address(addr, cl));
	return mmu_get_word_slow(addr, super, data, size, cl);
}

STATIC_INLINE uae_u8 mmu_get_user_byte(uaecptr addr, int super, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, 0, &cl)))
		return do_get_mem_byte((uae_u8 *)mmu_get_real_address(addr, cl));
	return mmu_get_byte_slow(addr, super, data, size, cl);
}

STATIC_INLINE void mmu_put_user_long(uaecptr addr, uae_u32 val, int super, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, 1, &cl)))
		do_put_mem_long((uae_u32 *)mmu_get_real_address(addr, cl), val);
	else
		mmu_put_long_slow(addr, val, super, data, size, cl);
}

STATIC_INLINE void mmu_put_user_word(uaecptr addr, uae_u16 val, int super, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, 1, &cl)))
		do_put_mem_word((uae_u16 *)mmu_get_real_address(addr, cl), val);
	else
		mmu_put_word_slow(addr, val, super, data, size, cl);
}

STATIC_INLINE void mmu_put_user_byte(uaecptr addr, uae_u8 val, int super, int data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, 1, &cl)))
		do_put_mem_byte((uae_u8 *)mmu_get_real_address(addr, cl), val);
	else
		mmu_put_byte_slow(addr, val, super, data, size, cl);
}

STATIC_INLINE int valid_address(uaecptr addr, int write, int sz)
{
    SAVE_EXCEPTION;
    TRY(prb) {
		(void)sz;
		check_ram_boundary(mmu_translate(addr, regs.s, 1, (write ? 1 : 0)), sz, write);
		RESTORE_EXCEPTION;
		return true;
    }
    CATCH(prb) {
		RESTORE_EXCEPTION;
		return false;
    } 
}

#else

#define get_long(a)			phys_get_long(a)
#define get_word(a)			phys_get_word(a)
#define get_byte(a)			phys_get_byte(a)
#define put_long(a,b)			phys_put_long(a,b)
#define put_word(a,b)			phys_put_word(a,b)
#define put_byte(a,b)			phys_put_byte(a,b)
#define get_real_address(a,w,s)	phys_get_real_address(a)

#define valid_address(a,w,s)		phys_valid_address(a,w,s)
#endif

STATIC_INLINE void flush_internals(void)
{
#if ARAM_PAGE_CHECK
    pc_page = 0xeeeeeeee;
    read_page = 0xeeeeeeee;
    write_page = 0xeeeeeeee;
#endif
}


#endif
