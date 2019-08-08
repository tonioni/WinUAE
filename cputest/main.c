
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <ctype.h>
#include <math.h>


#ifdef _MSC_VER
#include "msc_dirent.h"
#else
#include <dirent.h>
#endif

#define DONTSTOPONERROR 0

typedef unsigned int uae_u32;
typedef int uae_s32;
typedef unsigned short uae_u16;
typedef short uae_s16;
typedef unsigned char uae_u8;
typedef signed char uae_s8;

#include "cputest_defines.h"

struct fpureg
{
	uae_u16 exp;
	uae_u16 dummy;
	uae_u32 m[2];
};

// must match asm.S
struct registers
{
	uae_u32 regs[16];
	uae_u32 ssp;
	uae_u32 msp;
	uae_u32 pc;
	uae_u32 sr;
	uae_u32 exc;
	uae_u32 excframe;
	struct fpureg fpuregs[8];
	uae_u32 fpiar, fpcr, fpsr;
};

static struct registers test_regs;
static struct registers last_registers;
static struct registers regs;
static uae_u8 *opcode_memory;
static uae_u32 opcode_memory_addr;
static uae_u8 *low_memory;
static uae_u8 *high_memory;
static uae_u8 *test_memory;
static uae_u32 test_memory_addr;
static uae_u32 test_memory_size;
static uae_u8 *test_data;
static int test_data_size;
static uae_u32 oldvbr;
static uae_u8 *vbr_zero = 0;
static int hmem_rom, lmem_rom;
static uae_u8 *absallocated;
static int cpu_lvl, fpu_model;
static uae_u16 sr_undefined_mask;
static int check_undefined_sr;
static uae_u32 cpustatearraystore[16];
static uae_u32 cpustatearraynew[] = {
	0x00000005, // SFC
	0x00000005, // DFC
	0x00000009, // CACR
	0x00000000, // CAAR
	0x00000000, // MSP
};

static uae_u8 low_memory_temp[32768];
static uae_u8 high_memory_temp[32768];
static uae_u8 low_memory_back[32768];
static uae_u8 high_memory_back[32768];

static uae_u32 vbr[256];
	
static char inst_name[16+1];
#ifdef _MSC_VER
static char outbuffer[40000];
#else
static char outbuffer[4000];
#endif
static char *outbp;
static int infoadded;
static int errors;
static int testcnt;
static int dooutput = 1;
static int quit;
static uae_u8 ccr_mask;
static uae_u32 addressing_mask = 0x00ffffff;

#ifdef _MSC_VER

#define xmemcpy memcpy

static uae_u8 *allocate_absolute(uae_u32 addr, uae_u32 size)
{
	return calloc(1, size);
}
static void free_absolute(uae_u32 addr, uae_u32 size)
{
}
static void execute_test000(struct registers *regs)
{
}
static void execute_test010(struct registers *regs)
{
}
static void execute_test020(struct registers *regs)
{
}
static void execute_testfpu(struct registers *regs)
{
}
static uae_u32 tosuper(uae_u32 v)
{
	return 0;
}
static void touser(uae_u32 v)
{
}
static uae_u32 exceptiontable000, exception010, exception020, exceptionfpu;
static uae_u32 testexit(void)
{
	return 0;
}
static uae_u32 setvbr(uae_u32 v)
{
	return 0;
}
static uae_u32 get_cpu_model(void)
{
	return 0;
}
static void setcpu(uae_u32 v, uae_u32 *s, uae_u32 *d)
{
}
static void flushcache(void)
{
}
#else

static void xmemcpy(void *d, void *s, int size)
{
	__builtin_memcpy(d, s, size);
}

extern uae_u8 *allocate_absolute(uae_u32, uae_u32);
extern void free_absolute(uae_u32, uae_u32);
extern void execute_test000(struct registers*);
extern void execute_test010(struct registers *);
extern void execute_test020(struct registers *);
extern void execute_testfpu(struct registers *);
extern uae_u32 tosuper(uae_u32);
extern void touser(uae_u32);
extern uae_u32 exceptiontable000, exception010, exception020, exceptionfpu;
extern uae_u32 testexit(void);
extern uae_u32 setvbr(uae_u32);
extern uae_u32 get_cpu_model(void);
extern void setcpu(uae_u32, uae_u32*, uae_u32*);
extern void flushcache(void);

#endif

struct accesshistory
{
	uae_u8 *addr;
	uae_u32 val;
	uae_u32 oldval;
	int size;
};
static int ahcnt;

#define MAX_ACCESSHIST 8
static struct accesshistory ahist[MAX_ACCESSHIST];

static void endinfo(void)
{
	printf("Last test: %lu\n", testcnt);
	uae_u8 *p = opcode_memory;
	for (int i = 0; i < 32 * 2; i += 2) {
		uae_u16 v = (p[i] << 8) | (p[i + 1]);
		if (v == 0x4afc && i > 0)
			break;
		printf(" %04x", v);
	}
	printf("\n");
}

static int test_active;
static uae_u32 enable_data;

