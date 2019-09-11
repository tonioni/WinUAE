
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
static uae_u32 low_memory_size;
static uae_u32 high_memory_size;
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
static int flag_mode;
static int check_undefined_sr;
static uae_u32 cpustatearraystore[16];
static uae_u32 cpustatearraynew[] = {
	0x00000005, // SFC
	0x00000005, // DFC
	0x00000009, // CACR
	0x00000000, // CAAR
	0x00000000, // MSP
};

static uae_u8 *low_memory_temp;
static uae_u8 *high_memory_temp;
static uae_u8 *low_memory_back;
static uae_u8 *high_memory_back;

static uae_u32 vbr[256];
	
static char inst_name[16+1];
#ifndef M68K
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

#ifndef M68K

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
static void flushcache(uae_u32 v)
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
extern void flushcache(uae_u32);

#endif

struct accesshistory
{
	uae_u8 *addr;
	uae_u32 val;
	uae_u32 oldval;
	int size;
};
static int ahcnt;

#define MAX_ACCESSHIST 48
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

#ifdef M68K
	if (lmem_rom > 0) {
		if (memcmp(low_memory, low_memory_temp, low_memory_size)) {
			printf("Low memory ROM mismatch!\n");
			exit(0);
		}
	}
	if (hmem_rom > 0) {
		if (memcmp(high_memory, high_memory_temp, high_memory_size)) {
			printf("High memory ROM mismatch!\n");
			exit(0);
		}
	}
#endif

	test_active = 1;

	enable_data = tosuper(0);

	memcpy(low_memory_back, low_memory, low_memory_size);
	if (!hmem_rom)
		memcpy(high_memory_back, high_memory, high_memory_size);

	memcpy(low_memory, low_memory_temp, low_memory_size);
	if (!hmem_rom)
		memcpy(high_memory, high_memory_temp, high_memory_size);

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

	memcpy(low_memory, low_memory_back, low_memory_size);
	if (!hmem_rom)
		memcpy(high_memory, high_memory_back, high_memory_size);

	if (cpu_lvl > 0) {
		setvbr(oldvbr);
	}
	setcpu(cpu_lvl, cpustatearraystore, NULL);

	touser(enable_data);
}

static uae_u8 *load_file(const char *path, const char *file, uae_u8 *p, int *sizep, int exiterror)
{
	char fname[256];
	sprintf(fname, "%s%s", path, file);
	FILE *f = fopen(fname, "rb");
	if (!f) {
		if (exiterror) {
			printf("Couldn't open '%s'\n", fname);
			exit(0);
		}
		return NULL; 
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
static void pw(uae_u8 *p, uae_u32 v)
{
	p[0] = v >> 8;
	p[1] = v >> 0;
}

static uae_u32 gl(uae_u8 *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3] << 0);
}

static uae_u16 gw(uae_u8 *p)
{
	return (p[0] << 8) | (p[1] << 0);
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
	fp->exp = gw(p);
	p += 2;
	fp->m[0] = gl(p);
	p += 4;
	fp->m[1] = gl(p);
	p += 4;
	fp->dummy = 0;
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

static uae_u8 *restore_rel(uae_u8 *p, uae_u32 *vp, int nocheck)
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
			if (!nocheck) {
				if ((val & addressing_mask) < low_memory_size) {
					; // low memory
				} else if ((val & ~addressing_mask) == ~addressing_mask && val >= 0xfff80000) {
					; // high memory
				} else if ((val & addressing_mask) < test_memory_addr || (val & addressing_mask) >= test_memory_addr + test_memory_size) {
					end_test();
					printf("restore_rel CT_ABSOLUTE_LONG outside of test memory! %08x\n", v);
					endinfo();
					exit(0);
				}
			}
			break;
		}
	}
	*vp = v;
	return p;
}

