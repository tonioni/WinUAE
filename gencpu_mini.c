/*
 * UAE - The Un*x Amiga Emulator
 *
 * MC68000 emulation generator
 *
 * This is a fairly stupid program that generates a lot of case labels that
 * can be #included in a switch statement.
 * As an alternative, it can generate functions that handle specific
 * MC68000 instructions, plus a prototype header file and a function pointer
 * array to look up the function for an opcode.
 * Error checking is bad, an illegal table68k file will cause the program to
 * call abort().
 * The generated code is sometimes sub-optimal, an optimizing compiler should
 * take care of this.
 *
 * The source for the insn timings is Markt & Technik's Amiga Magazin 8/1992.
 *
 * Copyright 1995, 1996, 1997, 1998, 1999, 2000 Bernd Schmidt
 */

#include "sysconfig.h"
#include "sysdeps.h"
#include <ctype.h>

#include "readcpu.h"

#define BOOL_TYPE "int"

static FILE *headerfile;
static FILE *stblfile;

static int using_prefetch;
static int using_exception_3;
static int cpu_level;

/* For the current opcode, the next lower level that will have different code.
 * Initialized to -1 for each opcode. If it remains unchanged, indicates we
 * are done with that opcode.  */
static int next_cpu_level;

static int *opcode_map;
static int *opcode_next_clev;
static int *opcode_last_postfix;
static unsigned long *counts;

static void read_counts (void)
{
    FILE *file;
    unsigned long opcode, count, total;
    char name[20];
    int nr = 0;
    memset (counts, 0, 65536 * sizeof *counts);

    file = fopen ("frequent.68k", "r");
    if (file) {
	fscanf (file, "Total: %lu\n", &total);
	while (fscanf (file, "%lx: %lu %s\n", &opcode, &count, name) == 3) {
	    opcode_next_clev[nr] = 5;
	    opcode_last_postfix[nr] = -1;
	    opcode_map[nr++] = opcode;
	    counts[opcode] = count;
	}
	fclose (file);
    }
    if (nr == nr_cpuop_funcs)
	return;
    for (opcode = 0; opcode < 0x10000; opcode++) {
	if (table68k[opcode].handler == -1 && table68k[opcode].mnemo != i_ILLG
	    && counts[opcode] == 0)
	{
	    opcode_next_clev[nr] = 5;
	    opcode_last_postfix[nr] = -1;
	    opcode_map[nr++] = opcode;
	    counts[opcode] = count;
	}
    }
    if (nr != nr_cpuop_funcs)
	abort ();
}

static char endlabelstr[80];
static int endlabelno = 0;
static int need_endlabel;

static int n_braces = 0;
static int m68k_pc_offset = 0;

static void start_brace (void)
{
    n_braces++;
    printf ("{");
}

static void close_brace (void)
{
    assert (n_braces > 0);
    n_braces--;
    printf ("}");
}

static void finish_braces (void)
{
    while (n_braces > 0)
	close_brace ();
}

static void pop_braces (int to)
{
    while (n_braces > to)
	close_brace ();
}

static int bit_size (int size)
{
    switch (size) {
     case sz_byte: return 8;
     case sz_word: return 16;
     case sz_long: return 32;
     default: abort ();
    }
    return 0;
}

static const char *bit_mask (int size)
{
    switch (size) {
     case sz_byte: return "0xff";
     case sz_word: return "0xffff";
     case sz_long: return "0xffffffff";
     default: abort ();
    }
    return 0;
}

static const char *gen_nextilong (void)
{
    static char buffer[80];
    int r = m68k_pc_offset;
    m68k_pc_offset += 4;

    sprintf (buffer, "xget_ilong (%d)", r);
    return buffer;
}

static const char *gen_nextiword (void)
{
    static char buffer[80];
    int r = m68k_pc_offset;
    m68k_pc_offset += 2;

    sprintf (buffer, "xget_iword (%d)", r);
    return buffer;
}

static const char *gen_nextibyte (void)
{
    static char buffer[80];
    int r = m68k_pc_offset;
    m68k_pc_offset += 2;

    sprintf (buffer, "xget_ibyte (%d)", r);
    return buffer;
}

static void sync_m68k_pc (void)
{
    if (m68k_pc_offset == 0)
	return;
    printf ("xm68k_incpc(%d);\n", m68k_pc_offset);
    m68k_pc_offset = 0;
}

/* getv == 1: fetch data; getv != 0: check for odd address. If movem != 0,
 * the calling routine handles Apdi and Aipi modes.
 * gb-- movem == 2 means the same thing but for a MOVE16 instruction */
static void genamode (amodes mode, char *reg, wordsizes size, char *name, int getv, int movem)
{
    start_brace ();
    switch (mode) {
    case Dreg:
	if (movem)
	    abort ();
	if (getv == 1)
	    switch (size) {
	    case sz_byte:
#if defined(AMIGA) && !defined(WARPUP)
		/* sam: I don't know why gcc.2.7.2.1 produces a code worse */
		/* if it is not done like that: */
		printf ("\tuae_s8 %s = ((uae_u8*)&m68k_dreg (regs, %s))[3];\n", name, reg);
#else
		printf ("\tuae_s8 %s = xm68k_dreg (%s);\n", name, reg);
#endif
		break;
	    case sz_word:
#if defined(AMIGA) && !defined(WARPUP)
		printf ("\tuae_s16 %s = ((uae_s16*)&m68k_dreg (regs, %s))[1];\n", name, reg);
#else
		printf ("\tuae_s16 %s = xm68k_dreg (%s);\n", name, reg);
#endif
		break;
	    case sz_long:
		printf ("\tuae_s32 %s = xm68k_dreg (%s);\n", name, reg);
		break;
	    default:
		abort ();
	    }
	return;
    case Areg:
	if (movem)
	    abort ();
	if (getv == 1)
	    switch (size) {
	    case sz_word:
		printf ("\tuae_s16 %s = xm68k_areg (%s);\n", name, reg);
		break;
	    case sz_long:
		printf ("\tuae_s32 %s = xm68k_areg (%s);\n", name, reg);
		break;
	    default:
		abort ();
	    }
	return;
    case Aind:
	printf ("\tuaecptr %sa = xm68k_areg (%s);\n", name, reg);
	break;
    case Aipi:
	printf ("\tuaecptr %sa = xm68k_areg (%s);\n", name, reg);
	break;
    case Apdi:
	switch (size) {
	case sz_byte:
	    if (movem)
		printf ("\tuaecptr %sa = xm68k_areg (%s);\n", name, reg);
	    else
		printf ("\tuaecptr %sa = xm68k_areg (%s) - xareg_byteinc[%s];\n", name, reg, reg);
	    break;
	case sz_word:
	    printf ("\tuaecptr %sa = xm68k_areg (%s) - %d;\n", name, reg, movem ? 0 : 2);
	    break;
	case sz_long:
	    printf ("\tuaecptr %sa = xm68k_areg (%s) - %d;\n", name, reg, movem ? 0 : 4);
	    break;
	default:
	    abort ();
	}
	break;
    case Ad16:
	printf ("\tuaecptr %sa = xm68k_areg (%s) + (uae_s32)(uae_s16)%s;\n", name, reg, gen_nextiword ());
	break;
    case Ad8r:
	if (cpu_level > 1) {
	    if (next_cpu_level < 1)
		next_cpu_level = 1;
	    sync_m68k_pc ();
	    start_brace ();
	    /* This would ordinarily be done in gen_nextiword, which we bypass.  */
	    printf ("\tuaecptr %sa = xget_disp_ea_020 (xm68k_areg (%s), xnext_iword ());\n", name, reg);
	} else
	    printf ("\tuaecptr %sa = xget_disp_ea_000 (xm68k_areg (%s), %s);\n", name, reg, gen_nextiword ());

	break;
    case PC16:
	printf ("\tuaecptr %sa = xm68k_getpc () + %d;\n", name, m68k_pc_offset);
	printf ("\t%sa += (uae_s32)(uae_s16)%s;\n", name, gen_nextiword ());
	break;
    case PC8r:
	if (cpu_level > 1) {
	    if (next_cpu_level < 1)
		next_cpu_level = 1;
	    sync_m68k_pc ();
	    start_brace ();
	    /* This would ordinarily be done in gen_nextiword, which we bypass.  */
	    printf ("\tuaecptr tmppc = xm68k_getpc ();\n");
	    printf ("\tuaecptr %sa = xget_disp_ea_020(tmppc, xnext_iword ());\n", name);
	} else {
	    printf ("\tuaecptr tmppc = xm68k_getpc () + %d;\n", m68k_pc_offset);
	    printf ("\tuaecptr %sa = xget_disp_ea_000 (tmppc, %s);\n", name, gen_nextiword ());
	}

	break;
    case absw:
	printf ("\tuaecptr %sa = (uae_s32)(uae_s16)%s;\n", name, gen_nextiword ());
	break;
    case absl:
	printf ("\tuaecptr %sa = %s;\n", name, gen_nextilong ());
	break;
    case imm:
	if (getv != 1)
	    abort ();
	switch (size) {
	case sz_byte:
	    printf ("\tuae_s8 %s = %s;\n", name, gen_nextibyte ());
	    break;
	case sz_word:
	    printf ("\tuae_s16 %s = %s;\n", name, gen_nextiword ());
	    break;
	case sz_long:
	    printf ("\tuae_s32 %s = %s;\n", name, gen_nextilong ());
	    break;
	default:
	    abort ();
	}
	return;
    case imm0:
	if (getv != 1)
	    abort ();
	printf ("\tuae_s8 %s = %s;\n", name, gen_nextibyte ());
	return;
    case imm1:
	if (getv != 1)
	    abort ();
	printf ("\tuae_s16 %s = %s;\n", name, gen_nextiword ());
	return;
    case imm2:
	if (getv != 1)
	    abort ();
	printf ("\tuae_s32 %s = %s;\n", name, gen_nextilong ());
	return;
    case immi:
	if (getv != 1)
	    abort ();
	printf ("\tuae_u32 %s = %s;\n", name, reg);
	return;
    default:
	abort ();
    }

    if (getv == 1) {
	start_brace ();
	switch (size) {
	case sz_byte: printf ("\tuae_s8 %s = xget_byte (%sa);\n", name, name); break;
	case sz_word: printf ("\tuae_s16 %s = xget_word (%sa);\n", name, name); break;
	case sz_long: printf ("\tuae_s32 %s = xget_long (%sa);\n", name, name); break;
	default: abort ();
	}
    }

    /* We now might have to fix up the register for pre-dec or post-inc
     * addressing modes. */
    if (!movem)
	switch (mode) {
	case Aipi:
	    switch (size) {
	    case sz_byte:
		printf ("\txm68k_areg (%s) += xareg_byteinc[%s];\n", reg, reg);
		break;
	    case sz_word:
		printf ("\txm68k_areg (%s) += 2;\n", reg);
		break;
	    case sz_long:
		printf ("\txm68k_areg (%s) += 4;\n", reg);
		break;
	    default:
		abort ();
	    }
	    break;
	case Apdi:
	    printf ("\txm68k_areg (%s) = %sa;\n", reg, name);
	    break;
	default:
	    break;
	}
}