static void start_test(void)
{
	if (test_active)
		return;

#ifndef _MSC_VER
	if (lmem_rom > 0) {
		if (memcmp(low_memory, low_memory_temp, 32768)) {
			printf("Low memory ROM mismatch!\n");
			exit(0);
		}
	}
	if (hmem_rom > 0) {
		if (memcmp(high_memory, high_memory_temp, 32768)) {
			printf("High memory ROM mismatch!\n");
			exit(0);
		}
	}
#endif

	test_active = 1;

	enable_data = tosuper(0);

	memcpy(low_memory_back, low_memory, 32768);
	if (!hmem_rom)
		memcpy(high_memory_back, high_memory, 32768);

	memcpy(low_memory, low_memory_temp, 32768);
	if (!hmem_rom)
		memcpy(high_memory, high_memory_temp, 32768);

	if (cpu_lvl == 0) {
		uae_u32 *p = (uae_u32 *)vbr_zero;
		for (int i = 3; i < 12; i++) {
			p[i] = (uae_u32)(((uae_u32)&exceptiontable000) + (i - 3) * 2);
		}
		for (int i = 32; i < 48; i++) {
			p[i] = (uae_u32)(((uae_u32)&exceptiontable000) + (i - 3) * 2);
		}
	} else {
		oldvbr = setvbr((uae_u32)vbr);
		for (int i = 0; i < 256; i++) {
			vbr[i] = fpu_model ? (uae_u32)(&exceptionfpu) : (cpu_lvl == 1 ? (uae_u32)(&exception010) : (uae_u32)(&exception020));
		}
	}
	setcpu(cpu_lvl, cpustatearraynew, cpustatearraystore);
}

static void end_test(void)
{
	if (!test_active)
		return;
	test_active = 0;

	memcpy(low_memory, low_memory_back, 32768);
	if (!hmem_rom)
		memcpy(high_memory, high_memory_back, 32768);

	if (cpu_lvl > 0) {
		setvbr(oldvbr);
	}
	setcpu(cpu_lvl, cpustatearraystore, NULL);

	touser(enable_data);
}

static uae_u8 *load_file(const char *path, const char *file, uae_u8 *p, int *sizep)
{
	char fname[256];
	sprintf(fname, "%s%s", path, file);
	FILE *f = fopen(fname, "rb");
	if (!f) {
		printf("Couldn't open '%s'\n", fname);
		exit(0);
	}
	int size = *sizep;
	if (size < 0) {
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		fseek(f, 0, SEEK_SET);
	}
	if (!p) {
		p = calloc(1, size);
		if (!p) {
			printf("Couldn't allocate %ld bytes, file '%s'\n", size, fname);
			exit(0);
		}
	}
	*sizep = fread(p, 1, size, f);
	if (*sizep != size) {
		printf("Couldn't read file '%s'\n", fname);
		exit(0);
	}
	fclose(f);
	return p;
}

static void pl(uae_u8 *p, uae_u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >>  8;
	p[3] = v >>  0;
}
static uae_u32 gl(uae_u8 *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3] << 0);
}

static uae_u8 *restore_fpvalue(uae_u8 *p, struct fpureg *fp)
{
	uae_u8 v = *p++;
	if ((v & CT_SIZE_MASK) != CT_SIZE_FPU) {
		end_test();
		printf("Expected CT_SIZE_FPU, got %02x\n", v);
		endinfo();
		exit(0);
	}
	fp->exp = (p[0] << 8) | p[1];
	p += 2;
	fp->m[0] = gl(p);
	p += 4;
	fp->m[1] = gl(p);
	p += 4;
	return p;
}

static uae_u8 *restore_value(uae_u8 *p, uae_u32 *vp, int *sizep)
{
	uae_u32 val = *vp;
	uae_u8 v = *p++;
	switch(v & CT_SIZE_MASK)
	{
		case CT_SIZE_BYTE:
		val &= 0xffffff00;
		val |= *p++;
		*sizep = 0;
		break;
		case CT_SIZE_WORD:
		val &= 0xffff0000;
		val |= (*p++) << 8;
		val |= *p++;
		*sizep = 1;
		break;
		case CT_SIZE_LONG:
		val  = (*p++) << 24;
		val |= (*p++) << 16;
		val |= (*p++) << 8;
		val |= *p++;
		*sizep = 2;
		break;
		case CT_SIZE_FPU:
		end_test();
		printf("Unexpected CT_SIZE_FPU\n");
		endinfo();
		exit(0);
		break;
	}
	*vp = val;
	return p;
}

static uae_u8 *restore_rel(uae_u8 *p, uae_u32 *vp)
{
	uae_u32 v = *vp;
	switch ((*p++) & CT_SIZE_MASK)
	{
		case CT_RELATIVE_START_BYTE:
		{
			uae_u8 val;
			val = *p++;
			v += (uae_s8)val;
			break;
		}
		case CT_RELATIVE_START_WORD:
		{
			uae_u16 val;
			val = (*p++) << 8;
			val |= *p++;
			v += (uae_s16)val;
			break;
		}
		case CT_ABSOLUTE_WORD:
		{
			uae_u16 val;
			val = (*p++) << 8;
			val |= *p++;
			v = (uae_s32)(uae_s16)val;
			break;
		}
		case CT_ABSOLUTE_LONG:
		{
			uae_u32 val;
			val = (*p++) << 24;
			val |= (*p++) << 16;
			val |= (*p++) << 8;
			val |= *p++;
			v = val;
			if ((val & addressing_mask) < 0x8000) {
				; // low memory
			} else if ((val & ~addressing_mask) == ~addressing_mask && val >= 0xfff80000) {
				; // high memory
			} else if ((val & addressing_mask) < test_memory_addr || (val & addressing_mask) >= test_memory_addr + test_memory_size) {
				end_test();
				printf("restore_rel CT_ABSOLUTE_LONG outside of test memory! %08x\n", v);
				endinfo();
				exit(0);
			}
			break;
		}
	}
	*vp = v;
	return p;
}

static void validate_mode(uae_u8 mode, uae_u8 v)
{
	if ((mode & CT_DATA_MASK) != v) {
		end_test();
		printf("CT_MEMWRITE expected but got %02X\n", mode);
		endinfo();
		exit(0);
	}
}

