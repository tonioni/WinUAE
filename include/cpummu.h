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

#ifndef FULLMMU
#define FULLMMU
#endif

#define DUNUSED(x)
#define D
#if DEBUG
#define bug write_log
#else
#define bug
#endif

struct m68k_exception {
	int prb;
	m68k_exception (int exc) : prb (exc) {}
	operator int() { return prb; }
};
#define SAVE_EXCEPTION
#define RESTORE_EXCEPTION
#define TRY(var) try
#define CATCH(var) catch(m68k_exception var)
#define THROW(n) throw m68k_exception(n)
#define THROW_AGAIN(var) throw
#define VOLATILE
#define ALWAYS_INLINE __inline

#define likely(x) x
#define unlikely(x) x
static __inline void flush_internals (void) { }

typedef uae_u8 flagtype;

struct xttrx {
    uae_u32 log_addr_base : 8;
    uae_u32 log_addr_mask : 8;
    uae_u32 enable : 1;
    uae_u32 s_field : 2;
    uae_u32 : 3;
    uae_u32 usr1 : 1;
    uae_u32 usr0 : 1;
    uae_u32 : 1;
    uae_u32 cmode : 2;
    uae_u32 : 2;
    uae_u32 write : 1;
    uae_u32 : 2;
};

struct mmusr_t {
   uae_u32 phys_addr : 20;
   uae_u32 bus_err : 1;
   uae_u32 global : 1;
   uae_u32 usr1 : 1;
   uae_u32 usr0 : 1;
   uae_u32 super : 1;
   uae_u32 cmode : 2;
   uae_u32 modif : 1;
   uae_u32 : 1;
   uae_u32 write : 1;
   uae_u32 ttrhit : 1;
   uae_u32 resident : 1;
};

struct log_addr4 {
   uae_u32 rif : 7;
   uae_u32 pif : 7;
   uae_u32 paif : 6;
   uae_u32 poff : 12;
};

struct log_addr8 {
  uae_u32 rif : 7;
  uae_u32 pif : 7;
  uae_u32 paif : 5;
  uae_u32 poff : 13;
};

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
#define MMU_SSW_SIZE_B	0x0020
#define MMU_SSW_SIZE_W	0x0040
#define MMU_SSW_SIZE_L	0x0000
#define MMU_SSW_RW		0x0100
#define MMU_SSW_LK		0x0200
#define MMU_SSW_ATC		0x0400
#define MMU_SSW_MA		0x0800
#define MMU_SSW_CM	0x1000
#define MMU_SSW_CT	0x2000
#define MMU_SSW_CU	0x4000
#define MMU_SSW_CP	0x8000

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
#define ATC_L1_SIZE			(1 << ATC_L1_SIZE_LOG)

#define ATC_L1_INDEX(addr)	(((addr) >> 12) % ATC_L1_SIZE)

/*
 * first level atc cache
 * indexed by [super][data][rw][idx]
 */

typedef struct mmu_atc_line mmu_atc_l1_array[2][2][ATC_L1_SIZE];
extern mmu_atc_l1_array atc_l1[2];
extern mmu_atc_l1_array *current_atc;

#define ATC_L2_SIZE_LOG		12
#define ATC_L2_SIZE			(1 << ATC_L2_SIZE_LOG)

#define ATC_L2_INDEX(addr)	((((addr) >> 12) ^ ((addr) >> (32 - ATC_L2_SIZE_LOG))) % ATC_L2_SIZE)

/*
 * lookup address in the level 1 atc cache,
 * the data and write arguments are constant in the common,
 * thus allows gcc to generate a constant offset.
 */
static ALWAYS_INLINE bool mmu_lookup(uaecptr addr, bool data, bool write,
									  struct mmu_atc_line **cl)
{
	addr >>= 12;
	*cl = &(*current_atc)[data ? 1 : 0][write ? 1 : 0][addr % ATC_L1_SIZE];
	return (*cl)->tag == addr >> (ATC_TAG_SHIFT - 12);
}

/*
 * similiar to mmu_user_lookup, but for the use of the moves instruction
 */