static void genastore (char *from, amodes mode, char *reg, wordsizes size, char *to)
{
    switch (mode) {
     case Dreg:
	switch (size) {
	 case sz_byte:
	    printf ("\txm68k_dreg (%s) = (xm68k_dreg (%s) & ~0xff) | ((%s) & 0xff);\n", reg, reg, from);
	    break;
	 case sz_word:
	    printf ("\txm68k_dreg (%s) = (xm68k_dreg (%s) & ~0xffff) | ((%s) & 0xffff);\n", reg, reg, from);
	    break;
	 case sz_long:
	    printf ("\txm68k_dreg (%s) = (%s);\n", reg, from);
	    break;
	 default:
	    abort ();
	}
	break;
     case Areg:
	switch (size) {
	 case sz_word:
	    printf ("\txm68k_areg (%s) = (uae_s32)(uae_s16)(%s);\n", reg, from);
	    break;
	 case sz_long:
	    printf ("\txm68k_areg (%s) = (%s);\n", reg, from);
	    break;
	 default:
	    abort ();
	}
	break;
     case Aind:
     case Aipi:
     case Apdi:
     case Ad16:
     case Ad8r:
     case absw:
     case absl:
     case PC16:
     case PC8r:
	if (using_prefetch)
	    sync_m68k_pc ();
	switch (size) {
	 case sz_byte:
	    printf ("\txput_byte (%sa,%s);\n", to, from);
	    break;
	 case sz_word:
	    if (cpu_level < 2 && (mode == PC16 || mode == PC8r))
		abort ();
	    printf ("\txput_word (%sa,%s);\n", to, from);
	    break;
	 case sz_long:
	    if (cpu_level < 2 && (mode == PC16 || mode == PC8r))
		abort ();
	    printf ("\txput_long (%sa,%s);\n", to, from);
	    break;
	 default:
	    abort ();
	}
	break;
     case imm:
     case imm0:
     case imm1:
     case imm2:
     case immi:
	abort ();
	break;
     default:
	abort ();
    }
}

static void genmovemel (uae_u16 opcode)
{
    char getcode[100];
    int size = table68k[opcode].size == sz_long ? 4 : 2;

    if (table68k[opcode].size == sz_long) {
	strcpy (getcode, "xget_long (srca)");
    } else {
	strcpy (getcode, "(uae_s32)(uae_s16)xget_word (srca)");
    }

    printf ("\tuae_u16 mask = %s;\n", gen_nextiword ());
    printf ("\tunsigned int dmask = mask & 0xff, amask = (mask >> 8) & 0xff;\n");
    genamode (table68k[opcode].dmode, "dstreg", table68k[opcode].size, "src", 2, 1);
    start_brace ();
    printf ("\twhile (dmask) { xm68k_dreg (xmovem_index1[dmask]) = %s; srca += %d; dmask = xmovem_next[dmask]; }\n",
	    getcode, size);
    printf ("\twhile (amask) { xm68k_areg (xmovem_index1[amask]) = %s; srca += %d; amask = xmovem_next[amask]; }\n",
	    getcode, size);

    if (table68k[opcode].dmode == Aipi)
	printf ("\txm68k_areg (dstreg) = srca;\n");
}

static void genmovemle (uae_u16 opcode)
{
    char putcode[100];
    int size = table68k[opcode].size == sz_long ? 4 : 2;
    if (table68k[opcode].size == sz_long) {
	strcpy (putcode, "xput_long (srca,");
    } else {
	strcpy (putcode, "xput_word (srca,");
    }

    printf ("\tuae_u16 mask = %s;\n", gen_nextiword ());
    genamode (table68k[opcode].dmode, "dstreg", table68k[opcode].size, "src", 2, 1);
    if (using_prefetch)
	sync_m68k_pc ();

    start_brace ();
    if (table68k[opcode].dmode == Apdi) {
	printf ("\tuae_u16 amask = mask & 0xff, dmask = (mask >> 8) & 0xff;\n");
	printf ("\twhile (amask) { srca -= %d; %s xm68k_areg (xmovem_index2[amask])); amask = xmovem_next[amask]; }\n",
		size, putcode);
	printf ("\twhile (dmask) { srca -= %d; %s xm68k_dreg (xmovem_index2[dmask])); dmask = xmovem_next[dmask]; }\n",
		size, putcode);
	printf ("\txm68k_areg (dstreg) = srca;\n");
    } else {
	printf ("\tuae_u16 dmask = mask & 0xff, amask = (mask >> 8) & 0xff;\n");
	printf ("\twhile (dmask) { %s xm68k_dreg (xmovem_index1[dmask])); srca += %d; dmask = xmovem_next[dmask]; }\n",
		putcode, size);
	printf ("\twhile (amask) { %s xm68k_areg (xmovem_index1[amask])); srca += %d; amask = xmovem_next[amask]; }\n",
		putcode, size);
    }
}

static void duplicate_carry (void)
{
    printf ("\tXCOPY_CARRY;\n");
}

typedef enum
{
  flag_logical_noclobber, flag_logical, flag_add, flag_sub, flag_cmp, flag_addx, flag_subx, flag_zn,
  flag_av, flag_sv
}
flagtypes;