static uae_u8 *get_memory_addr(uae_u8 *p, uae_u8 **addrp)
{
	uae_u8 v = *p++;
	switch(v & CT_SIZE_MASK)
	{
		case CT_ABSOLUTE_WORD:
		{
			uae_u16 val;
			val = (*p++) << 8;
			val |= *p++;
			uae_u8 *addr;
			uae_s16 offset = (uae_s16)val;
			if (offset < 0) {
				addr = high_memory + 32768 + offset;
			} else {
				addr = low_memory + offset;
			}
			validate_mode(p[0], CT_MEMWRITE);
			*addrp = addr;
			return p;
		}
		case CT_ABSOLUTE_LONG:
		{
			uae_u32 val;
			val  = (*p++) << 24;
			val |= (*p++) << 16;
			val |= (*p++) << 8;
			val |= *p++;
			if (val < test_memory_addr || val >= test_memory_addr + test_memory_size) {
				end_test();
				printf("get_memory_addr CT_ABSOLUTE_LONG outside of test memory! %08x\n", val);
				endinfo();
				exit(0);
			}
#ifdef _MSC_VER
			uae_u8 *addr = test_memory + (val - test_memory_addr);
#else
			uae_u8 *addr = (uae_u8 *)val;
#endif
			validate_mode(p[0], CT_MEMWRITE);
			*addrp = addr;
			return p;
		}
		case CT_RELATIVE_START_WORD:
		{
			uae_u16 val;
			val = (*p++) << 8;
			val |= *p++;
			uae_s16 offset = (uae_s16)val;
			uae_u8 *addr = opcode_memory + offset;
			validate_mode(p[0], CT_MEMWRITE);
			*addrp = addr;
			return p;
		}
		break;

		default:
			end_test();
			printf("get_memory_addr unknown size %02x\n", v);
			endinfo();
			exit(0);
	}
	return NULL;		
}

static void tomem(uae_u8 *p, uae_u32 v, uae_u32 oldv, int size, int storedata)
{
	if (storedata) {
		struct accesshistory *ah = &ahist[ahcnt++];
		ah->oldval = oldv;
		ah->val = v;
		ah->size = size;
		ah->addr = p;
	}
	switch (size)
	{
		case 0:
			p[0] = (uae_u8)v;
			break;
		case 1:
			p[0] = (uae_u8)(v >> 8);
			p[1] = (uae_u8)(v >> 0);
			break;
		case 2:
			p[0] = (uae_u8)(v >> 24);
			p[1] = (uae_u8)(v >> 16);
			p[2] = (uae_u8)(v >> 8);
			p[3] = (uae_u8)(v >> 0);
			break;
	}
}

static void restoreahist(void)
{
	for (int i = ahcnt - 1; i >= 0; i--) {
		struct accesshistory *ah = &ahist[ahcnt];
		tomem(ah->addr, ah->oldval, 0, ah->size, 0);
	}
	ahcnt = 0;
}


static uae_u8 *restore_memory(uae_u8 *p, int storedata)
{
	uae_u8 v = *p;
	switch (v & CT_SIZE_MASK)
	{
		case CT_ABSOLUTE_WORD:
		{
			uae_u8 *addr;
			int size;
			p = get_memory_addr(p, &addr);
			uae_u32 mv = 0;
			uae_u32 oldv = 0;
			p = restore_value(p, &oldv, &size);
			p = restore_value(p, &mv, &size);
			tomem(addr, mv, oldv, size, storedata);
			return p;
		}
		case CT_ABSOLUTE_LONG:
		{
			uae_u8 *addr;
			int size;
			p = get_memory_addr(p, &addr);
			uae_u32 mv = 0;
			uae_u32 oldv = 0;
			p = restore_value(p, &oldv, &size);
			p = restore_value(p, &mv, &size);
			tomem(addr, mv, oldv, size, storedata);
			return p;
		}
	}
	if ((v & CT_DATA_MASK) == CT_MEMWRITES) {
		switch (v & CT_SIZE_MASK)
		{
			case CT_PC_BYTES:
			{
				p++;
				uae_u8 *addr = opcode_memory;
				uae_u8 v = *p++;
				addr += v >> 5;
				v &= 31;
				if (v == 0)
					v = 32;
				memcpy(addr, p, v);
				p += v;
				break;
			}
			default:
				end_test();
				printf("Unknown restore_memory type!?\n");
				endinfo();
				exit(0);
				break;
			}
	} else {
		switch (v & CT_SIZE_MASK)
		{
			case CT_RELATIVE_START_WORD:
			{
				uae_u8 *addr;
				int size;
				p = get_memory_addr(p, &addr);
				uae_u32 mv = 0, oldv = 0;
				p = restore_value(p, &oldv, &size);
				p = restore_value(p, &mv, &size);
				tomem(addr, mv, oldv, size, storedata);
				return p;
			}
			default:
				end_test();
				printf("Unknown restore_memory type!?\n");
				endinfo();
				exit(0);
				break;
		}
	}
	return p;
}

static uae_u8 *restore_data(uae_u8 *p)
{
	uae_u8 v = *p;
	if (v & CT_END) {
		end_test();
		printf("Unexpected end bit!? offset %ld\n", p - test_data);
		endinfo();
		exit(0);
	}
	int mode = v & CT_DATA_MASK;
	if (mode < CT_AREG + 8) {
		int size;
		if ((v & CT_SIZE_MASK) == CT_SIZE_FPU) {
			p = restore_fpvalue(p, &regs.fpuregs[mode]);
		} else {
			p = restore_value(p, &regs.regs[mode], &size);
		}
	} else if (mode == CT_SR) {
		int size;
		p = restore_value(p, &regs.sr, &size);
	} else if (mode == CT_FPIAR) {
		int size;
		p = restore_value(p, &regs.fpiar, &size);
	} else if (mode == CT_FPCR) {
		int size;
		p = restore_value(p, &regs.fpcr, &size);
	} else if (mode == CT_FPSR) {
		int size;
		p = restore_value(p, &regs.fpsr, &size);
	} else if (mode == CT_MEMWRITE) {
		// if memwrite, store old data
		p = restore_memory(p, 1);
	} else if (mode == CT_MEMWRITES) {
		p = restore_memory(p, 0);
	} else {
		end_test();
		printf("Unexpected mode %02x\n", v);
		endinfo();
		exit(0);
	}
	return p;
}