static ALWAYS_INLINE bool mmu_user_lookup(uaecptr addr, bool super, bool data,
										   bool write, struct mmu_atc_line **cl)
{
	addr >>= 12;
	*cl = &atc_l1[super ? 1 : 0][data ? 1 : 0][write ? 1 : 0][addr % ATC_L1_SIZE];
	return (*cl)->tag == addr >> (ATC_TAG_SHIFT - 12);
}

extern uae_u16 REGPARAM3 mmu_get_word_unaligned(uaecptr addr, bool data) REGPARAM;
extern uae_u32 REGPARAM3 mmu_get_long_unaligned(uaecptr addr, bool data) REGPARAM;

extern uae_u8 REGPARAM3 mmu_get_byte_slow(uaecptr addr, bool super, bool data,
										  int size, struct mmu_atc_line *cl) REGPARAM;
extern uae_u16 REGPARAM3 mmu_get_word_slow(uaecptr addr, bool super, bool data,
										   int size, struct mmu_atc_line *cl) REGPARAM;
extern uae_u32 REGPARAM3 mmu_get_long_slow(uaecptr addr, bool super, bool data,
										   int size, struct mmu_atc_line *cl) REGPARAM;

extern void REGPARAM3 mmu_put_word_unaligned(uaecptr addr, uae_u16 val, bool data) REGPARAM;
extern void REGPARAM3 mmu_put_long_unaligned(uaecptr addr, uae_u32 val, bool data) REGPARAM;

extern void REGPARAM3 mmu_put_byte_slow(uaecptr addr, uae_u8 val, bool super, bool data,
										int size, struct mmu_atc_line *cl) REGPARAM;
extern void REGPARAM3 mmu_put_word_slow(uaecptr addr, uae_u16 val, bool super, bool data,
										int size, struct mmu_atc_line *cl) REGPARAM;
extern void REGPARAM3 mmu_put_long_slow(uaecptr addr, uae_u32 val, bool super, bool data,
										int size, struct mmu_atc_line *cl) REGPARAM;

extern void mmu_make_transparent_region(uaecptr baseaddr, uae_u32 size, int datamode);

#define FC_DATA		(regs.s ? 5 : 1)
#define FC_INST		(regs.s ? 6 : 2)

extern uaecptr REGPARAM3 mmu_translate(uaecptr addr, bool super, bool data, bool write) REGPARAM;

extern uae_u32 REGPARAM3 sfc_get_long(uaecptr addr) REGPARAM;
extern uae_u16 REGPARAM3 sfc_get_word(uaecptr addr) REGPARAM;
extern uae_u8 REGPARAM3 sfc_get_byte(uaecptr addr) REGPARAM;
extern void REGPARAM3 dfc_put_long(uaecptr addr, uae_u32 val) REGPARAM;
extern void REGPARAM3 dfc_put_word(uaecptr addr, uae_u16 val) REGPARAM;
extern void REGPARAM3 dfc_put_byte(uaecptr addr, uae_u8 val) REGPARAM;

extern void REGPARAM3 mmu_op_real(uae_u32 opcode, uae_u16 extra) REGPARAM;

extern void REGPARAM3 mmu_reset(void) REGPARAM;
extern void REGPARAM3 mmu_set_tc(uae_u16 tc) REGPARAM;
extern void REGPARAM3 mmu_set_super(bool super) REGPARAM;

static ALWAYS_INLINE bool is_unaligned(uaecptr addr, int size)
{
    return unlikely((addr & (size - 1)) && (addr ^ (addr + size - 1)) & 0x1000);
}

static ALWAYS_INLINE uaecptr mmu_get_real_address(uaecptr addr, struct mmu_atc_line *cl)
{
    return cl->phys + addr;
}

static ALWAYS_INLINE void phys_put_long(uaecptr addr, uae_u32 l)
{
    longput(addr, l);
}
static ALWAYS_INLINE void phys_put_word(uaecptr addr, uae_u32 w)
{
    wordput(addr, w);
}
static ALWAYS_INLINE void phys_put_byte(uaecptr addr, uae_u32 b)
{
    byteput(addr, b);
}
static ALWAYS_INLINE uae_u32 phys_get_long(uaecptr addr)
{
    return longget (addr);
}
static ALWAYS_INLINE uae_u32 phys_get_word(uaecptr addr)
{
    return wordget (addr);
}
static ALWAYS_INLINE uae_u32 phys_get_byte(uaecptr addr)
{
    return byteget (addr);
}