static void genflags_normal (flagtypes type, wordsizes size, char *value, char *src, char *dst)
{
    char vstr[100], sstr[100], dstr[100];
    char usstr[100], udstr[100];
    char unsstr[100], undstr[100];

    switch (size) {
     case sz_byte:
	strcpy (vstr, "((uae_s8)(");
	strcpy (usstr, "((uae_u8)(");
	break;
     case sz_word:
	strcpy (vstr, "((uae_s16)(");
	strcpy (usstr, "((uae_u16)(");
	break;
     case sz_long:
	strcpy (vstr, "((uae_s32)(");
	strcpy (usstr, "((uae_u32)(");
	break;
     default:
	abort ();
    }
    strcpy (unsstr, usstr);

    strcpy (sstr, vstr);
    strcpy (dstr, vstr);
    strcat (vstr, value);
    strcat (vstr, "))");
    strcat (dstr, dst);
    strcat (dstr, "))");
    strcat (sstr, src);
    strcat (sstr, "))");

    strcpy (udstr, usstr);
    strcat (udstr, dst);
    strcat (udstr, "))");
    strcat (usstr, src);
    strcat (usstr, "))");

    strcpy (undstr, unsstr);
    strcat (unsstr, "-");
    strcat (undstr, "~");
    strcat (undstr, dst);
    strcat (undstr, "))");
    strcat (unsstr, src);
    strcat (unsstr, "))");

    switch (type) {
     case flag_logical_noclobber:
     case flag_logical:
     case flag_zn:
     case flag_av:
     case flag_sv:
     case flag_addx:
     case flag_subx:
	break;

     case flag_add:
	start_brace ();
	printf ("uae_u32 %s = %s + %s;\n", value, dstr, sstr);
	break;
     case flag_sub:
     case flag_cmp:
	start_brace ();
	printf ("uae_u32 %s = %s - %s;\n", value, dstr, sstr);
	break;
    }

    switch (type) {
     case flag_logical_noclobber:
     case flag_logical:
     case flag_zn:
	break;

     case flag_add:
     case flag_sub:
     case flag_addx:
     case flag_subx:
     case flag_cmp:
     case flag_av:
     case flag_sv:
	start_brace ();
	printf ("\t" BOOL_TYPE " flgs = %s < 0;\n", sstr);
	printf ("\t" BOOL_TYPE " flgo = %s < 0;\n", dstr);
	printf ("\t" BOOL_TYPE " flgn = %s < 0;\n", vstr);
	break;
    }

    switch (type) {
     case flag_logical:
	printf ("\tXCLEAR_CZNV;\n");
	printf ("\tXSET_ZFLG (%s == 0);\n", vstr);
	printf ("\tXSET_NFLG (%s < 0);\n", vstr);
	break;
     case flag_logical_noclobber:
	printf ("\tXSET_ZFLG (%s == 0);\n", vstr);
	printf ("\tXSET_NFLG (%s < 0);\n", vstr);
	break;
     case flag_av:
	printf ("\tXSET_VFLG ((flgs ^ flgn) & (flgo ^ flgn));\n");
	break;
     case flag_sv:
	printf ("\tXSET_VFLG ((flgs ^ flgo) & (flgn ^ flgo));\n");
	break;
     case flag_zn:
	printf ("\tXSET_ZFLG (XGET_ZFLG & (%s == 0));\n", vstr);
	printf ("\tXSET_NFLG (%s < 0);\n", vstr);
	break;
     case flag_add:
	printf ("\tXSET_ZFLG (%s == 0);\n", vstr);
	printf ("\tXSET_VFLG ((flgs ^ flgn) & (flgo ^ flgn));\n");
	printf ("\tXSET_CFLG (%s < %s);\n", undstr, usstr);
	duplicate_carry ();
	printf ("\tXSET_NFLG (flgn != 0);\n");
	break;
     case flag_sub:
	printf ("\tXSET_ZFLG (%s == 0);\n", vstr);
	printf ("\tXSET_VFLG ((flgs ^ flgo) & (flgn ^ flgo));\n");
	printf ("\tXSET_CFLG (%s > %s);\n", usstr, udstr);
	duplicate_carry ();
	printf ("\tXSET_NFLG (flgn != 0);\n");
	break;
     case flag_addx:
	printf ("\tXSET_VFLG ((flgs ^ flgn) & (flgo ^ flgn));\n"); /* minterm SON: 0x42 */
	printf ("\tXSET_CFLG (flgs ^ ((flgs ^ flgo) & (flgo ^ flgn)));\n"); /* minterm SON: 0xD4 */
	duplicate_carry ();
	break;
     case flag_subx:
	printf ("\tXSET_VFLG ((flgs ^ flgo) & (flgo ^ flgn));\n"); /* minterm SON: 0x24 */
	printf ("\tXSET_CFLG (flgs ^ ((flgs ^ flgn) & (flgo ^ flgn)));\n"); /* minterm SON: 0xB2 */
	duplicate_carry ();
	break;
     case flag_cmp:
	printf ("\tXSET_ZFLG (%s == 0);\n", vstr);
	printf ("\tXSET_VFLG ((flgs != flgo) && (flgn != flgo));\n");
	printf ("\tXSET_CFLG (%s > %s);\n", usstr, udstr);
	printf ("\tXSET_NFLG (flgn != 0);\n");
	break;
    }
}

static void genflags (flagtypes type, wordsizes size, char *value, char *src, char *dst)
{
    /* Temporarily deleted 68k/ARM flag optimizations.  I'd prefer to have
       them in the appropriate m68k.h files and use just one copy of this
       code here.  The API can be changed if necessary.  */
#ifdef OPTIMIZED_FLAGS
    switch (type) {
     case flag_add:
     case flag_sub:
	start_brace ();
	printf ("\tuae_u32 %s;\n", value);
	break;

     default:
	break;
    }

    /* At least some of those casts are fairly important! */
    switch (type) {
     case flag_logical_noclobber:
	printf ("\t{uae_u32 oldcznv = GET_CZNV & ~(FLAGVAL_Z | FLAGVAL_N);\n");
	if (strcmp (value, "0") == 0) {
	    printf ("\tSET_CZNV (olcznv | FLAGVAL_Z);\n");
	} else {
	    switch (size) {
	     case sz_byte: printf ("\toptflag_testb ((uae_s8)(%s));\n", value); break;
	     case sz_word: printf ("\toptflag_testw ((uae_s16)(%s));\n", value); break;
	     case sz_long: printf ("\toptflag_testl ((uae_s32)(%s));\n", value); break;
	    }
	    printf ("\tIOR_CZNV (oldcznv);\n");
	}
	printf ("\t}\n");
	return;
     case flag_logical:
	if (strcmp (value, "0") == 0) {
	    printf ("\tSET_CZNV (FLAGVAL_Z);\n");
	} else {
	    switch (size) {
	     case sz_byte: printf ("\toptflag_testb ((uae_s8)(%s));\n", value); break;
	     case sz_word: printf ("\toptflag_testw ((uae_s16)(%s));\n", value); break;
	     case sz_long: printf ("\toptflag_testl ((uae_s32)(%s));\n", value); break;
	    }
	}
	return;

     case flag_add:
	switch (size) {
	 case sz_byte: printf ("\toptflag_addb (%s, (uae_s8)(%s), (uae_s8)(%s));\n", value, src, dst); break;
	 case sz_word: printf ("\toptflag_addw (%s, (uae_s16)(%s), (uae_s16)(%s));\n", value, src, dst); break;
	 case sz_long: printf ("\toptflag_addl (%s, (uae_s32)(%s), (uae_s32)(%s));\n", value, src, dst); break;
	}
	return;

     case flag_sub:
	switch (size) {
	 case sz_byte: printf ("\toptflag_subb (%s, (uae_s8)(%s), (uae_s8)(%s));\n", value, src, dst); break;
	 case sz_word: printf ("\toptflag_subw (%s, (uae_s16)(%s), (uae_s16)(%s));\n", value, src, dst); break;
	 case sz_long: printf ("\toptflag_subl (%s, (uae_s32)(%s), (uae_s32)(%s));\n", value, src, dst); break;
	}
	return;

     case flag_cmp:
	switch (size) {
	 case sz_byte: printf ("\toptflag_cmpb ((uae_s8)(%s), (uae_s8)(%s));\n", src, dst); break;
	 case sz_word: printf ("\toptflag_cmpw ((uae_s16)(%s), (uae_s16)(%s));\n", src, dst); break;
	 case sz_long: printf ("\toptflag_cmpl ((uae_s32)(%s), (uae_s32)(%s));\n", src, dst); break;
	}
	return;

     default:
	break;
    }
#endif

    genflags_normal (type, size, value, src, dst);
}