int	Disass68k(long addr, char *labelBuffer, char *opcodeBuffer, char *operandBuffer, char *commentBuffer);
void Disasm_SetCPUType(int CPU, int FPU);

static uae_u16 test_sr;
static uae_u32 test_fpsr, test_fpcr;

static void addinfo(void)
{
	if (infoadded)
		return;
	infoadded = 1;
	if (!dooutput)
		return;
	sprintf(outbp, "%lu:", testcnt);
	outbp += strlen(outbp);
	uae_u8 *p = opcode_memory;
	for (int i = 0; i < 32 * 2; i += 2) {
		uae_u16 v = (p[i] << 8) |(p[i + 1]);
		if (v == 0x4afc && i > 0)
			break;
		sprintf(outbp, " %04x", v);
		outbp += strlen(outbp);
	}
	strcat(outbp, " ");
	outbp += strlen(outbp);

	Disasm_SetCPUType(0, 0);
	char buf1[80], buf2[80], buf3[80], buf4[80];
	Disass68k((long)opcode_memory, buf1, buf2, buf3, buf4);
	sprintf(outbp, "%s %s\n", buf2, buf3);
	outbp += strlen(outbp);
}

static void out_regs(struct registers *r, int before)
{
	for (int i = 0; i < 16; i++) {
		if (i > 0 && (i % 4) == 0) {
			strcat(outbp, "\n");
		} else if ((i % 8) != 0) {
			strcat(outbp, " ");
		}
		outbp += strlen(outbp);
		sprintf(outbp, "%c%d:%c%08lx", i < 8 ? 'D' : 'A', i & 7, test_regs.regs[i] != regs.regs[i] ? '*' : ' ', r->regs[i]);
		outbp += strlen(outbp);
	}
	strcat(outbp, "\n");
	outbp += strlen(outbp);
	sprintf(outbp, "SR:%c%04x   PC: %08lx ISP: %08lx MSP: %08lx\n", test_regs.sr != test_sr ? '*' : ' ', before ? test_sr : r->sr, r->pc, r->ssp, r->msp);
	outbp += strlen(outbp);
	uae_u16 s = before ? test_sr : r->sr;
	uae_u16 s1 = test_regs.sr;
	uae_u16 s2 = test_sr;
	sprintf(outbp, "T%c%d S%c%d X%c%d N%c%d Z%c%d V%c%d C%c%d",
		(s1 & 0x8000) != (s2 & 0x8000) ? '*' : '=', (s & 0x8000) != 0,
		(s1 & 0x2000) != (s2 & 0x2000) ? '*' : '=', (s & 0x2000) != 0,
		(s1 & 0x10) != (s2 & 0x10) ? '*' : '=', (s & 0x10) != 0,
		(s1 & 0x08) != (s2 & 0x08) ? '*' : '=', (s & 0x08) != 0,
		(s1 & 0x04) != (s2 & 0x04) ? '*' : '=', (s & 0x04) != 0,
		(s1 & 0x02) != (s2 & 0x02) ? '*' : '=', (s & 0x02) != 0,
		(s1 & 0x01) != (s2 & 0x01) ? '*' : '=', (s & 0x01) != 0);
	outbp += strlen(outbp);

	if (!fpu_model) {
		strcat(outbp, "\n");
		outbp += strlen(outbp);
		return;
	}

	for (int i = 0; i < 8; i++) {
		if ((i % 2) == 0) {
			strcat(outbp, "\n");
		}
		else if ((i % 4) != 0) {
			strcat(outbp, " ");
		}
		outbp += strlen(outbp);
		struct fpureg *f = &r->fpuregs[i];
		void *f1 = &regs.fpuregs[i];
		void *f2 = &test_regs.fpuregs[i];
		sprintf(outbp, "FP%d:%c%04x-%08lx%08lx %f",
			i,
			memcmp(f1, f2, 12) ? '*' : ' ',
			f->exp, f->m[0], f->m[1],
			*((long double*)f));
		outbp += strlen(outbp);
	}
	sprintf(outbp, "\nFPSR:%c%08lx FPCR:%c%08lx FPIAR:%c%08lx\n",
		test_fpsr != test_regs.fpsr ? '*' : ' ', before ? test_fpsr : r->fpsr,
		test_fpcr != test_regs.fpcr ? '*' : ' ', before ? test_fpcr : r->fpcr,
		regs.fpiar != test_regs.fpiar ? '*' : ' ', r->fpiar);

	outbp += strlen(outbp);

}

static void hexdump(uae_u8 *p, int len)
{
	for (int i = 0; i < len; i++) {
		if (i > 0)
			*outbp++ = '.';
		sprintf(outbp, "%02x", p[i]);
		outbp += strlen(outbp);
	}
	*outbp++ = '\n';
}

static void validate_exception(struct registers *regs, uae_u8 *p)
{
	int exclen = 0;
	uae_u8 excmatch[32];
	uae_u8 *sp = (uae_u8*)regs->excframe;
	uae_u32 v;
	uae_u8 excdatalen = *p++;

	if (!excdatalen)
		return;

	if (cpu_lvl == 0) {
		if (regs->exc == 3) {
			// status (with undocumented opcode part)
			excmatch[0] = opcode_memory[0];
			excmatch[1] = (opcode_memory[1] & 0xf0) | (*p++);
			// access address
			v = opcode_memory_addr;
			p = restore_rel(p, &v);
			pl(excmatch + 2, v);
			p += 4;
			// opcode
			excmatch[6] = opcode_memory[0];
			excmatch[7] = opcode_memory[1];
			// sr
			excmatch[8] = regs->sr >> 8;
			excmatch[9] = regs->sr;
			// pc
			pl(excmatch + 10, regs->pc);
			exclen = 14;
		}
	} else {
		// sr
		excmatch[0] = regs->sr >> 8;
		excmatch[1] = regs->sr;
		pl(excmatch + 2, regs->pc);
	}
	if (exclen == 0)
		return;
	if (memcmp(excmatch, sp, exclen)) {
		strcpy(outbp, "Exception stack frame mismatch");
		outbp += strlen(outbp);
		hexdump(sp, exclen);
		hexdump(excmatch, exclen);
	}
}