static ALWAYS_INLINE uae_u32 mmu_get_long(uaecptr addr, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, false, &cl)))
		return phys_get_long(mmu_get_real_address(addr, cl));
	return mmu_get_long_slow(addr, regs.s != 0, data, size, cl);
}

static ALWAYS_INLINE uae_u16 mmu_get_word(uaecptr addr, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, false, &cl)))
		return phys_get_word(mmu_get_real_address(addr, cl));
	return mmu_get_word_slow(addr, regs.s != 0, data, size, cl);
}

static ALWAYS_INLINE uae_u8 mmu_get_byte(uaecptr addr, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, false, &cl)))
		return phys_get_byte(mmu_get_real_address(addr, cl));
	return mmu_get_byte_slow(addr, regs.s != 0, data, size, cl);
}


static ALWAYS_INLINE void mmu_put_long(uaecptr addr, uae_u32 val, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, true, &cl)))
		phys_put_long(mmu_get_real_address(addr, cl), val);
	else
		mmu_put_long_slow(addr, val, regs.s != 0, data, size, cl);
}

static ALWAYS_INLINE void mmu_put_word(uaecptr addr, uae_u16 val, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, true, &cl)))
		phys_put_word(mmu_get_real_address(addr, cl), val);
	else
		mmu_put_word_slow(addr, val, regs.s != 0, data, size, cl);
}

static ALWAYS_INLINE void mmu_put_byte(uaecptr addr, uae_u8 val, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_lookup(addr, data, true, &cl)))
		phys_put_byte(mmu_get_real_address(addr, cl), val);
	else
		mmu_put_byte_slow(addr, val, regs.s != 0, data, size, cl);
}

static ALWAYS_INLINE uae_u32 mmu_get_user_long(uaecptr addr, bool super, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, false, &cl)))
		return phys_get_long(mmu_get_real_address(addr, cl));
	return mmu_get_long_slow(addr, super, data, size, cl);
}

static ALWAYS_INLINE uae_u16 mmu_get_user_word(uaecptr addr, bool super, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, false, &cl)))
		return phys_get_word(mmu_get_real_address(addr, cl));
	return mmu_get_word_slow(addr, super, data, size, cl);
}

static ALWAYS_INLINE uae_u8 mmu_get_user_byte(uaecptr addr, bool super, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, false, &cl)))
		return phys_get_byte(mmu_get_real_address(addr, cl));
	return mmu_get_byte_slow(addr, super, data, size, cl);
}

static ALWAYS_INLINE void mmu_put_user_long(uaecptr addr, uae_u32 val, bool super, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, true, &cl)))
		phys_put_long(mmu_get_real_address(addr, cl), val);
	else
		mmu_put_long_slow(addr, val, super, data, size, cl);
}

static ALWAYS_INLINE void mmu_put_user_word(uaecptr addr, uae_u16 val, bool super, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, true, &cl)))
		phys_put_word(mmu_get_real_address(addr, cl), val);
	else
		mmu_put_word_slow(addr, val, super, data, size, cl);
}

static ALWAYS_INLINE void mmu_put_user_byte(uaecptr addr, uae_u8 val, bool super, bool data, int size)
{
	struct mmu_atc_line *cl;

	if (likely(mmu_user_lookup(addr, super, data, true, &cl)))
		phys_put_byte(mmu_get_real_address(addr, cl), val);
	else
		mmu_put_byte_slow(addr, val, super, data, size, cl);
}


static ALWAYS_INLINE void HWput_l(uaecptr addr, uae_u32 l)
{
    put_long (addr, l);
}
static ALWAYS_INLINE void HWput_w(uaecptr addr, uae_u32 w)
{
    put_word (addr, w);
}
static ALWAYS_INLINE void HWput_b(uaecptr addr, uae_u32 b)
{
    put_byte (addr, b);
}
static ALWAYS_INLINE uae_u32 HWget_l(uaecptr addr)
{
    return get_long (addr);
}
static ALWAYS_INLINE uae_u32 HWget_w(uaecptr addr)
{
    return get_word (addr);
}
static ALWAYS_INLINE uae_u32 HWget_b(uaecptr addr)
{
    return get_byte (addr);
}