static void force_range_for_rox (const char *var, wordsizes size)
{
    /* Could do a modulo operation here... which one is faster? */
    switch (size) {
     case sz_long:
	printf ("\tif (%s >= 33) %s -= 33;\n", var, var);
	break;
     case sz_word:
	printf ("\tif (%s >= 34) %s -= 34;\n", var, var);
	printf ("\tif (%s >= 17) %s -= 17;\n", var, var);
	break;
     case sz_byte:
	printf ("\tif (%s >= 36) %s -= 36;\n", var, var);
	printf ("\tif (%s >= 18) %s -= 18;\n", var, var);
	printf ("\tif (%s >= 9) %s -= 9;\n", var, var);
	break;
    }
}

static const char *cmask (wordsizes size)
{
    switch (size) {
     case sz_byte: return "0x80";
     case sz_word: return "0x8000";
     case sz_long: return "0x80000000";
     default: abort ();
    }
}

static int source_is_imm1_8 (struct instr *i)
{
    return i->stype == 3;
}

static void gen_opcode (unsigned long int opcode)
{
    struct instr *curi = table68k + opcode;

    switch (curi->plev) {
    case 0: /* not privileged */
	break;
    case 1: /* unprivileged only on 68000 */
	if (cpu_level == 0)
	    break;
	if (next_cpu_level < 0)
	    next_cpu_level = 0;

	/* fall through */
    case 2:
    case 3:
    return;
    }

    m68k_pc_offset = 2;
    start_brace ();

    switch (curi->mnemo) {
    case i_OR:
    case i_AND:
    case i_EOR:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	printf ("\tsrc %c= dst;\n", curi->mnemo == i_OR ? '|' : curi->mnemo == i_AND ? '&' : '^');
	genflags (flag_logical, curi->size, "src", "", "");
	genastore ("src", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_SUB:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	start_brace ();
	genflags (flag_sub, curi->size, "newv", "src", "dst");
	genastore ("newv", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_SUBA:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", sz_long, "dst", 1, 0);
	start_brace ();
	printf ("\tuae_u32 newv = dst - src;\n");
	genastore ("newv", curi->dmode, "dstreg", sz_long, "dst");
	break;
    case i_SUBX:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	start_brace ();
	printf ("\tuae_u32 newv = dst - src - (XGET_XFLG ? 1 : 0);\n");
	genflags (flag_subx, curi->size, "newv", "src", "dst");
	genflags (flag_zn, curi->size, "newv", "", "");
	genastore ("newv", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_SBCD:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	start_brace ();
	printf ("\tuae_u16 newv_lo = (dst & 0xF) - (src & 0xF) - (XGET_XFLG ? 1 : 0);\n");
	printf ("\tuae_u16 newv_hi = (dst & 0xF0) - (src & 0xF0);\n");
	printf ("\tuae_u16 newv, tmp_newv;\n");
	printf ("\tint bcd = 0;\n");
	printf ("\tnewv = tmp_newv = newv_hi + newv_lo;\n");
	printf ("\tif (newv_lo & 0xF0) { newv -= 6; bcd = 6; };\n");
	printf ("\tif ((((dst & 0xFF) - (src & 0xFF) - (XGET_XFLG ? 1 : 0)) & 0x100) > 0xFF) { newv -= 0x60; }\n");
	printf ("\tXSET_CFLG ((((dst & 0xFF) - (src & 0xFF) - bcd - (XGET_XFLG ? 1 : 0)) & 0x300) > 0xFF);\n");
	duplicate_carry ();
	genflags (flag_zn, curi->size, "newv", "", "");
	printf ("\tXSET_VFLG ((tmp_newv & 0x80) != 0 && (newv & 0x80) == 0);\n");
	genastore ("newv", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_ADD:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	start_brace ();
	genflags (flag_add, curi->size, "newv", "src", "dst");
	genastore ("newv", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_ADDA:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", sz_long, "dst", 1, 0);
	start_brace ();
	printf ("\tuae_u32 newv = dst + src;\n");
	genastore ("newv", curi->dmode, "dstreg", sz_long, "dst");
	break;
    case i_ADDX:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	start_brace ();
	printf ("\tuae_u32 newv = dst + src + (XGET_XFLG ? 1 : 0);\n");
	genflags (flag_addx, curi->size, "newv", "src", "dst");
	genflags (flag_zn, curi->size, "newv", "", "");
	genastore ("newv", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_ABCD:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	start_brace ();
	printf ("\tuae_u16 newv_lo = (src & 0xF) + (dst & 0xF) + (XGET_XFLG ? 1 : 0);\n");
	printf ("\tuae_u16 newv_hi = (src & 0xF0) + (dst & 0xF0);\n");
	printf ("\tuae_u16 newv, tmp_newv;\n");
	printf ("\tint cflg;\n");
	printf ("\tnewv = tmp_newv = newv_hi + newv_lo;");
	printf ("\tif (newv_lo > 9) { newv += 6; }\n");
	printf ("\tcflg = (newv & 0x3F0) > 0x90;\n");
	printf ("\tif (cflg) newv += 0x60;\n");
	printf ("\tXSET_CFLG (cflg);\n");
	duplicate_carry ();
	genflags (flag_zn, curi->size, "newv", "", "");
	printf ("\tXSET_VFLG ((tmp_newv & 0x80) == 0 && (newv & 0x80) != 0);\n");
	genastore ("newv", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_NEG:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	start_brace ();
	genflags (flag_sub, curi->size, "dst", "src", "0");
	genastore ("dst", curi->smode, "srcreg", curi->size, "src");
	break;
    case i_NEGX:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	start_brace ();
	printf ("\tuae_u32 newv = 0 - src - (XGET_XFLG ? 1 : 0);\n");
	genflags (flag_subx, curi->size, "newv", "src", "0");
	genflags (flag_zn, curi->size, "newv", "", "");
	genastore ("newv", curi->smode, "srcreg", curi->size, "src");
	break;
    case i_NBCD:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	start_brace ();
	printf ("\tuae_u16 newv_lo = - (src & 0xF) - (XGET_XFLG ? 1 : 0);\n");
	printf ("\tuae_u16 newv_hi = - (src & 0xF0);\n");
	printf ("\tuae_u16 newv;\n");
	printf ("\tint cflg;\n");
	printf ("\tif (newv_lo > 9) { newv_lo -= 6; }\n");
	printf ("\tnewv = newv_hi + newv_lo;");
	printf ("\tcflg = (newv & 0x1F0) > 0x90;\n");
	printf ("\tif (cflg) newv -= 0x60;\n");
	printf ("\tXSET_CFLG (cflg);\n");
	duplicate_carry();
	genflags (flag_zn, curi->size, "newv", "", "");
	genastore ("newv", curi->smode, "srcreg", curi->size, "src");
	break;
    case i_CLR:
	genamode (curi->smode, "srcreg", curi->size, "src", 2, 0);
	genflags (flag_logical, curi->size, "0", "", "");
	genastore ("0", curi->smode, "srcreg", curi->size, "src");
	break;
    case i_NOT:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	start_brace ();
	printf ("\tuae_u32 dst = ~src;\n");
	genflags (flag_logical, curi->size, "dst", "", "");
	genastore ("dst", curi->smode, "srcreg", curi->size, "src");
	break;
    case i_TST:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genflags (flag_logical, curi->size, "src", "", "");
	break;
    case i_BTST:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	if (curi->size == sz_byte)
	    printf ("\tsrc &= 7;\n");
	else
	    printf ("\tsrc &= 31;\n");
	printf ("\tXSET_ZFLG (1 ^ ((dst >> src) & 1));\n");
	break;
    case i_BCHG:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	if (curi->size == sz_byte)
	    printf ("\tsrc &= 7;\n");
	else
	    printf ("\tsrc &= 31;\n");
	printf ("\tdst ^= (1 << src);\n");
	printf ("\tXSET_ZFLG (((uae_u32)dst & (1 << src)) >> src);\n");
	genastore ("dst", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_BCLR:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	if (curi->size == sz_byte)
	    printf ("\tsrc &= 7;\n");
	else
	    printf ("\tsrc &= 31;\n");
	printf ("\tXSET_ZFLG (1 ^ ((dst >> src) & 1));\n");
	printf ("\tdst &= ~(1 << src);\n");
	genastore ("dst", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_BSET:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	if (curi->size == sz_byte)
	    printf ("\tsrc &= 7;\n");
	else
	    printf ("\tsrc &= 31;\n");
	printf ("\tXSET_ZFLG (1 ^ ((dst >> src) & 1));\n");
	printf ("\tdst |= (1 << src);\n");
	genastore ("dst", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_CMPM:
    case i_CMP:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	start_brace ();
	genflags (flag_cmp, curi->size, "newv", "src", "dst");
	break;
    case i_CMPA:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", sz_long, "dst", 1, 0);
	start_brace ();
	genflags (flag_cmp, sz_long, "newv", "src", "dst");
	break;
	/* The next two are coded a little unconventional, but they are doing
	 * weird things... */
    case i_MVPRM:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);

	printf ("\tuaecptr memp = xm68k_areg (dstreg) + (uae_s32)(uae_s16)%s;\n", gen_nextiword ());
	if (curi->size == sz_word) {
	    printf ("\txput_byte (memp, src >> 8); xput_byte (memp + 2, src);\n");
	} else {
	    printf ("\txput_byte (memp, src >> 24); xput_byte (memp + 2, src >> 16);\n");
	    printf ("\txput_byte (memp + 4, src >> 8); xput_byte (memp + 6, src);\n");
	}
	break;
    case i_MVPMR:
	printf ("\tuaecptr memp = xm68k_areg (srcreg) + (uae_s32)(uae_s16)%s;\n", gen_nextiword ());
	genamode (curi->dmode, "dstreg", curi->size, "dst", 2, 0);
	if (curi->size == sz_word) {
	    printf ("\tuae_u16 val = (xget_byte (memp) << 8) + xget_byte (memp + 2);\n");
	} else {
	    printf ("\tuae_u32 val = (xget_byte (memp) << 24) + (xget_byte (memp + 2) << 16)\n");
	    printf ("              + (xget_byte (memp + 4) << 8) + xget_byte (memp + 6);\n");
	}
	genastore ("val", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_MOVE:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 2, 0);
	genflags (flag_logical, curi->size, "src", "", "");
	genastore ("src", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_MOVEA:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 2, 0);
	if (curi->size == sz_word) {
	    printf ("\tuae_u32 val = (uae_s32)(uae_s16)src;\n");
	} else {
	    printf ("\tuae_u32 val = src;\n");
	}
	genastore ("val", curi->dmode, "dstreg", sz_long, "dst");
	break;
    case i_SWAP:
	genamode (curi->smode, "srcreg", sz_long, "src", 1, 0);
	start_brace ();
	printf ("\tuae_u32 dst = ((src >> 16)&0xFFFF) | ((src&0xFFFF)<<16);\n");
	genflags (flag_logical, sz_long, "dst", "", "");
	genastore ("dst", curi->smode, "srcreg", sz_long, "src");
	break;
    case i_EXG:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 1, 0);
	genastore ("dst", curi->smode, "srcreg", curi->size, "src");
	genastore ("src", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_EXT:
	genamode (curi->smode, "srcreg", sz_long, "src", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 dst = (uae_s32)(uae_s8)src;\n"); break;
	case sz_word: printf ("\tuae_u16 dst = (uae_s16)(uae_s8)src;\n"); break;
	case sz_long: printf ("\tuae_u32 dst = (uae_s32)(uae_s16)src;\n"); break;
	default: abort ();
	}
	genflags (flag_logical,
		  curi->size == sz_word ? sz_word : sz_long, "dst", "", "");
	genastore ("dst", curi->smode, "srcreg",
		   curi->size == sz_word ? sz_word : sz_long, "src");
	break;
    case i_MVMEL:
	genmovemel (opcode);
	break;
    case i_MVMLE:
	genmovemle (opcode);
	break;
    case i_NOP:
	break;
    case i_LINK:
	genamode (Apdi, "7", sz_long, "old", 2, 0);
	genamode (curi->smode, "srcreg", sz_long, "src", 1, 0);
	genastore ("src", Apdi, "7", sz_long, "old");
	genastore ("xm68k_areg (7)", curi->smode, "srcreg", sz_long, "src");
	genamode (curi->dmode, "dstreg", curi->size, "offs", 1, 0);
	printf ("\txm68k_areg (7) += offs;\n");
	break;
    case i_UNLK:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	printf ("\txm68k_areg (7) = src;\n");
	genamode (Aipi, "7", sz_long, "old", 1, 0);
	genastore ("old", curi->smode, "srcreg", curi->size, "src");
	break;
    case i_RTS:
	printf ("\txm68k_setpc (xget_long (xm68k_areg (7)));\n");
	printf ("\txm68k_areg (7) += 4;\n");
	m68k_pc_offset = 0;
	break;
    case i_JSR:
	genamode (curi->smode, "srcreg", curi->size, "src", 0, 0);
	printf ("\txm68k_areg (7) -= 4;\n");
	printf ("\txput_long (xm68k_areg (7), xm68k_getpc () + %d);\n", m68k_pc_offset);
	printf ("\txm68k_setpc (srca);\n");
	m68k_pc_offset = 0;
	break;
    case i_JMP:
	genamode (curi->smode, "srcreg", curi->size, "src", 0, 0);
	printf ("\txm68k_setpc (srca);\n");
	m68k_pc_offset = 0;
	break;
    case i_BSR:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	printf ("\tuae_s32 s = (uae_s32)src + 2;\n");
	printf ("\txm68k_areg (7) -= 4;\n");
	printf ("\txput_long (xm68k_areg (7), xm68k_getpc () + %d);\n", m68k_pc_offset);
	printf ("\txm68k_incpc(s);\n");
	m68k_pc_offset = 0;
	break;
    case i_Bcc:
	if (curi->size == sz_long) {
	    if (next_cpu_level < 1)
		next_cpu_level = 1;
	}
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	printf ("\tif (!xcctrue(%d)) goto didnt_jump;\n", curi->cc);
	printf ("\txm68k_incpc ((uae_s32)src + 2);\n");
	printf ("\treturn;\n");
	printf ("didnt_jump:;\n");
	need_endlabel = 1;
	break;
    case i_LEA:
	genamode (curi->smode, "srcreg", curi->size, "src", 0, 0);
	genamode (curi->dmode, "dstreg", curi->size, "dst", 2, 0);
	genastore ("srca", curi->dmode, "dstreg", curi->size, "dst");
	break;
    case i_PEA:
	genamode (curi->smode, "srcreg", curi->size, "src", 0, 0);
	genamode (Apdi, "7", sz_long, "dst", 2, 0);
	genastore ("srca", Apdi, "7", sz_long, "dst");
	break;
    case i_DBcc:
	genamode (curi->smode, "srcreg", curi->size, "src", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "offs", 1, 0);
	printf ("\tif (!xcctrue(%d)) {\n", curi->cc);
	genastore ("(src-1)", curi->smode, "srcreg", curi->size, "src");
	printf ("\t\tif (src) {\n");
	printf ("\t\t\txm68k_incpc((uae_s32)offs + 2);\n");
	printf ("\t\t}\n");
	printf ("\t}\n");
	need_endlabel = 1;
	break;
    case i_Scc:
	genamode (curi->smode, "srcreg", curi->size, "src", 2, 0);
	start_brace ();
	printf ("\tint val = xcctrue(%d) ? 0xff : 0;\n", curi->cc);
	genastore ("val", curi->smode, "srcreg", curi->size, "src");
	break;
    case i_DIVU:
	printf ("\tuaecptr oldpc = xm68k_getpc ();\n");
	genamode (curi->smode, "srcreg", sz_word, "src", 1, 0);
	genamode (curi->dmode, "dstreg", sz_long, "dst", 1, 0);
	sync_m68k_pc ();
	start_brace ();
	/* Clear V flag when dividing by zero - Alcatraz Odyssey demo depends
	 * on this (actually, it's doing a DIVS).  */
	printf ("\tuae_u32 newv = (uae_u32)dst / (uae_u32)(uae_u16)src;\n");
	printf ("\tuae_u32 rem = (uae_u32)dst %% (uae_u32)(uae_u16)src;\n");
	/* The N flag appears to be set each time there is an overflow.
	 * Weird. */
	printf ("\tif (newv > 0xffff) { XSET_VFLG (1); XSET_NFLG (1); XSET_CFLG (0); } else\n\t{\n");
	genflags (flag_logical, sz_word, "newv", "", "");
	printf ("\tnewv = (newv & 0xffff) | ((uae_u32)rem << 16);\n");
	genastore ("newv", curi->dmode, "dstreg", sz_long, "dst");
	printf ("\t}\n");
	need_endlabel = 1;
	break;
    case i_DIVS:
	printf ("\tuaecptr oldpc = xm68k_getpc ();\n");
	genamode (curi->smode, "srcreg", sz_word, "src", 1, 0);
	genamode (curi->dmode, "dstreg", sz_long, "dst", 1, 0);
	sync_m68k_pc ();
	start_brace ();
	printf ("\tuae_s32 newv = (uae_s32)dst / (uae_s32)(uae_s16)src;\n");
	printf ("\tuae_u16 rem = (uae_s32)dst %% (uae_s32)(uae_s16)src;\n");
	printf ("\tif ((newv & 0xffff8000) != 0 && (newv & 0xffff8000) != 0xffff8000) { XSET_VFLG (1); XSET_NFLG (1); XSET_CFLG (0); } else\n\t{\n");
	printf ("\tif (((uae_s16)rem < 0) != ((uae_s32)dst < 0)) rem = -rem;\n");
	genflags (flag_logical, sz_word, "newv", "", "");
	printf ("\tnewv = (newv & 0xffff) | ((uae_u32)rem << 16);\n");
	genastore ("newv", curi->dmode, "dstreg", sz_long, "dst");
	printf ("\t}\n");
	need_endlabel = 1;
	break;
    case i_MULU:
	genamode (curi->smode, "srcreg", sz_word, "src", 1, 0);
	genamode (curi->dmode, "dstreg", sz_word, "dst", 1, 0);
	start_brace ();
	printf ("\tuae_u32 newv = (uae_u32)(uae_u16)dst * (uae_u32)(uae_u16)src;\n");
	genflags (flag_logical, sz_long, "newv", "", "");
	genastore ("newv", curi->dmode, "dstreg", sz_long, "dst");
	break;
    case i_MULS:
	genamode (curi->smode, "srcreg", sz_word, "src", 1, 0);
	genamode (curi->dmode, "dstreg", sz_word, "dst", 1, 0);
	start_brace ();
	printf ("\tuae_u32 newv = (uae_s32)(uae_s16)dst * (uae_s32)(uae_s16)src;\n");
	genflags (flag_logical, sz_long, "newv", "", "");
	genastore ("newv", curi->dmode, "dstreg", sz_long, "dst");
	break;
    case i_ASR:
	genamode (curi->smode, "srcreg", curi->size, "cnt", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 val = (uae_u8)data;\n"); break;
	case sz_word: printf ("\tuae_u32 val = (uae_u16)data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tuae_u32 sign = (%s & val) >> %d;\n", cmask (curi->size), bit_size (curi->size) - 1);
	printf ("\tcnt &= 63;\n");
	printf ("\tXCLEAR_CZNV;\n");
	printf ("\tif (cnt >= %d) {\n", bit_size (curi->size));
	printf ("\t\tval = %s & (uae_u32)-sign;\n", bit_mask (curi->size));
	printf ("\t\tXSET_CFLG (sign);\n");
	duplicate_carry ();
	if (source_is_imm1_8 (curi))
	    printf ("\t} else {\n");
	else
	    printf ("\t} else if (cnt > 0) {\n");
	printf ("\t\tval >>= cnt - 1;\n");
	printf ("\t\tXSET_CFLG (val & 1);\n");
	duplicate_carry ();
	printf ("\t\tval >>= 1;\n");
	printf ("\t\tval |= (%s << (%d - cnt)) & (uae_u32)-sign;\n",
		bit_mask (curi->size),
		bit_size (curi->size));
	printf ("\t\tval &= %s;\n", bit_mask (curi->size));
	printf ("\t}\n");
	genflags (flag_logical_noclobber, curi->size, "val", "", "");
	genastore ("val", curi->dmode, "dstreg", curi->size, "data");
	break;
    case i_ASL:
	genamode (curi->smode, "srcreg", curi->size, "cnt", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 val = (uae_u8)data;\n"); break;
	case sz_word: printf ("\tuae_u32 val = (uae_u16)data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tcnt &= 63;\n");
	printf ("\tXCLEAR_CZNV;\n");
	printf ("\tif (cnt >= %d) {\n", bit_size (curi->size));
	printf ("\t\tXSET_VFLG (val != 0);\n");
	printf ("\t\tXSET_CFLG (cnt == %d ? val & 1 : 0);\n",
		bit_size (curi->size));
	duplicate_carry ();
	printf ("\t\tval = 0;\n");
	if (source_is_imm1_8 (curi))
	    printf ("\t} else {\n");
	else
	    printf ("\t} else if (cnt > 0) {\n");
	printf ("\t\tuae_u32 mask = (%s << (%d - cnt)) & %s;\n",
		bit_mask (curi->size),
		bit_size (curi->size) - 1,
		bit_mask (curi->size));
	printf ("\t\tXSET_VFLG ((val & mask) != mask && (val & mask) != 0);\n");
	printf ("\t\tval <<= cnt - 1;\n");
	printf ("\t\tXSET_CFLG ((val & %s) >> %d);\n", cmask (curi->size), bit_size (curi->size) - 1);
	duplicate_carry ();
	printf ("\t\tval <<= 1;\n");
	printf ("\t\tval &= %s;\n", bit_mask (curi->size));
	printf ("\t}\n");
	genflags (flag_logical_noclobber, curi->size, "val", "", "");
	genastore ("val", curi->dmode, "dstreg", curi->size, "data");
	break;
    case i_LSR:
	genamode (curi->smode, "srcreg", curi->size, "cnt", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 val = (uae_u8)data;\n"); break;
	case sz_word: printf ("\tuae_u32 val = (uae_u16)data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tcnt &= 63;\n");
	printf ("\tXCLEAR_CZNV;\n");
	printf ("\tif (cnt >= %d) {\n", bit_size (curi->size));
	printf ("\t\tXSET_CFLG ((cnt == %d) & (val >> %d));\n",
		bit_size (curi->size), bit_size (curi->size) - 1);
	duplicate_carry ();
	printf ("\t\tval = 0;\n");
	if (source_is_imm1_8 (curi))
	    printf ("\t} else {\n");
	else
	    printf ("\t} else if (cnt > 0) {\n");
	printf ("\t\tval >>= cnt - 1;\n");
	printf ("\t\tXSET_CFLG (val & 1);\n");
	duplicate_carry ();
	printf ("\t\tval >>= 1;\n");
	printf ("\t}\n");
	genflags (flag_logical_noclobber, curi->size, "val", "", "");
	genastore ("val", curi->dmode, "dstreg", curi->size, "data");
	break;
    case i_LSL:
	genamode (curi->smode, "srcreg", curi->size, "cnt", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 val = (uae_u8)data;\n"); break;
	case sz_word: printf ("\tuae_u32 val = (uae_u16)data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tcnt &= 63;\n");
	printf ("\tXCLEAR_CZNV;\n");
	printf ("\tif (cnt >= %d) {\n", bit_size (curi->size));
	printf ("\t\tXSET_CFLG (cnt == %d ? val & 1 : 0);\n",
		bit_size (curi->size));
	duplicate_carry ();
	printf ("\t\tval = 0;\n");
	if (source_is_imm1_8 (curi))
	    printf ("\t} else {\n");
	else
	    printf ("\t} else if (cnt > 0) {\n");
	printf ("\t\tval <<= (cnt - 1);\n");
	printf ("\t\tXSET_CFLG ((val & %s) >> %d);\n", cmask (curi->size), bit_size (curi->size) - 1);
	duplicate_carry ();
	printf ("\t\tval <<= 1;\n");
	printf ("\tval &= %s;\n", bit_mask (curi->size));
	printf ("\t}\n");
	genflags (flag_logical_noclobber, curi->size, "val", "", "");
	genastore ("val", curi->dmode, "dstreg", curi->size, "data");
	break;
    case i_ROL:
	genamode (curi->smode, "srcreg", curi->size, "cnt", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 val = (uae_u8)data;\n"); break;
	case sz_word: printf ("\tuae_u32 val = (uae_u16)data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tcnt &= 63;\n");
	printf ("\tXCLEAR_CZNV;\n");
	if (source_is_imm1_8 (curi))
	    printf ("{");
	else
	    printf ("\tif (cnt > 0) {\n");
	printf ("\tuae_u32 loval;\n");
	printf ("\tcnt &= %d;\n", bit_size (curi->size) - 1);
	printf ("\tloval = val >> (%d - cnt);\n", bit_size (curi->size));
	printf ("\tval <<= cnt;\n");
	printf ("\tval |= loval;\n");
	printf ("\tval &= %s;\n", bit_mask (curi->size));
	printf ("\tXSET_CFLG (val & 1);\n");
	printf ("}\n");
	genflags (flag_logical_noclobber, curi->size, "val", "", "");
	genastore ("val", curi->dmode, "dstreg", curi->size, "data");
	break;
    case i_ROR:
	genamode (curi->smode, "srcreg", curi->size, "cnt", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 val = (uae_u8)data;\n"); break;
	case sz_word: printf ("\tuae_u32 val = (uae_u16)data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tcnt &= 63;\n");
	printf ("\tXCLEAR_CZNV;\n");
	if (source_is_imm1_8 (curi))
	    printf ("{");
	else
	    printf ("\tif (cnt > 0) {");
	printf ("\tuae_u32 hival;\n");
	printf ("\tcnt &= %d;\n", bit_size (curi->size) - 1);
	printf ("\thival = val << (%d - cnt);\n", bit_size (curi->size));
	printf ("\tval >>= cnt;\n");
	printf ("\tval |= hival;\n");
	printf ("\tval &= %s;\n", bit_mask (curi->size));
	printf ("\tXSET_CFLG ((val & %s) >> %d);\n", cmask (curi->size), bit_size (curi->size) - 1);
	printf ("\t}\n");
	genflags (flag_logical_noclobber, curi->size, "val", "", "");
	genastore ("val", curi->dmode, "dstreg", curi->size, "data");
	break;
    case i_ROXL:
	genamode (curi->smode, "srcreg", curi->size, "cnt", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 val = (uae_u8)data;\n"); break;
	case sz_word: printf ("\tuae_u32 val = (uae_u16)data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tcnt &= 63;\n");
	printf ("\tXCLEAR_CZNV;\n");
	if (source_is_imm1_8 (curi))
	    printf ("{");
	else {
	    force_range_for_rox ("cnt", curi->size);
	    printf ("\tif (cnt > 0) {\n");
	}
	printf ("\tcnt--;\n");
	printf ("\t{\n\tuae_u32 carry;\n");
	printf ("\tuae_u32 loval = val >> (%d - cnt);\n", bit_size (curi->size) - 1);
	printf ("\tcarry = loval & 1;\n");
	printf ("\tval = (((val << 1) | XGET_XFLG) << cnt) | (loval >> 1);\n");
	printf ("\tXSET_XFLG (carry);\n");
	printf ("\tval &= %s;\n", bit_mask (curi->size));
	printf ("\t} }\n");
	printf ("\tXSET_CFLG (XGET_XFLG);\n");
	genflags (flag_logical_noclobber, curi->size, "val", "", "");
	genastore ("val", curi->dmode, "dstreg", curi->size, "data");
	break;
    case i_ROXR:
	genamode (curi->smode, "srcreg", curi->size, "cnt", 1, 0);
	genamode (curi->dmode, "dstreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 val = (uae_u8)data;\n"); break;
	case sz_word: printf ("\tuae_u32 val = (uae_u16)data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tcnt &= 63;\n");
	printf ("\tXCLEAR_CZNV;\n");
	if (source_is_imm1_8 (curi))
	    printf ("{");
	else {
	    force_range_for_rox ("cnt", curi->size);
	    printf ("\tif (cnt > 0) {\n");
	}
	printf ("\tcnt--;\n");
	printf ("\t{\n\tuae_u32 carry;\n");
	printf ("\tuae_u32 hival = (val << 1) | XGET_XFLG;\n");
	printf ("\thival <<= (%d - cnt);\n", bit_size (curi->size) - 1);
	printf ("\tval >>= cnt;\n");
	printf ("\tcarry = val & 1;\n");
	printf ("\tval >>= 1;\n");
	printf ("\tval |= hival;\n");
	printf ("\tXSET_XFLG (carry);\n");
	printf ("\tval &= %s;\n", bit_mask (curi->size));
	printf ("\t} }\n");
	printf ("\tXSET_CFLG (XGET_XFLG);\n");
	genflags (flag_logical_noclobber, curi->size, "val", "", "");
	genastore ("val", curi->dmode, "dstreg", curi->size, "data");
	break;
    case i_ASRW:
	genamode (curi->smode, "srcreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 val = (uae_u8)data;\n"); break;
	case sz_word: printf ("\tuae_u32 val = (uae_u16)data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tuae_u32 sign = %s & val;\n", cmask (curi->size));
	printf ("\tuae_u32 cflg = val & 1;\n");
	printf ("\tval = (val >> 1) | sign;\n");
	genflags (flag_logical, curi->size, "val", "", "");
	printf ("\tXSET_CFLG (cflg);\n");
	duplicate_carry ();
	genastore ("val", curi->smode, "srcreg", curi->size, "data");
	break;
    case i_ASLW:
	genamode (curi->smode, "srcreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 val = (uae_u8)data;\n"); break;
	case sz_word: printf ("\tuae_u32 val = (uae_u16)data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tuae_u32 sign = %s & val;\n", cmask (curi->size));
	printf ("\tuae_u32 sign2;\n");
	printf ("\tval <<= 1;\n");
	genflags (flag_logical, curi->size, "val", "", "");
	printf ("\tsign2 = %s & val;\n", cmask (curi->size));
	printf ("\tXSET_CFLG (sign != 0);\n");
	duplicate_carry ();

	printf ("\tXSET_VFLG (XGET_VFLG | (sign2 != sign));\n");
	genastore ("val", curi->smode, "srcreg", curi->size, "data");
	break;
    case i_LSRW:
	genamode (curi->smode, "srcreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u32 val = (uae_u8)data;\n"); break;
	case sz_word: printf ("\tuae_u32 val = (uae_u16)data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tuae_u32 carry = val & 1;\n");
	printf ("\tval >>= 1;\n");
	genflags (flag_logical, curi->size, "val", "", "");
	printf ("XSET_CFLG (carry);\n");
	duplicate_carry ();
	genastore ("val", curi->smode, "srcreg", curi->size, "data");
	break;
    case i_LSLW:
	genamode (curi->smode, "srcreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u8 val = data;\n"); break;
	case sz_word: printf ("\tuae_u16 val = data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tuae_u32 carry = val & %s;\n", cmask (curi->size));
	printf ("\tval <<= 1;\n");
	genflags (flag_logical, curi->size, "val", "", "");
	printf ("XSET_CFLG (carry >> %d);\n", bit_size (curi->size) - 1);
	duplicate_carry ();
	genastore ("val", curi->smode, "srcreg", curi->size, "data");
	break;
    case i_ROLW:
	genamode (curi->smode, "srcreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u8 val = data;\n"); break;
	case sz_word: printf ("\tuae_u16 val = data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tuae_u32 carry = val & %s;\n", cmask (curi->size));
	printf ("\tval <<= 1;\n");
	printf ("\tif (carry)  val |= 1;\n");
	genflags (flag_logical, curi->size, "val", "", "");
	printf ("XSET_CFLG (carry >> %d);\n", bit_size (curi->size) - 1);
	genastore ("val", curi->smode, "srcreg", curi->size, "data");
	break;
    case i_RORW:
	genamode (curi->smode, "srcreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u8 val = data;\n"); break;
	case sz_word: printf ("\tuae_u16 val = data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tuae_u32 carry = val & 1;\n");
	printf ("\tval >>= 1;\n");
	printf ("\tif (carry) val |= %s;\n", cmask (curi->size));
	genflags (flag_logical, curi->size, "val", "", "");
	printf ("XSET_CFLG (carry);\n");
	genastore ("val", curi->smode, "srcreg", curi->size, "data");
	break;
    case i_ROXLW:
	genamode (curi->smode, "srcreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u8 val = data;\n"); break;
	case sz_word: printf ("\tuae_u16 val = data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tuae_u32 carry = val & %s;\n", cmask (curi->size));
	printf ("\tval <<= 1;\n");
	printf ("\tif (XGET_XFLG) val |= 1;\n");
	genflags (flag_logical, curi->size, "val", "", "");
	printf ("XSET_CFLG (carry >> %d);\n", bit_size (curi->size) - 1);
	duplicate_carry ();
	genastore ("val", curi->smode, "srcreg", curi->size, "data");
	break;
    case i_ROXRW:
	genamode (curi->smode, "srcreg", curi->size, "data", 1, 0);
	start_brace ();
	switch (curi->size) {
	case sz_byte: printf ("\tuae_u8 val = data;\n"); break;
	case sz_word: printf ("\tuae_u16 val = data;\n"); break;
	case sz_long: printf ("\tuae_u32 val = data;\n"); break;
	default: abort ();
	}
	printf ("\tuae_u32 carry = val & 1;\n");
	printf ("\tval >>= 1;\n");
	printf ("\tif (XGET_XFLG) val |= %s;\n", cmask (curi->size));
	genflags (flag_logical, curi->size, "val", "", "");
	printf ("XSET_CFLG (carry);\n");
	duplicate_carry ();
	genastore ("val", curi->smode, "srcreg", curi->size, "data");
	break;
    default:
	m68k_pc_offset = 0;
	break;
    }
    finish_braces ();
    sync_m68k_pc ();
}

static int postfix;

static void generate_one_opcode (int rp)
{
    int i;
    uae_u16 smsk, dmsk;
    long int opcode = opcode_map[rp];

    if (table68k[opcode].mnemo == i_ILLG
	|| table68k[opcode].clev > cpu_level)
	return;

    for (i = 0; lookuptab[i].name[0]; i++) {
	if (table68k[opcode].mnemo == lookuptab[i].mnemo)
	    break;
    }

    if (table68k[opcode].handler != -1)
	return;

    if (opcode_next_clev[rp] != cpu_level) {
	fprintf (stblfile, "{ xop_%lx_%d, %ld }, /* %s */\n", opcode, opcode_last_postfix[rp],
		 opcode, lookuptab[i].name);
	return;
    }
    fprintf (stblfile, "{ xop_%lx_%d, %ld }, /* %s */\n", opcode, postfix, opcode, lookuptab[i].name);
    fprintf (headerfile, "extern xcpuop_func xop_%lx_%d;\n", opcode, postfix);
    printf ("void xop_%lx_%d(uae_u32 opcode) /* %s */\n{\n", opcode, postfix, lookuptab[i].name);

    switch (table68k[opcode].stype) {
    case 0: smsk = 7; break;
    case 1: smsk = 255; break;
    case 2: smsk = 15; break;
    case 3: smsk = 7; break;
    case 4: smsk = 7; break;
    case 5: smsk = 63; break;
    case 7: smsk = 3; break;
    default: abort ();
    }
    dmsk = 7;

    next_cpu_level = -1;
    if (table68k[opcode].suse
	&& table68k[opcode].smode != imm && table68k[opcode].smode != imm0
	&& table68k[opcode].smode != imm1 && table68k[opcode].smode != imm2
	&& table68k[opcode].smode != absw && table68k[opcode].smode != absl
	&& table68k[opcode].smode != PC8r && table68k[opcode].smode != PC16)
    {
	if (table68k[opcode].spos == -1) {
	    if (((int) table68k[opcode].sreg) >= 128)
		printf ("\tuae_u32 srcreg = (uae_s32)(uae_s8)%d;\n", (int) table68k[opcode].sreg);
	    else
		printf ("\tuae_u32 srcreg = %d;\n", (int) table68k[opcode].sreg);
	} else {
	    char source[100];
	    int pos = table68k[opcode].spos;

	    if (pos)
		sprintf (source, "((opcode >> %d) & %d)", pos, smsk);
	    else
		sprintf (source, "(opcode & %d)", smsk);

	    if (table68k[opcode].stype == 3)
		printf ("\tuae_u32 srcreg = ximm8_table[%s];\n", source);
	    else if (table68k[opcode].stype == 1)
		printf ("\tuae_u32 srcreg = (uae_s32)(uae_s8)%s;\n", source);
	    else
		printf ("\tuae_u32 srcreg = %s;\n", source);
	}
    }
    if (table68k[opcode].duse
	/* Yes, the dmode can be imm, in case of LINK or DBcc */
	&& table68k[opcode].dmode != imm && table68k[opcode].dmode != imm0
	&& table68k[opcode].dmode != imm1 && table68k[opcode].dmode != imm2
	&& table68k[opcode].dmode != absw && table68k[opcode].dmode != absl)
    {
	if (table68k[opcode].dpos == -1) {
	    if (((int) table68k[opcode].dreg) >= 128)
		printf ("\tuae_u32 dstreg = (uae_s32)(uae_s8)%d;\n", (int) table68k[opcode].dreg);
	    else
		printf ("\tuae_u32 dstreg = %d;\n", (int) table68k[opcode].dreg);
	} else {
	    int pos = table68k[opcode].dpos;
#if 0
	    /* Check that we can do the little endian optimization safely.  */
	    if (pos < 8 && (dmsk >> (8 - pos)) != 0)
		abort ();
#endif
	    if (pos)
		printf ("\tuae_u32 dstreg = (opcode >> %d) & %d;\n",
			pos, dmsk);
	    else
		printf ("\tuae_u32 dstreg = opcode & %d;\n", dmsk);
	}
    }
    need_endlabel = 0;
    endlabelno++;
    sprintf (endlabelstr, "endlabel%d", endlabelno);
    gen_opcode (opcode);
    if (need_endlabel)
	printf ("%s: ;\n", endlabelstr);
    printf ("}\n");
    opcode_next_clev[rp] = next_cpu_level;
    opcode_last_postfix[rp] = postfix;
}

static void generate_func (void)
{
    int i, j, rp;

    using_prefetch = 0;
    using_exception_3 = 0;
    for (i = 0; i <= 0; i++) {
	cpu_level = 5;

	postfix = i;
	fprintf (stblfile, "struct xcputbl xop_smalltbl_%d[] = {\n", postfix);

	rp = 0;
	for(j=1;j<=8;++j) {
		int k = (j*nr_cpuop_funcs)/8;
		for (; rp < k; rp++)
		   generate_one_opcode (rp);
	}

	fprintf (stblfile, "{ 0, 0 }};\n");
    }

}

static void generate_includes (FILE * f)
{
    fprintf (f, "#include \"cpu_small.h\"\n");
    fprintf (f, "#include \"cputbl_small.h\"\n");
}

int main (int argc, char **argv)
{
    read_table68k ();
    do_merges ();

    opcode_map = (int *) xmalloc (sizeof (int) * nr_cpuop_funcs);
    opcode_last_postfix = (int *) xmalloc (sizeof (int) * nr_cpuop_funcs);
    opcode_next_clev = (int *) xmalloc (sizeof (int) * nr_cpuop_funcs);
    counts = (unsigned long *) xmalloc (65536 * sizeof (unsigned long));
    read_counts ();

    /* It would be a lot nicer to put all in one file (we'd also get rid of
     * cputbl.h that way), but cpuopti can't cope.  That could be fixed, but
     * I don't dare to touch the 68k version.  */

    headerfile = fopen ("cputbl_small.h", "wb");
    stblfile = fopen ("cpustbl_small.c", "wb");
    generate_includes (stblfile);
    freopen ("cpuemu_small.c", "wb", stdout);
    generate_includes (stdout);

    generate_func ();

    free (table68k);
    return 0;
}