static uae_u8 *validate_test(uae_u8 *p, int ignore_errors, int ignore_sr)
{
	uae_u8 regs_changed[16] = { 0 };
	uae_u8 regs_fpuchanged[8] = { 0 };
	uae_u8 sr_changed = 0, pc_changed = 0;
	uae_u8 fpiar_changed = 0, fpsr_changed = 0, fpcr_changed = 0;
	int exc = -1;

	for (int i = 0; i < 16; i++) {
		if (last_registers.regs[i] != test_regs.regs[i]) {
			regs_changed[i] = 1;
		}
	}
	if (last_registers.sr != test_regs.sr) {
		sr_changed = 1;
	}
	if (last_registers.pc != test_regs.pc) {
		pc_changed = 1;
	}
	if (fpu_model) {
		for (int i = 0; i < 8; i++) {
			if (memcmp(&last_registers.fpuregs[i], &test_regs.fpuregs[i], 12)) {
				regs_fpuchanged[i] = 1;
			}
		}
		if (last_registers.fpsr != test_regs.fpsr) {
			fpsr_changed = 1;
		}
		if (last_registers.fpcr != test_regs.fpcr) {
			fpcr_changed = 1;
		}
		if (last_registers.fpiar != test_regs.fpiar) {
			fpiar_changed = 1;
		}
	}

	if (*p == CT_END_SKIP)
		return p + 1;

	for (;;) {
		uae_u8 v = *p;
		if (v & CT_END) {
			exc = v & CT_EXCEPTION_MASK;
			int cpuexc = test_regs.exc & 65535;
			p++;
			if ((v & CT_END_INIT) == CT_END_INIT) {
				end_test();
				printf("Unexpected CT_END_INIT %02x %08lx\n", v, p - test_data);
				endinfo();
				exit(0);
			}
			if (exc == 1) {
				end_test();
				printf("Invalid exception ID %02x\n", exc);
				endinfo();
				exit(0);
			}
			if (ignore_errors)
				break;
			if (exc == 0 && cpuexc == 4) {
				// successful complete generates exception 4 with matching PC
				if (last_registers.pc != test_regs.pc && dooutput) {
					sprintf(outbp, "PC: expected %08lx but got %08lx\n", last_registers.pc, test_regs.pc);
					outbp += strlen(outbp);
					errors++;
				}
				break;
			}
			if (exc) {
				uae_u8 excdatalen = *p++;
				if (exc == cpuexc && excdatalen) {
					validate_exception(&test_regs, p);
				}
				p += excdatalen;
			}
			if (exc != cpuexc) {
				addinfo();
				if (dooutput) {
					if (cpuexc == 4 && last_registers.pc == test_regs.pc) {
						sprintf(outbp, "Exception ID: expected %d but got no exception.\n", exc);
					} else {
						sprintf(outbp, "Exception ID: expected %d but got %d\n", exc, cpuexc);
					}
				}
				outbp += strlen(outbp);
				errors++;
			}
			break;
		}
		int mode = v & CT_DATA_MASK;
		if (mode < CT_AREG + 8) {
			uae_u32 val = last_registers.regs[mode];
			int size;
			p = restore_value(p, &val, &size);
			if (val != test_regs.regs[mode] && !ignore_errors) {
				addinfo();
				if (dooutput) {
					sprintf(outbp, "%c%d: expected %08lx but got %08lx\n", mode < CT_AREG ? 'D' : 'A', mode & 7, val, test_regs.regs[mode]);
					outbp += strlen(outbp);
				}
				errors++;
			}
			regs_changed[mode] = 0;
			last_registers.regs[mode] = val;
		} else if (mode == CT_SR) {
			uae_u32 val = last_registers.sr;
			int size;
			p = restore_value(p, &val, &size);
			if ((val & sr_undefined_mask) != (test_regs.sr & sr_undefined_mask) && !ignore_errors && !ignore_sr) {
				addinfo();
				if (dooutput) {
					sprintf(outbp, "SR: expected %04x -> %04x but got %04x\n", test_sr, val, test_regs.sr);
					outbp += strlen(outbp);
				}
				errors++;
			}
			sr_changed = 0;
			last_registers.sr = val;
		} else if (mode == CT_PC) {
			uae_u32 val = last_registers.pc;
			p = restore_rel(p, &val);
			pc_changed = 0;
			last_registers.pc = val;
		} else if (mode == CT_FPCR) {
			uae_u32 val = last_registers.fpcr;
			int size;
			p = restore_value(p, &val, &size);
			if (val != test_regs.fpcr && !ignore_errors) {
				addinfo();
				if (dooutput) {
					sprintf(outbp, "FPCR: expected %08lx -> %08lx but got %08lx\n", test_fpcr, val, test_regs.fpcr);
					outbp += strlen(outbp);
				}
				errors++;
			}
			fpcr_changed = 0;
			last_registers.fpcr = val;
		} else if (mode == CT_FPSR) {
			uae_u32 val = last_registers.fpsr;
			int size;
			p = restore_value(p, &val, &size);
			if (val != test_regs.fpsr && !ignore_errors) {
				addinfo();
				if (dooutput) {
					sprintf(outbp, "FPSR: expected %08lx -> %08lx but got %08lx\n", test_fpsr, val, test_regs.fpsr);
					outbp += strlen(outbp);
				}
				errors++;
			}
			fpsr_changed = 0;
			last_registers.fpsr = val;
		} else if (mode == CT_FPIAR) {
			uae_u32 val = last_registers.fpiar;
			p = restore_rel(p, &val);
			if (val != test_regs.fpiar && !ignore_errors) {
				addinfo();
				if (dooutput) {
					sprintf(outbp, "FPIAR: expected %08x but got %08x\n", val, test_regs.fpiar);
					outbp += strlen(outbp);
				}
				errors++;
			}
			fpiar_changed = 0;
			last_registers.fpiar = val;

		} else if (mode == CT_MEMWRITES) {
			p = restore_memory(p, 0);
		} else if (mode == CT_MEMWRITE) {
			uae_u8 *addr;
			uae_u32 val = 0, mval = 0, oldval = 0;
			int size;
			p = get_memory_addr(p, &addr);
			p = restore_value(p, &oldval, &size);
			p = restore_value(p, &val, &size);
			switch(size)
			{
				case 0:
				mval = addr[0];
				if (mval != val && !ignore_errors) {
					addinfo();
					if (dooutput) {
						sprintf(outbp, "Memory byte write: address %08lx, expected %02x but got %02x\n", (uae_u32)addr, val, mval);
						outbp += strlen(outbp);
					}
					errors++;
				}
				addr[0] = oldval;
				break;
				case 1:
				mval = (addr[0] << 8) | (addr[1]);
				if (mval != val && !ignore_errors) {
					addinfo();
					if (dooutput) {
						sprintf(outbp, "Memory word write: address %08lx, expected %04x but got %04x\n", (uae_u32)addr, val, mval);
						outbp += strlen(outbp);
					}
					errors++;
				}
				addr[0] = oldval >> 8;
				addr[1] = oldval;
				break;
				case 2:
				mval = gl(addr);
				if (mval != val && !ignore_errors) {
					addinfo();
					if (dooutput) {
						sprintf(outbp, "Memory long write: address %08lx, expected %08lx but got %08x\n", (uae_u32)addr, val, mval);
						outbp += strlen(outbp);
					}
					errors++;
				}
				pl(addr, oldval);
				break;
			}
		} else {
			end_test();
			printf("Unknown test data %02x\n", v);
			exit(0);
		}
	}
	if (!ignore_errors) {
		if (!ignore_sr) {
			for (int i = 0; i < 16; i++) {
				if (regs_changed[i]) {
					addinfo();
					if (dooutput) {
						sprintf(outbp, "%c%d: modified %08lx -> %08lx but expected no modifications\n", i < 8 ? 'D' : 'A', i & 7, last_registers.regs[i], test_regs.regs[i]);
						outbp += strlen(outbp);
					}
					errors++;
				}
			}
			if (sr_changed) {
				addinfo();
				if (dooutput) {
					sprintf(outbp, "SR: modified %04x -> %04x but expected no modifications\n", last_registers.sr, test_regs.sr);
					outbp += strlen(outbp);
				}
				errors++;
			}
		}
		for (int i = 0; i < 8; i++) {
			if (regs_fpuchanged[i]) {
				addinfo();
				if (dooutput) {
					sprintf(outbp, "FP%d: modified %f -> %f but expected no modifications\n", i,
						*((long double *)(&last_registers.fpuregs[i])), *((long double *)(&test_regs.fpuregs[i])));
					outbp += strlen(outbp);
				}
				errors++;
			}
		}
		if (fpsr_changed) {
			addinfo();
			if (dooutput) {
				sprintf(outbp, "FPSR: modified %08x -> %08x but expected no modifications\n", last_registers.fpsr, test_regs.fpsr);
				outbp += strlen(outbp);
			}
			errors++;
		}
		if (fpcr_changed) {
			addinfo();
			if (dooutput) {
				sprintf(outbp, "FPCR: modified %08x -> %08x but expected no modifications\n", last_registers.fpcr, test_regs.fpcr);
				outbp += strlen(outbp);
			}
			errors++;
		}
		if (fpiar_changed) {
			addinfo();
			if (dooutput) {
				sprintf(outbp, "FPIAR: modified %08x -> %08x but expected no modifications\n", last_registers.fpiar, test_regs.fpiar);
				outbp += strlen(outbp);
			}
			errors++;
		}
	}
	if (errors && dooutput) {
		addinfo();
		if (!fpu_model) {
			strcat(outbp, "Registers before:\n");
			outbp += strlen(outbp);
		}
		out_regs(&regs, 1);
		if (!fpu_model) {
			strcat(outbp, "Registers after:\n");
			outbp += strlen(outbp);
		}
		out_regs(&test_regs, 0);
		if (exc > 1) {
			sprintf(outbp, "OK: Generated exception %d\n", exc);
			outbp += strlen(outbp);
			if (exc == 3 && cpu_lvl == 0) {
				sprintf(outbp, "RW=%d IN=%d FC=%d\n",
					((test_regs.exc >> (16 + 4)) & 1),
					((test_regs.exc >> (16 + 3)) & 1),
					((test_regs.exc >> (16 + 0)) & 7));
				outbp += strlen(outbp);
			}
		} else if (exc == 0 && (test_regs.exc & 65535) == 4) {
			sprintf(outbp, "OK: No exception generated\n");
			outbp += strlen(outbp);
		}
	}
	return p;
}