static ALWAYS_INLINE uae_u32 uae_mmu_get_ilong(uaecptr addr)
{
	if (unlikely(is_unaligned(addr, 4)))
		return mmu_get_long_unaligned(addr, false);
	return mmu_get_long(addr, false, sz_long);
}
static ALWAYS_INLINE uae_u16 uae_mmu_get_iword(uaecptr addr)
{
	if (unlikely(is_unaligned(addr, 2)))
		return mmu_get_word_unaligned(addr, false);
	return mmu_get_word(addr, false, sz_word);
}
static ALWAYS_INLINE uae_u16 uae_mmu_get_ibyte(uaecptr addr)
{
	return mmu_get_byte(addr, false, sz_byte);
}
static ALWAYS_INLINE uae_u32 uae_mmu_get_long(uaecptr addr)
{
	if (unlikely(is_unaligned(addr, 4)))
		return mmu_get_long_unaligned(addr, true);
	return mmu_get_long(addr, true, sz_long);
}
static ALWAYS_INLINE uae_u16 uae_mmu_get_word(uaecptr addr)
{
	if (unlikely(is_unaligned(addr, 2)))
		return mmu_get_word_unaligned(addr, true);
	return mmu_get_word(addr, true, sz_word);
}
static ALWAYS_INLINE uae_u8 uae_mmu_get_byte(uaecptr addr)
{
	return mmu_get_byte(addr, true, sz_byte);
}
static ALWAYS_INLINE void uae_mmu_put_long(uaecptr addr, uae_u32 val)
{
	if (unlikely(is_unaligned(addr, 4)))
		mmu_put_long_unaligned(addr, val, true);
	else
		mmu_put_long(addr, val, true, sz_long);
}
static ALWAYS_INLINE void uae_mmu_put_word(uaecptr addr, uae_u16 val)
{
	if (unlikely(is_unaligned(addr, 2)))
		mmu_put_word_unaligned(addr, val, true);
	else
		mmu_put_word(addr, val, true, sz_word);
}
static ALWAYS_INLINE void uae_mmu_put_byte(uaecptr addr, uae_u8 val)
{
	mmu_put_byte(addr, val, true, sz_byte);
}

STATIC_INLINE void put_byte_mmu (uaecptr addr, uae_u32 v)
{
    uae_mmu_put_byte (addr, v);
}
STATIC_INLINE void put_word_mmu (uaecptr addr, uae_u32 v)
{
    uae_mmu_put_word (addr, v);
}
STATIC_INLINE void put_long_mmu (uaecptr addr, uae_u32 v)
{
    uae_mmu_put_long (addr, v);
}
STATIC_INLINE uae_u32 get_byte_mmu (uaecptr addr)
{
    return uae_mmu_get_byte (addr);
}
STATIC_INLINE uae_u32 get_word_mmu (uaecptr addr)
{
    return uae_mmu_get_word (addr);
}
STATIC_INLINE uae_u32 get_long_mmu (uaecptr addr)
{
    return uae_mmu_get_long (addr);
}
STATIC_INLINE uae_u32 get_ibyte_mmu (int o)
{
    uae_u32 pc = m68k_getpc () + o;
    return uae_mmu_get_iword (pc);
}
STATIC_INLINE uae_u32 get_iword_mmu (int o)
{
    uae_u32 pc = m68k_getpc () + o;
    return uae_mmu_get_iword (pc);
}
STATIC_INLINE uae_u32 get_ilong_mmu (int o)
{
    uae_u32 pc = m68k_getpc () + o;
    return uae_mmu_get_ilong (pc);
}
STATIC_INLINE uae_u32 next_iword_mmu (void)
{
    uae_u32 pc = m68k_getpc ();
    m68k_incpci (2);
    return uae_mmu_get_iword (pc);
}
STATIC_INLINE uae_u32 next_ilong_mmu (void)
{
    uae_u32 pc = m68k_getpc ();
    m68k_incpci (4);
    return uae_mmu_get_ilong (pc);
}

extern void m68k_do_rts_mmu (void);
extern void m68k_do_rte_mmu (uaecptr a7);
extern void m68k_do_bsr_mmu (uaecptr oldpc, uae_s32 offset);

struct mmufixup
{
    int reg;
    uae_u32 value;
};
extern struct mmufixup mmufixup[2];

#endif /* CPUMMU_H */