static uae_u8 *restore_rel_ordered(uae_u8 *p, uae_u32 *vp)
{
	if (*p == CT_EMPTY)
		return p + 1;
	return restore_rel(p, vp, 1);
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
			if (val < low_memory_size) {
#ifndef M68K
				uae_u8 *addr = low_memory + val;
#else
				uae_u8 *addr = (uae_u8 *)val;
#endif
				validate_mode(p[0], CT_MEMWRITE);
				*addrp = addr;
				return p;
			} else if (val >= test_memory_addr && val < test_memory_addr + test_memory_size) {
#ifndef M68K
				uae_u8 *addr = test_memory + (val - test_memory_addr);
#else
				uae_u8 *addr = (uae_u8 *)val;
#endif
				validate_mode(p[0], CT_MEMWRITE);
				*addrp = addr;
				return p;
			} else {
				end_test();
				printf("get_memory_addr CT_ABSOLUTE_LONG outside of test memory! %08x\n", val);
				endinfo();
				exit(0);
			}
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

static uae_u16 test_ccrignoremask;
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

	uae_u16 disasm_instr(uae_u16 *, char *);
#ifndef M68K
	uae_u16 swapped[16];
	for (int i = 0; i < 16; i++) {
		swapped[i] = (opcode_memory[i * 2 + 0] << 8) | (opcode_memory[i * 2 + 1] << 0);
	}
	disasm_instr((uae_u16*)swapped, outbp);
#else
	disasm_instr((uae_u16 *)opcode_memory, outbp);
#endif
	outbp += strlen(outbp);
	*outbp++ = '\n';
	*outbp = 0;

#if 0
int	Disass68k(long addr, char *labelBuffer, char *opcodeBuffer, char *operandBuffer, char *commentBuffer);
void Disasm_SetCPUType(int CPU, int FPU);
	Disasm_SetCPUType(0, 0);
	char buf1[80], buf2[80], buf3[80], buf4[80];
	Disass68k((long)opcode_memory, buf1, buf2, buf3, buf4);
	sprintf(outbp, "%s %s\n", buf2, buf3);
	outbp += strlen(outbp);
#endif
}

struct srbit
{
	char *name;
	int bit;
};
static struct srbit srbits[] = {
	{ "T1", 15 },
	{ "T0", 14 },
	{ "M", 13 },
	{ "S", 12 },
	{ "X", 4 },
	{ "N", 3 },
	{ "Z", 2 },
	{ "V", 1 },
	{ "C", 0 },
	{ NULL, 0 }
};

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
	sprintf(outbp, "SR:%c%04x   PC: %08lx ISP: %08lx MSP: %08lx\n",
		test_regs.sr != last_registers.sr ? '*' : ' ', before ? regs.sr : test_regs.sr,
		r->pc, r->ssp, r->msp);
	outbp += strlen(outbp);
	if (before >= 0) {
		uae_u16 s = before ? regs.sr : test_regs.sr; // current value
		uae_u16 s1 = regs.sr; // original value
		uae_u16 s2 = test_regs.sr; // test result value
		uae_u16 s3 = last_registers.sr; // expected result value
		for (int i = 0; srbits[i].name; i++) {
			if (i > 0)
				*outbp++ = ' ';
			uae_u16 mask = 1 << srbits[i].bit;
			sprintf(outbp, "%s%c%d", srbits[i].name,
				(s2 & mask) != (s3 & mask) ? '!' : ((s1 & mask) != (s2 & mask) ? '*' : '='), (s & mask) != 0);
			outbp += strlen(outbp);
		}
	}

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
			memcmp(f1, f2, sizeof(struct fpureg)) ? '*' : ' ',
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

static uae_u8 last_exception[256];
static int last_exception_len;