static void process_test(uae_u8 *p)
{
	outbp = outbuffer;
	outbp[0] = 0;
	infoadded = 0;
	errors = 0;

	memset(&regs, 0, sizeof(struct registers));

	start_test();

	ahcnt = 0;

	for (;;) {

#ifdef _MSC_VER
		outbp = outbuffer;
#endif

		for (;;) {
			uae_u8 v = *p;
			if (v == CT_END_INIT || v == CT_END_FINISH)
				break;
			p = restore_data(p);
		}
		if (*p == CT_END_FINISH)
			break;
		p++;

		xmemcpy(&last_registers, &regs, sizeof(struct registers));

		int fpumode = fpu_model && (opcode_memory[0] & 0xf0) == 0xf0;

		if (cpu_lvl >= 2)
			flushcache();

		uae_u32 pc = opcode_memory_addr;

		int extraccr = 0;

		uae_u32 last_pc = opcode_memory_addr;
		uae_u32 last_fpiar = opcode_memory_addr;

		for (;;) {
			uae_u16 sr_mask = 0;

			if (extraccr & 1)
				sr_mask |= 0x2000; // S
			if (extraccr & 2)
				sr_mask |= 0x4000; // T0
			if (extraccr & 4)
				sr_mask |= 0x8000; // T1
			if (extraccr & 8)
				sr_mask |= 0x1000; // M

			int ccrmax = fpumode ? 256 : 32;
			for (int ccr = 0; ccr < ccrmax; ccr++) {

				regs.ssp = test_memory_addr + test_memory_size - 0x80;
				regs.msp = test_memory_addr + test_memory_size;
				regs.pc = opcode_memory_addr;
				regs.fpiar = opcode_memory_addr;

				xmemcpy(&test_regs, &regs, sizeof(struct registers));

				test_regs.sr = ccr | sr_mask;
				test_sr = test_regs.sr;
				if (fpumode) {
					test_regs.fpsr = (ccr & 15) << 24;
					test_regs.fpcr = (ccr >> 4) << 4;
					test_fpsr = test_regs.fpsr;
					test_fpcr = test_regs.fpcr;
				}


				if ((*p) == CT_END_SKIP) {

					p++;

				} else {

					int ignore_errors = 0;
					int ignore_sr = 0;

					if ((ccr_mask & ccr) || (ccr == 0)) {

						if (cpu_lvl == 1) {
							execute_test010(&test_regs);
						} else if (cpu_lvl == 2) {
							if (fpu_model)
								execute_testfpu(&test_regs);
							else
								execute_test020(&test_regs);
						} else {
							execute_test000(&test_regs);
						}

						if (ccr_mask == 0 && ccr == 0)
							ignore_sr = 1;

					} else {

						test_regs.sr = test_sr;
						ignore_errors = 1;
						ignore_sr = 1;

					}

					last_registers.pc = last_pc;
					last_registers.fpiar = last_fpiar;

					p = validate_test(p, ignore_errors, ignore_sr);

					last_pc = last_registers.pc;
					last_fpiar = last_registers.fpiar;

				}

				testcnt++;

				if (testexit()) {
					end_test();
					printf("\nAborted\n");
					exit(0);
				}

#if DONTSTOPONERROR == 0
				if (quit || errors)
					goto end;
#endif
			}

			if (*p == CT_END) {
				p++;
				break;
			}

			extraccr = *p++;

		}

		restoreahist();

	}

end:
	end_test();

	if (infoadded) {
		printf("\n");
		printf(outbuffer);
	}

}