static uae_u8 *validate_exception(struct registers *regs, uae_u8 *p, int excnum, int sameexc, int *experr)
{
	int exclen = 0;
	uae_u8 *exc;
	uae_u8 *op = p;
	uae_u8 *sp = (uae_u8*)regs->excframe;
	uae_u32 v;
	uae_u8 excdatalen = *p++;
	int size;

	if (!excdatalen)
		return p;
	exc = last_exception;
	if (excdatalen != 0xff) {
		if (cpu_lvl == 0) {
			if (excnum == 3) {
				// status (with undocumented opcode part)
				uae_u8 opcode0 = p[1];
				uae_u8 opcode1 = p[2];
				exc[0] = opcode0;
				exc[1] = (opcode1 & ~0x1f) | p[0];
				p += 3;
				// access address
				v = opcode_memory_addr;
				p = restore_rel_ordered(p, &v);
				pl(exc + 2, v);
				// opcode
				exc[6] = opcode0;
				exc[7] = opcode1;
				// sr
				exc[8] = regs->sr >> 8;
				exc[9] = regs->sr;
				// pc
				pl(exc + 10, regs->pc);
				exclen = 14;
			}
		} else if (cpu_lvl > 0) {
			// sr
			exc[0] = regs->sr >> 8;
			exc[1] = regs->sr;
			pl(exc + 2, regs->pc);
			// frame type
			uae_u16 frame = ((*p++) << 8) | (*p++);
			exc[6] = frame >> 8;
			exc[7] = frame >> 0;

			switch (frame >> 12)
			{
			case 0:
				exclen = 8;
				break;
			case 2:
				v = opcode_memory_addr;
				p = restore_rel_ordered(p, &v);
				pl(exc + 8, v);
				exclen = 12;
				break;
			case 3:
				v = opcode_memory_addr;
				p = restore_rel_ordered(p, &v);
				pl(exc + 8, v);
				exclen = 12;
				break;
			case 4:
				v = opcode_memory_addr;
				p = restore_rel_ordered(p, &v);
				pl(exc + 8, v);
				v = opcode_memory_addr;
				p = restore_rel_ordered(p, &v);
				pl(exc + 12, v);
				exclen = 16;
				break;
			case 0x0a:
			case 0x0b:
				exclen = 8;
				break;
			default:
				end_test();
				printf("Unknown frame %04x\n", frame);
				exit(0);
				break;
			}
		}
		last_exception_len = exclen;
		if (p != op + excdatalen + 1) {
			end_test();
			printf("Exception length mismatch %d != %d\n", excdatalen, p - op - 1);
			exit(0);
		}
	} else {
		exclen = last_exception_len;
	}
	if (exclen == 0 || !sameexc)
		return p;
	if (memcmp(exc, sp, exclen)) {
		strcpy(outbp, "Exception stack frame mismatch:\n");
		outbp += strlen(outbp);
		strcpy(outbp, "Expected: ");
		outbp += strlen(outbp);
		hexdump(exc, exclen);
		strcpy(outbp, "Got     : ");
		outbp += strlen(outbp);
		hexdump(sp, exclen);
		errors = 1;
		*experr = 1;
	}
	return p;
}

// regs: registers before execution of test code
// test_reg: registers used during execution of test code, also modified by test code.
// last_registers: registers after modifications from data files. Test ok if test_reg == last_registers.

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
	if ((last_registers.sr & test_ccrignoremask) != (test_regs.sr & test_ccrignoremask)) {
		sr_changed = 1;
	}
	if (last_registers.pc != test_regs.pc) {
		pc_changed = 1;
	}
	if (fpu_model) {
		for (int i = 0; i < 8; i++) {
			if (memcmp(&last_registers.fpuregs[i], &test_regs.fpuregs[i], sizeof(struct fpureg))) {
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

	int experr = 0;
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
			if (ignore_errors) {
				if (exc) {
					p = validate_exception(&test_regs, p, exc, exc == cpuexc, &experr);
				}
				break;
			}
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
				p = validate_exception(&test_regs, p, exc, exc == cpuexc, &experr);
			}
			if (exc != cpuexc) {
				addinfo();
				if (dooutput) {
					if (cpuexc == 4 && last_registers.pc == test_regs.pc) {
						sprintf(outbp, "Exception ID: expected %d but got no exception.\n", exc);
					} else {
						sprintf(outbp, "Exception ID: expected %d but got %d\n", exc, cpuexc);
					}
					experr = 1;
				}
				outbp += strlen(outbp);
				errors++;
			}
			break;
		}
		int mode = v & CT_DATA_MASK;

		if (mode < CT_AREG + 8 && (v & CT_SIZE_MASK) != CT_SIZE_FPU) {
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
		} else if (mode < CT_AREG && (v & CT_SIZE_MASK) == CT_SIZE_FPU) {
			struct fpureg val;
			p = restore_fpvalue(p, &val);
			if (memcmp(&val, &test_regs.fpuregs[mode], sizeof(struct fpureg)) && !ignore_errors) {
				addinfo();
				if (dooutput) {
					sprintf(outbp, "FP%d: expected %04x-%08lx%08lx but got %04x-%08lx%08lx\n", mode,
						val.exp, val.m[0], val.m[1],
						test_regs.fpuregs[mode].exp, test_regs.fpuregs[mode].m[0], test_regs.fpuregs[mode].m[1]);
					outbp += strlen(outbp);
				}
				errors++;
			}
			regs_fpuchanged[mode] = 0;
			memcpy(&last_registers.fpuregs[mode], &val, sizeof(struct fpureg));
		} else if (mode == CT_SR) {
			uae_u32 val = last_registers.sr;
			int size;
			// High 16 bit: ignore mask, low 16 bit: SR/CCR
			p = restore_value(p, &val, &size);
			test_ccrignoremask = ~(val >> 16);

			if ((val & (sr_undefined_mask & test_ccrignoremask)) != (test_regs.sr & (sr_undefined_mask & test_ccrignoremask)) && !ignore_errors && !ignore_sr) {
				addinfo();
				if (dooutput) {
					sprintf(outbp, "SR: expected %04x -> %04x but got %04x (%04x)\n", regs.sr & 0xffff, val & 0xffff, test_regs.sr & 0xffff, test_ccrignoremask);
					outbp += strlen(outbp);
				}
				errors++;
			}
			sr_changed = 0;
			last_registers.sr = val;
		} else if (mode == CT_PC) {
			uae_u32 val = last_registers.pc;
			p = restore_rel(p, &val, 0);
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
			p = restore_rel(p, &val, 0);
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
					sprintf(outbp, "SR: modified %04x -> %04x but expected no modifications\n", last_registers.sr & 0xffff, test_regs.sr & 0xffff);
					outbp += strlen(outbp);
				}
				errors++;
			}
		}
		for (int i = 0; i < 8; i++) {
			if (regs_fpuchanged[i]) {
				addinfo();
				if (dooutput) {
					sprintf(outbp, "FP%d: modified %04x-%08lx%08lx -> %04x-%08lx%08lx but expected no modifications\n", i,
						last_registers.fpuregs[i].exp, last_registers.fpuregs[i].m[0], last_registers.fpuregs[i].m[1],
						test_regs.fpuregs[i].exp, test_regs.fpuregs[i].m[0], test_regs.fpuregs[i].m[1]);
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
			if (!experr) {
				sprintf(outbp, "OK: Generated exception %d\n", exc);
				outbp += strlen(outbp);
			}
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

	test_ccrignoremask = 0xffff;
	ahcnt = 0;

	for (;;) {

#ifndef M68K
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
			flushcache(cpu_lvl);

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

			int maxccr = fpumode ? 256 : 32;
			if (flag_mode) {
				maxccr = fpumode ? 256 / 8 : 2;
			}
			for (int ccr = 0;  ccr < maxccr; ccr++) {

				regs.ssp = test_memory_addr + test_memory_size - 0x80;
				regs.msp = test_memory_addr + test_memory_size;
				regs.pc = opcode_memory_addr;
				regs.fpiar = opcode_memory_addr;

				memcpy((void*)regs.ssp, (void*)regs.regs[15], 0x20);
				xmemcpy(&test_regs, &regs, sizeof(struct registers));

				if (flag_mode == 0) {
					test_regs.sr = ccr;
				} else {
					test_regs.sr = (ccr ? 31 : 0);
				}
				test_regs.sr |= sr_mask;
				uae_u32 test_sr = test_regs.sr;
				if (fpumode) {
					if (flag_mode == 0) {
						test_regs.fpsr = (ccr & 15) << 24;
						test_regs.fpcr = (ccr >> 4) << 4;
					} else {
						test_regs.fpsr = (ccr ? 15 : 0) << 24;
						test_regs.fpcr = (ccr >> 1) << 4;
					}
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
						} else if (cpu_lvl >= 2) {
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

					if ((*p) == CT_SKIP_REGS) {
						p++;
						for (int i = 0; i < 16; i++) {
							test_regs.regs[i] = regs.regs[i];
						}
					}

					p = validate_test(p, ignore_errors, ignore_sr);

					last_pc = last_registers.pc;
					last_fpiar = last_registers.fpiar;

				}

				testcnt++;

				if (testexit()) {
					end_test();
					printf("\nAborted (%ld)\n", testcnt);
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
	int lvl;

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
	lvl = (gl(data) >> 16) & 15;
	addressing_mask = (gl(data) & 0x80000000) ? 0xffffffff : 0x00ffffff;
	flag_mode = (gl(data) >> 30) & 1;
	sr_undefined_mask = gl(data) & 0xffff;
	fread(data, 1, 4, f);
	fpu_model = gl(data);
	fread(inst_name, 1, sizeof(inst_name) - 1, f);
	inst_name[sizeof(inst_name) - 1] = 0;

	int lvl2 = cpu_lvl;
	if (lvl2 == 5 && lvl2 != lvl)
		lvl2 = 4;

	if (lvl != lvl2) {
		printf("Mismatched CPU model: %lu <> %lu\n", 68000 + 10 * cpu_lvl, 68000 + lvl * 10);
		exit(0);
	}

	if (!check_undefined_sr) {
		sr_undefined_mask = ~sr_undefined_mask;
	} else {
		sr_undefined_mask = 0xffff;
	}

	if (lmem_rom >= 0 && (low_memory_size <= 0 || !low_memory_temp)) {
		printf("lmem.dat required but it was not loaded or was missing.\n");
		return 0;
	}
	if (hmem_rom >= 0 && (high_memory_size <= 0 || !high_memory_temp)) {
		printf("hmem.dat required but it was not loaded or was missing.\n");
		return 0;
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
	load_file(path, "tmem.dat", test_memory, &size, 1);
	if (size != test_memory_size) {
		printf("tmem.dat size mismatch\n");
		exit(0);
	}

	printf("CPUlvl=%d, Mask=%08lx Code=%08lx\n", cpu_lvl, addressing_mask, opcode_memory);
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
			printf("Test data file header mismatch (old test data file?)\n");
			break;
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
	char opcode[16];
	int stop_on_error = 1;

	atexit(freestuff);

#ifndef M68K

	char *params[] = { "", "unpk", "", NULL };
	argv = params;
	argc = 3;

	strcpy(path, "C:\\projects\\winuae\\src\\cputest\\data\\");

	vbr_zero = calloc(1, 1024);

	cpu_lvl = 2;

#else

#define _stricmp stricmp

	if (strlen(argv[1]) >= sizeof(opcode) - 1)
		return 0;

	strcpy(path, "data/");

	low_memory = (uae_u8 *)0;
	high_memory = (uae_u8 *)0xffff8000;

	cpu_lvl = get_cpu_model();

#endif

	if (cpu_lvl == 5) {
#ifdef M68K
		// Overwrite MOVEC to/from MSP
		// with NOPs if 68060
		extern void *msp_address1;
		extern void *msp_address2;
		extern void *msp_address3;
		extern void *msp_address4;
		*((uae_u32*)&msp_address1) = 0x4e714e71;
		*((uae_u32*)&msp_address2) = 0x4e714e71;
		*((uae_u32*)&msp_address3) = 0x4e714e71;
		*((uae_u32*)&msp_address4) = 0x4e714e71;
#endif
	}

	if (argc < 2) {
		printf("cputest <all/mnemonic> (<start mnemonic>) (continue)\n");
		printf("mnemonic = test single mnemonic\n");
		printf("all = test all\n");
		printf("all <mnemonic> = test all, starting from <mnemonic>\n");
		printf("continue = don't stop on error (all mode only)\n");
		printf("ccrmask = ignore CCR bits that are not set.\n");
		return 0;
	}

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
				ccr_mask = ~getparamval(next);
				i++;
			}
		} else if (!_stricmp(s, "68000")) {
			cpu_lvl = 0;
		} else if (!_stricmp(s, "68010")) {
			cpu_lvl = 1;
		} else if (!_stricmp(s, "68020")) {
			cpu_lvl = 2;
		} else if (!_stricmp(s, "68030")) {
			cpu_lvl = 3;
		} else if (!_stricmp(s, "68040")) {
			cpu_lvl = 4;
		} else if (!_stricmp(s, "68060")) {
			cpu_lvl = 5;
		}
	}

	sprintf(path + strlen(path), "%lu/", 68000 + (cpu_lvl == 5 ? 6 : cpu_lvl) * 10);

	low_memory_size = -1;
	low_memory_temp = load_file(path, "lmem.dat", NULL, &low_memory_size, 0);
	high_memory_size = -1;
	high_memory_temp = load_file(path, "hmem.dat", NULL, &high_memory_size, 0);

#ifndef M68K
	if (low_memory_size > 0)
		low_memory = calloc(1, low_memory_size);
	if (high_memory_size > 0)
		high_memory = calloc(1, high_memory_size);
#endif

	if (low_memory_size > 0)
		low_memory_back = calloc(1, low_memory_size);
	if (high_memory_size > 0)
		high_memory_back = calloc(1, high_memory_size);

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
			first = -1;
			for (int i = 0; i < diroff; i += MAX_FILE_LEN) {
				if (!_stricmp(dirs + i, argv[2])) {
					first = i;
					break;
				}
			}
			if (first < 0) {
				printf("Couldn't find '%s'\n", argv[2]);
				return 0;
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

	return 0;
}