static void freestuff(void)
{
	if (test_memory && test_memory_addr)
		free_absolute(test_memory_addr, test_memory_size);
}


static int test_mnemo(const char *path, const char *opcode)
{
	int size;
	uae_u8 data[4] = { 0 };
	uae_u32 v;
	char fname[256], tfname[256];
	int filecnt = 1;
	uae_u32 starttimeid;

	errors = 0;
	quit = 0;

	sprintf(tfname, "%s%s/0000.dat", path, opcode);
	FILE *f = fopen(tfname, "rb");
	if (!f) {
		printf("Couldn't open '%s'\n", tfname);
		exit(0);
	}
	int header_size = 32;
	fread(data, 1, 4, f);
	v = gl(data);
	if (v != 0x00000001) {
		printf("Invalid test data file (header)\n");
		exit(0);
	}
	fread(data, 1, 4, f);
	starttimeid = gl(data);
	fread(data, 1, 4, f);
	hmem_rom = (uae_s16)(gl(data) >> 16);
	lmem_rom = (uae_s16)(gl(data) & 65535);
	fread(data, 1, 4, f);
	test_memory_addr = gl(data);
	fread(data, 1, 4, f);
	test_memory_size = gl(data);
	fread(data, 1, 4, f);
	opcode_memory_addr = gl(data) + test_memory_addr;
	fread(data, 1, 4, f);
	cpu_lvl = gl(data) >> 16;
	sr_undefined_mask = gl(data);
	fread(data, 1, 4, f);
	fpu_model = gl(data);
	fread(inst_name, 1, sizeof(inst_name) - 1, f);
	inst_name[sizeof(inst_name) - 1] = 0;

	if (!check_undefined_sr) {
		sr_undefined_mask = ~sr_undefined_mask;
	} else {
		sr_undefined_mask = 0xffff;
	}

	if (!absallocated) {
		test_memory = allocate_absolute(test_memory_addr, test_memory_size);
		if (!test_memory) {
			printf("Couldn't allocate tmem area %08lx-%08lx\n", (uae_u32)test_memory_addr, test_memory_size);
			exit(0);
		}
		absallocated = test_memory;
	}
	if (absallocated != test_memory) {
		printf("tmem area changed!?\n");
		exit(0);
	}

	opcode_memory = test_memory + (opcode_memory_addr - test_memory_addr);

	size = test_memory_size;
	load_file(path, "tmem.dat", test_memory, &size);
	if (size != test_memory_size) {
		printf("tmem.dat size mismatch\n");
		exit(0);
	}

	printf("%s:\n", inst_name);

	testcnt = 0;

	for (;;) {
		printf("%s. %lu...\n", tfname, testcnt);

		sprintf(tfname, "%s%s/%04ld.dat", path, opcode, filecnt);
		FILE *f = fopen(tfname, "rb");
		if (!f)
			break;
		fread(data, 1, 4, f);
		if (gl(data) != 0x00000001) {
			printf("Invalid test data file (header)\n");
			exit(0);
		}
		fread(data, 1, 4, f);
		if (gl(data) != starttimeid) {
			printf("Test data file header mismatch\n");
			exit(0);
		}
		fseek(f, 0, SEEK_END);
		test_data_size = ftell(f);
		fseek(f, 16, SEEK_SET);
		test_data_size -= 16;
		if (test_data_size <= 0)
			break;
		test_data = calloc(1, test_data_size);
		if (!test_data) {
			printf("Couldn't allocate memory for '%s', %lu bytes\n", tfname, test_memory_size);
			exit(0);
		}
		if (fread(test_data, 1, test_data_size, f) != test_data_size) {
			printf("Couldn't read '%s'\n", fname);
			break;
		}
		fclose(f);
		if (test_data[test_data_size - 1] != CT_END_FINISH) {
			printf("Invalid test data file (footer)\n");
			exit(0);
		}

		process_test(test_data);

		if (errors || quit)
			break;

		free(test_data);
		filecnt++;
	}

	if (!errors && !quit) {
		printf("All tests complete (total %lu).\n", testcnt);
	}

	return errors || quit;
}

static int getparamval(const char *p)
{
	if (strlen(p) > 2 && p[0] == '0' && toupper(p[1]) == 'X') {
		char *endptr;
		return strtol(p + 2, &endptr, 16);
	} else {
		return atol(p);
	}
}

static char path[256];

int main(int argc, char *argv[])
{
	int size;
	char opcode[16];
	int stop_on_error = 1;

	atexit(freestuff);

#ifdef _MSC_VER

	char *params[] = { "", "or.w", "", NULL };
	argv = params;
	argc = 3;

	strcpy(path, "C:\\projects\\winuae\\src\\cputest\\data\\");

	low_memory = calloc(1, 32768);
	high_memory = calloc(1, 32768);
	vbr_zero = calloc(1, 1024);

	cpu_lvl = 0;
#else

#define _stricmp stricmp

	if (strlen(argv[1]) >= sizeof(opcode) - 1)
		return 0;

	strcpy(path, "data/");

	low_memory = (uae_u8 *)0;
	high_memory = (uae_u8 *)0xffff8000;

	cpu_lvl = get_cpu_model();
#endif

	if (argc < 2) {
		printf("cputest <all/mnemonic> (<start mnemonic>) (continue)\n");
		printf("mnemonic = test single mnemonic\n");
		printf("all = test all\n");
		printf("all <mnemonic> = test all, starting from <mnemonic>\n");
		printf("continue = don't stop on error (all mode only)\n");
		printf("ccrmask = ignore CCR bits that are not set.\n");
		return 0;
	}

	sprintf(path + strlen(path), "%lu/", 68000 + cpu_lvl * 10);

	strcpy(opcode, argv[1]);

	check_undefined_sr = 1;
	ccr_mask = 0xff;
	for (int i = 1; i < argc; i++) {
		char *s = argv[i];
		char *next = i + 1 < argc ? argv[i + 1] : NULL;
		if (!_stricmp(s, "continue")) {
			stop_on_error = 0;
		} else if (!_stricmp(s, "noundefined")) {
			check_undefined_sr = 0;
		} else if (!_stricmp(s, "ccrmask")) {
			ccr_mask = 0;
			if (next) {
				ccr_mask = getparamval(next);
				i++;
			}
		}
	}

	size = 32768;
	load_file(path, "lmem.dat", low_memory_temp, &size);
	size = 32768;
	load_file(path, "hmem.dat", high_memory_temp, &size);

	if (!_stricmp(opcode, "all")) {
		DIR *d = opendir(path);
		if (!d) {
			printf("Couldn't list directory '%s'\n", path);
			return 0;
		}
#define MAX_FILE_LEN 128
#define MAX_MNEMOS 256
		char *dirs = calloc(MAX_MNEMOS, MAX_FILE_LEN);
		int diroff = 0;
		if (!dirs)
			return 0;

		for (;;) {
			struct dirent *dr = readdir(d);
			if (!dr)
				break;
			if (dr->d_type == DT_DIR && dr->d_name[0] != '.') {
				strcpy(dirs + diroff, dr->d_name);
				diroff += MAX_FILE_LEN;
				if (diroff >= MAX_FILE_LEN * MAX_MNEMOS) {
					printf("too many directories!?\n");
					return 0;
				}
			}
		}
		closedir(d);

		for (int i = 0; i < diroff; i += MAX_FILE_LEN) {
			for (int j = i + MAX_FILE_LEN; j < diroff; j += MAX_FILE_LEN) {
				if (_stricmp(dirs + i, dirs + j) > 0) {
					char tmp[MAX_FILE_LEN];
					strcpy(tmp, dirs + j);
					strcpy(dirs + j, dirs + i);
					strcpy(dirs + i, tmp);
				}
			}
		}

		int first = 0;
		if (argc >= 3) {
			for (int i = 0; i < diroff; i += MAX_FILE_LEN) {
				if (!_stricmp(dirs + i, argv[2])) {
					first = i;
					break;
				}
			}
		}
		for (int i = first; i < diroff; i += MAX_FILE_LEN) {
			if (test_mnemo(path, dirs + i)) {
				if (stop_on_error)
					break;
			}
		}

		free(dirs);

	} else {
		test_mnemo(path, opcode);
	}
}
