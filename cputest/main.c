

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>

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

extern void callinflate(uae_u8*, uae_u8*,uae_u8*);

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
	uae_u32 expsr;
	uae_u32 exc, exc010;
	uae_u32 excframe;
	struct fpureg fpuregs[8];
	uae_u32 fpiar, fpcr, fpsr;
	uae_u32 tracecnt;
	uae_u16 tracedata[6];
	uae_u32 cycles, cycles2, cyclest;
	uae_u32 srcaddr, dstaddr, branchtarget;
	uae_u8 branchtarget_mode;
	uae_u32 endpc;
};

static struct registers test_regs;
static struct registers last_registers;
static struct registers regs;
static uae_u8 *opcode_memory;
static uae_u32 opcode_memory_addr;
static uae_u8 *low_memory;
static uae_u8 *high_memory;
static int low_memory_size;
static int high_memory_size;
static uae_u32 test_low_memory_start, test_low_memory_end;
static uae_u32 test_high_memory_start, test_high_memory_end;
static uae_u8 *test_memory;
static uae_u32 test_memory_addr, test_memory_end;
static uae_u32 test_memory_size;
static uae_u8 *test_data;
static uae_u8 *safe_memory_start, *safe_memory_end;
static short safe_memory_mode;
static uae_u32 user_stack_memory, super_stack_memory;
static uae_u32 exception_vectors;
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

static uae_u8 *low_memory_temp;
static uae_u8 *high_memory_temp;
static uae_u8 *low_memory_back;
static uae_u8 *high_memory_back;
static int low_memory_offset;
static int high_memory_offset;

static uae_u32 vbr[256];
static int exceptioncount[3][128];
static int supercnt;
static uae_u32 startpc, endpc;

static char inst_name[16+1];
#ifndef M68K
static char outbuffer[40000];
static char outbuffer2[40000];
#else
static char outbuffer[4000];
static char outbuffer2[4000];
#endif
static char tmpbuffer[1024];
static char path[256];

static char *outbp;
static short infoadded;
static int errors;
static int testcnt;
static short dooutput = 1;
static short quit;
static uae_u8 ccr_mask;
static uae_u32 addressing_mask = 0x00ffffff;
static uae_u32 interrupt_mask;
static short disasm;
static short basicexcept;
static short excskipccr;
static short skipmemwrite;
static short skipregchange;
static short skipccrchange;
static short askifmissing;
static short nextall;
static int exitcnt;
static short cycles, cycles_range, cycles_adjust;
static short gotcycles;
static short interrupttest;
static uae_u32 cyclecounter_addr;
#ifdef AMIGA
static short interrupt_count;
static uae_u16 main_intena;
#endif

#define SIZE_STORED_ADDRESS_OFFSET 8
#define SIZE_STORED_ADDRESS 16
static uae_u8 srcaddr[SIZE_STORED_ADDRESS];
static uae_u8 dstaddr[SIZE_STORED_ADDRESS];
static uae_u8 branchtarget[SIZE_STORED_ADDRESS];
static uae_u8 stackaddr[SIZE_STORED_ADDRESS];
static uae_u32 stackaddr_ptr;

static char opcode[32], group[32], cpustr[10];

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
static uae_u32 exceptiontable000, exceptiontable010, exceptiontable020, exceptiontablefpu;
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
static void *error_vector;
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
extern uae_u32 exceptiontable000, exceptiontable010, exceptiontable020, exceptiontablefpu;
extern uae_u32 testexit(void);
extern uae_u32 setvbr(uae_u32);
extern uae_u32 get_cpu_model(void);
extern void setcpu(uae_u32, uae_u32*, uae_u32*);
extern void flushcache(uae_u32);
extern void *error_vector;

#endif
static uae_u32 exceptiontableinuse;

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

static int is_valid_test_addr_read(uae_u32 a)
{
	a &= addressing_mask;
	if ((uae_u8 *)a >= safe_memory_start && (uae_u8 *)a < safe_memory_end && (safe_memory_mode & 1))
		return 0;
	return (a >= test_low_memory_start && a < test_low_memory_end && test_low_memory_start != 0xffffffff) ||
		(a >= test_high_memory_start && a < test_high_memory_end && test_high_memory_start != 0xffffffff) ||
		(a >= test_memory_addr && a < test_memory_end);
}

static int is_valid_test_addr_readwrite(uae_u32 a)
{
	a &= addressing_mask;
	if ((uae_u8 *)a >= safe_memory_start && (uae_u8 *)a < safe_memory_end)
		return 0;
	return (a >= test_low_memory_start && a < test_low_memory_end && test_low_memory_start != 0xffffffff) ||
		(a >= test_high_memory_start && a < test_high_memory_end && test_high_memory_start != 0xffffffff) ||
		(a >= test_memory_addr && a < test_memory_end);
}


static void endinfo(void)
{
	printf("Last test: %u\n", testcnt);
	uae_u8 *p = opcode_memory;
	for (int i = 0; i < 4 * 2; i += 2) {
		if (!is_valid_test_addr_read((uae_u32)(&p[i])))
			break;
		uae_u16 v = (p[i] << 8) | (p[i + 1]);
		printf("%08x %04x\n", (uae_u32)&p[i], v);
		if (v == 0x4afc && i > 0)
			break;
	}
	printf("\n");
}

static void safe_memcpy(uae_u8 *d, uae_u8 *s, int size)
{
	if (safe_memory_start == (uae_u8*)0xffffffff && safe_memory_end == (uae_u8*)0xffffffff) {
		xmemcpy(d, s, size);
		return;
	}
	if (safe_memory_end <= d || safe_memory_start >= d + size) {
		if (safe_memory_end <= s || safe_memory_start >= s + size) {
			xmemcpy(d, s, size);
			return;
		}
	}
	while (size > 0) {
		int size2 = size > sizeof(tmpbuffer) ? sizeof(tmpbuffer) : size;
		if ((d + size2 > safe_memory_start && d < safe_memory_end) ||
			(s + size2 > safe_memory_start && s < safe_memory_end)) {
			for (int i = 0; i < size2; i++) {
				if ((d >= safe_memory_start && d < safe_memory_end) ||
					(s >= safe_memory_start && s < safe_memory_end)) {
					s++;
					d++;
					continue;
				}
				*d++ = *s++;
			}
		} else {
			xmemcpy(d, s, size2);
			d += size2;
			s += size2;
		}
		size -= size2;
	}
}

static int test_active;
static uae_u32 enable_data;
static uae_u32 error_vectors[12];

// if exception happens outside of test code, jump to
// infinite loop and flash colors.
static void reset_error_vectors(void)
{
	uae_u32 *p;
	if (cpu_lvl == 0) {
		p = (uae_u32*)vbr_zero;
	} else {
		p = vbr;
	}
	for (int i = 2; i < 4; i++) {
		p[i] = error_vectors[i - 2];
	}
}

static void set_error_vectors(void)
{
	uae_u32 *p;
	if (cpu_lvl == 0) {
		p = (uae_u32 *)vbr_zero;
	} else {
		p = vbr;
	}
	for (int i = 2; i < 4; i++) {
		p[i] = (uae_u32)&error_vector;
	}
}

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

#ifdef AMIGA
	main_intena = *((volatile uae_u16*)0xdff01c);
#endif

	if (test_low_memory_start != 0xffffffff)
		safe_memcpy(low_memory_back + low_memory_offset, low_memory + low_memory_offset, low_memory_size - low_memory_offset);

	// always copy exception vectors if 68000
	if (cpu_lvl == 0 && low_memory_offset > 0x08)
		safe_memcpy(low_memory_back + 8, low_memory + 8, (192 - 2) * 4);

	if (!hmem_rom && test_high_memory_start != 0xffffffff)
		safe_memcpy(high_memory_back, high_memory + high_memory_offset, high_memory_size - high_memory_offset);

	if (test_low_memory_start != 0xffffffff)
		safe_memcpy(low_memory + low_memory_offset, low_memory_temp + low_memory_offset, low_memory_size - low_memory_offset);

	if (cpu_lvl == 0 && low_memory_offset > 0x08)
		safe_memcpy(low_memory + 8, low_memory_temp + 8, (192 - 2) * 4);

	if (!hmem_rom && test_high_memory_start != 0xffffffff)
		safe_memcpy(high_memory + high_memory_offset, high_memory_temp, high_memory_size - high_memory_offset);

	if (cpu_lvl == 0) {
		uae_u32 *p = (uae_u32 *)vbr_zero;
		for (int i = 2; i < 12; i++) {
			p[i] = (uae_u32)(((uae_u32)&exceptiontable000) + (i - 2) * 2);
			if (exception_vectors && i >= 4) {
				p[i] = exception_vectors;
			}
			if (i < 12 + 2) {
				error_vectors[i - 2] = p[i];
			}
		}
		if (interrupttest) {
			for (int i = 24; i < 24 + 8; i++) {
				p[i] = (uae_u32)(((uae_u32)&exceptiontable000) + (i - 2) * 2);
				if (exception_vectors) {
					p[i] = exception_vectors;
				}
			}
		}
		for (int i = 32; i < 48; i++) {
			p[i] = (uae_u32)(((uae_u32)&exceptiontable000) + (i - 2) * 2);
			if (exception_vectors) {
				p[i] = exception_vectors;
			}
		}
		exceptiontableinuse = (uae_u32)&exceptiontable000;
	} else {
		oldvbr = setvbr((uae_u32)vbr);
		for (int i = 2; i < 48; i++) {
			if (fpu_model) {
				vbr[i] = (uae_u32)(((uae_u32)&exceptiontablefpu) + (i - 2) * 2);
				exceptiontableinuse = (uae_u32)&exceptiontablefpu;
			} else if (cpu_lvl == 1) {
				vbr[i] = (uae_u32)(((uae_u32)&exceptiontable010) + (i - 2) * 2);
				exceptiontableinuse = (uae_u32)&exceptiontable010;
			} else {
				vbr[i] = (uae_u32)(((uae_u32)&exceptiontable020) + (i - 2) * 2);
				exceptiontableinuse = (uae_u32)&exceptiontable020;
			}
			if (i >= 2 && i < 12) {
				error_vectors[i - 2] = vbr[i];
			}
			if (exception_vectors && i >= 4) {
				vbr[i] = exception_vectors;
			}
		}
	}
	setcpu(cpu_lvl, cpustatearraynew, cpustatearraystore);
}

static void end_test(void)
{
	if (!test_active)
		return;
	test_active = 0;

	if (test_low_memory_start != 0xffffffff)
		safe_memcpy(low_memory + low_memory_offset, low_memory_back + low_memory_offset, low_memory_size - low_memory_offset);

	if (cpu_lvl == 0 && low_memory_offset > 0x08)
		safe_memcpy(low_memory + 8, low_memory_back + 8, (192 - 2) * 4);

	if (!hmem_rom && test_high_memory_start != 0xffffffff)
		safe_memcpy(high_memory + high_memory_offset, high_memory_back, high_memory_size - high_memory_offset);

	if (cpu_lvl > 0) {
		setvbr(oldvbr);
	}
	setcpu(cpu_lvl, cpustatearraystore, NULL);

#ifdef AMIGA
	*((volatile uae_u16*)0xdff09a) = 0x7fff;
	*((volatile uae_u16*)0xdff09c) = 0x7fff;
	*((volatile uae_u16*)0xdff09a) = main_intena | 0x8000;
#endif

	touser(enable_data);
}

static int readdata(uae_u8 *p, int size, FILE *f, uae_u8 *unpack, int *offsetp)
{
	if (!unpack) {
		return fread(p, 1, size, f);
	} else {
		memcpy(p, unpack + (*offsetp), size);
		(*offsetp) += size;
		return size;
	}
}
static void seekdata(int seek, FILE *f, uae_u8 *unpack, int *offsetp)
{
	if (!unpack) {
		fseek(f, seek, SEEK_CUR);
	} else {
		(*offsetp) += seek;
	}
}

static uae_u8 *parse_gzip(uae_u8 *gzbuf, int *sizep)
{
	uae_u8 *end = gzbuf + (*sizep);
	uae_u8 flags = gzbuf[3];
	uae_u16 v;
	if (gzbuf[0] != 0x1f && gzbuf[1] != 0x8b)
		return NULL;
	gzbuf += 10;
	if (flags & 2) /* multipart not supported */
		return NULL;
	if (flags & 32) /* encryption not supported */
		return NULL;
	if (flags & 4) { /* skip extra field */
		v = *gzbuf++;
		v |= (*gzbuf++) << 8;
		gzbuf += v + 2;
	}
	if (flags & 8) { /* get original file name */
		while (*gzbuf++);
	}
	if (flags & 16) { /* skip comment */
		while (*gzbuf++);
	}
	*sizep = (end[-4] << 0) | (end[-3] << 8) | (end[-2] << 16) | (end[-1] << 24);
	return gzbuf;
}

#define INFLATE_STACK_SIZE 3000
static uae_u8 *inflatestack;

static uae_u8 *load_file(const char *path, const char *file, uae_u8 *p, int *sizep, int exiterror, int candirect)
{
	char fname[256];
	uae_u8 *unpack = NULL;
	int unpackoffset = 0;
	int size = 0;

	sprintf(fname, "%s%s.gz", path, file);
	FILE *f = fopen(fname, "rb");
	if (f) {
		fseek(f, 0, SEEK_END);
		int gsize = ftell(f);
		fseek(f, 0, SEEK_SET);
		uae_u8 *gzbuf = malloc(gsize);
		if (!gzbuf) {
			printf("Couldn't allocate %d bytes (packed), file '%s'\n", gsize, fname);
			exit(0);
		}
		if (fread(gzbuf, 1, gsize, f) != gsize) {
			printf("Couldn't read file '%s'\n", fname);
			exit(0);
		}
		fclose(f);
		size = gsize;
		uae_u8 *gzdata = parse_gzip(gzbuf, &size);
		if (!gzdata) {
			printf("Couldn't parse gzip file '%s'\n", fname);
			exit(0);
		}
		f = NULL;
		if (!inflatestack) {
			inflatestack = malloc(INFLATE_STACK_SIZE);
			if (!inflatestack) {
				printf("Couldn't allocate %d bytes (inflate stack)\n", INFLATE_STACK_SIZE);
				exit(0);
			}
			inflatestack += INFLATE_STACK_SIZE;
		}
		if (!p) {
			p = calloc(1, size);
			if (!p) {
				printf("Couldn't allocate %d bytes, file '%s'\n", size, fname);
				exit(0);
			}
			printf("Decompressing '%s' (%d -> %d)\n", fname, gsize, size);
			callinflate(p, gzdata, inflatestack);
			*sizep = size;
			return p;
		} else if (candirect) {
			printf("Decompressing '%s' (%d -> %d)\n", fname, gsize, size);
			callinflate(p, gzdata, inflatestack);
			*sizep = size;
			return p;
		} else {
			unpack = calloc(1, size);
			if (!unpack) {
				printf("Couldn't allocate %d bytes (unpack), file '%s'\n", size, fname);
				exit(0);
			}
			printf("Decompressing '%s' (%d -> %d)\n", fname, gsize, size);
			callinflate(unpack, gzdata, inflatestack);
			*sizep = size;
		}
	}
	if (!unpack) {
		sprintf(fname, "%s%s", path, file);
		f = fopen(fname, "rb");
		if (!f) {
			if (exiterror) {
				printf("Couldn't open '%s'\n", fname);
				exit(0);
			}
			return NULL;
		}
		size = *sizep;
		if (size < 0) {
			fseek(f, 0, SEEK_END);
			size = ftell(f);
			fseek(f, 0, SEEK_SET);
		}
	}
	if (!p) {
		p = calloc(1, size);
		if (!p) {
			printf("Couldn't allocate %d bytes, file '%s'\n", size, fname);
			exit(0);
		}
	}
	if (safe_memory_end < p || safe_memory_start >= p + size) {
		*sizep = readdata(p, size, f, unpack, &unpackoffset);
	} else {
		if (size > 0 && p < safe_memory_start) {
			int size2 = safe_memory_start - p;
			if (size2 > size)
				size2 = size;
			if (readdata(p, size2, f, unpack, &unpackoffset) != size2)
				goto end;
			p += size2;
			size -= size2;
		}
		if ((safe_memory_mode & 1)) {
			// if reading cause bus error: skip it
			if (size > 0 && p >= safe_memory_start && p < safe_memory_end) {
				int size2 = safe_memory_end - p;
				if (size2 > size)
					size2 = size;
				seekdata(size2, f, unpack, &unpackoffset);
				p += size2;
				size -= size2;
			}
		} else if (safe_memory_mode == 2) {
			// if only writes generate bus error: load data if different
			if (size > 0 && p >= safe_memory_start && p < safe_memory_end) {
				int size2 = safe_memory_end - p;
				if (size2 > size)
					size2 = size;
				uae_u8 *tmp = malloc(size2);
				if (!tmp) {
					printf("Couldn't allocate safe tmp memory (%d bytes)\n", size2);
					exit(0);
				}
				readdata(tmp, size2, f, unpack, &unpackoffset);
				if (memcmp(tmp, p, size2)) {
					printf("Disable write bus error mode and press any key (SPACE=skip,ESC=abort)\n");
					int ch = getchar();
					if (ch == 27) {
						exit(0);
					} else if (ch == 32) {
						seekdata(size2, f, unpack, &unpackoffset);
						p += size2;
						size -= size2;
					} else {
						memcpy(p, tmp, size2);
						p += size2;
						size -= size2;
						printf("Re-enable write bus error mode and press any key (ESC=abort)\n");
						if (getchar() == 27) {
							exit(0);
						}
					}
				} else {
					printf("Write-only bus error mode, data already correct. Skipping read.\n");
					p += size2;
					size -= size2;
				}
				free(tmp);
			}
		}
		if (size > 0) {
			if (readdata(p, size, f, unpack, &unpackoffset) != size)
				goto end;
		}
		size = *sizep;
	}
	if (*sizep != size) {
end:
		printf("Couldn't read file '%s' (%d <> %d)\n", fname, *sizep, size);
		exit(0);
	}
	if (f) {
		fclose(f);
	}
	if (unpack) {
		free(unpack);
	}
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
				if (test_low_memory_start != 0xffffffff && (val & addressing_mask) < low_memory_size) {
					; // low memory
				} else if (test_high_memory_start != 0xffffffff && (val & ~addressing_mask) == ~addressing_mask && val >= 0xfff80000) {
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
	if (ahcnt >= MAX_ACCESSHIST) {
		end_test();
		printf("History index too large! %d >= %d\n", ahcnt, MAX_ACCESSHIST);
		exit(0);
	}

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
	if (!ahcnt)
		return;
	for (int i = ahcnt - 1; i >= 0; i--) {
		struct accesshistory *ah = &ahist[i];
		tomem(ah->addr, ah->oldval, 0, ah->size, 0);
	}
	ahcnt = 0;
}

static uae_u8 *restore_bytes(uae_u8 *mem, uae_u8 *p)
{
	uae_u8 *addr = mem;
	uae_u8 v = *p++;
	addr += v >> 5;
	v &= 31;
	if (v == 0)
		v = 32;
#ifndef _MSC_VER
	xmemcpy(addr, p, v);
#endif
	p += v;
	return p;
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
				p = restore_bytes(opcode_memory, p + 1);
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
		printf("Unexpected end bit!? offset %d\n", p - test_data);
		endinfo();
		exit(0);
	}
	int mode = v & CT_DATA_MASK;
	if (mode == CT_SRCADDR) {
		int size;
		p = restore_value(p, &regs.srcaddr, &size);
	} else if (mode == CT_DSTADDR) {
		int size;
		p = restore_value(p, &regs.dstaddr, &size);
	} else if (mode == CT_ENDPC) {
		int size;
		p = restore_value(p, &regs.endpc, &size);
	} else if (mode == CT_PC) {
		int size;
		p = restore_value(p, &regs.pc, &size);
	} else if (mode == CT_BRANCHTARGET) {
		int size;
		p = restore_value(p, &regs.branchtarget, &size);
		regs.branchtarget_mode = *p++;
	} else if (mode < CT_AREG + 8) {
		int size;
		if ((v & CT_SIZE_MASK) == CT_SIZE_FPU) {
			p = restore_fpvalue(p, &regs.fpuregs[mode]);
		} else {
			p = restore_value(p, &regs.regs[mode], &size);
		}
	} else if (mode == CT_SR) {
		int size;
		p = restore_value(p, &regs.sr, &size);
	} else if (mode == CT_CYCLES) {
		int size;
		p = restore_value(p, &regs.cycles, &size);
		gotcycles = 1;
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

static uae_u16 test_sr, test_ccrignoremask;
static uae_u32 test_fpsr, test_fpcr;

static int addr_diff(uae_u8 *ap, uae_u8 *bp, int size)
{
	for (int i = 0; i < size; i++) {
		if (is_valid_test_addr_read((uae_u32)bp)) {
			if (*ap != *bp)
				return 1;
		}
		ap++;
		bp++;
	}
	return 0;
}

static void addinfo_bytes(char *name, uae_u8 *src, uae_u32 address, int offset, int len)
{
	sprintf(outbp, "%s %08x ", name, address);
	address += offset;
	outbp += strlen(outbp);
	int cnt = 0;
	while (len-- > 0) {
		if (offset == 0)
			*outbp++ = '*';
		else if (cnt > 0)
			*outbp++ = '.';
		if ((uae_u8*)address >= safe_memory_start && (uae_u8*)address < safe_memory_end && (safe_memory_mode & (1 | 4))) {
			outbp[0] = '?';
			outbp[1] = '?';
		} else {
			sprintf(outbp, "%02x", src[cnt]);
		}
		outbp += 2;
		offset++;
		address++;
		cnt++;
	}
	*outbp++ = '\n';
}

extern uae_u16 disasm_instr(uae_u16 *, char *, int);

static void out_disasm(uae_u8 *mem)
{
	uae_u16 *code;
#ifndef M68K
	uae_u16 swapped[16];
	for (int i = 0; i < 16; i++) {
		swapped[i] = (mem[i * 2 + 0] << 8) | (mem[i * 2 + 1] << 0);
	}
	code = swapped;
#else
	code = (uae_u16*)mem;
#endif
	uae_u8 *p = mem;
	int offset = 0;
	int lines = 0;
	while (lines++ < 7) {
		int v = 0;
		if (!is_valid_test_addr_read((uae_u32)p) || !is_valid_test_addr_read((uae_u32)p + 1)) {
			sprintf(outbp, "%08x -- INACCESSIBLE --\n", (uae_u32)p);
			outbp += strlen(outbp);
			break;
		}
		tmpbuffer[0] = 0;
		if (!(((uae_u32)code) & 1)) {
			v = disasm_instr(code + offset, tmpbuffer, cpu_lvl);
			sprintf(outbp, "%08x ", (uae_u32)p);
			outbp += strlen(outbp);
			for (int i = 0; i < v; i++) {
				uae_u16 v = (p[i * 2 + 0] << 8) | (p[i * 2 + 1]);
				sprintf(outbp, "%04x ", v);
				outbp += strlen(outbp);
				if (v == 0x4e71)
					lines--;
			}
			sprintf(outbp, " %s\n", tmpbuffer);
			outbp += strlen(outbp);
			if (v <= 0 || code[offset] == 0x4afc)
				break;
			while (v > 0) {
				offset++;
				p += 2;
				v--;
			}
		} else {
			sprintf(outbp, "%08x %02x\n", (uae_u32)code, *((uae_u8*)code));
			code = (uae_u16*)(((uae_u32)code) + 1);
			p++;
			outbp += strlen(outbp);
		}
		if (v < 0)
			break;
	}
	*outbp = 0;
}

static void addinfo(void)
{
	if (infoadded)
		return;
	infoadded = 1;
	if (!dooutput)
		return;

	if (disasm) {
		out_disasm(opcode_memory);
	}

	if (regs.branchtarget != 0xffffffff) {
		out_disasm((uae_u8*)regs.branchtarget);
	}

	uae_u16 *code = (uae_u16*)opcode_memory;
	if (code[0] == 0x4e73 || code[0] == 0x4e74 || code[0] == 0x4e75 || code[0] == 0x4e77) {
		addinfo_bytes("P", stackaddr, stackaddr_ptr, -SIZE_STORED_ADDRESS_OFFSET, SIZE_STORED_ADDRESS);
		addinfo_bytes(" ", (uae_u8 *)stackaddr_ptr - SIZE_STORED_ADDRESS_OFFSET, stackaddr_ptr, -SIZE_STORED_ADDRESS_OFFSET, SIZE_STORED_ADDRESS);
	}
	if (regs.srcaddr != 0xffffffff) {
		uae_u8 *a = srcaddr;
		uae_u8 *b = (uae_u8 *)regs.srcaddr - SIZE_STORED_ADDRESS_OFFSET;
		addinfo_bytes("S", a, regs.srcaddr, -SIZE_STORED_ADDRESS_OFFSET, SIZE_STORED_ADDRESS);
		if (addr_diff(a, b, SIZE_STORED_ADDRESS)) {
			addinfo_bytes(" ", b, regs.srcaddr, -SIZE_STORED_ADDRESS_OFFSET, SIZE_STORED_ADDRESS);
		}
	}
	if (regs.dstaddr != 0xffffffff) {
		uae_u8 *a = dstaddr;
		uae_u8 *b = (uae_u8*)regs.dstaddr - SIZE_STORED_ADDRESS_OFFSET;
		addinfo_bytes("D", a, regs.dstaddr, -SIZE_STORED_ADDRESS_OFFSET, SIZE_STORED_ADDRESS);
		if (addr_diff(a, b, SIZE_STORED_ADDRESS)) {
			addinfo_bytes(" ", b, regs.dstaddr, -SIZE_STORED_ADDRESS_OFFSET, SIZE_STORED_ADDRESS);
		}
	}
	if (regs.branchtarget != 0xffffffff && regs.srcaddr != regs.branchtarget && regs.dstaddr != regs.branchtarget) {
		uae_u8 *b = (uae_u8 *)regs.branchtarget - SIZE_STORED_ADDRESS_OFFSET;
		addinfo_bytes("B", b, regs.branchtarget, -SIZE_STORED_ADDRESS_OFFSET, SIZE_STORED_ADDRESS);
	}
//	sprintf(outbp, "STARTPC=%08x ENDPC=%08x\n", startpc, endpc);
//	outbp += strlen(outbp);
}

struct srbit
{
	char *name;
	int bit;
};
static const struct srbit srbits[] = {
	{ "T1", 15 },
	{ "T0", 14 },
	{ "S", 13 },
	{ "M", 12 },
	{ "X", 4 },
	{ "N", 3 },
	{ "Z", 2 },
	{ "V", 1 },
	{ "C", 0 },
	{ NULL, 0 }
};

static void out_regs(struct registers *r, int before)
{
	if (before) {
		for (int i = 0; i < 16; i++) {
			if (i > 0 && (i % 4) == 0) {
				strcat(outbp, "\n");
			} else if ((i % 8) != 0) {
				strcat(outbp, " ");
			}
			outbp += strlen(outbp);
			sprintf(outbp, "%c%d:%c%08x", i < 8 ? 'D' : 'A', i & 7, test_regs.regs[i] != last_registers.regs[i] ? '*' : ' ', r->regs[i]);
			outbp += strlen(outbp);
		}
		*outbp++ = '\n';
		sprintf(outbp, "SR:%c%04x      PC: %08x ISP: %08x", test_sr != last_registers.sr ? '*' : ' ', test_sr, r->pc, r->ssp);
	} else {
		// output only lines that have at least one modified register to save screen space
		for (int i = 0; i < 4; i++) {
			int diff = 0;
			for (int j = 0; j < 4; j++) {
				int idx = i * 4 + j;
				if (test_regs.regs[idx] != regs.regs[idx]) {
					diff = 1;
				}
			}
			if (diff) {
				for (int j = 0; j < 4; j++) {
					int idx = i * 4 + j;
					if (j > 0)
						*outbp++ = ' ';
					sprintf(outbp, "%c%d:%c%08x", idx < 8 ? 'D' : 'A', idx & 7, test_regs.regs[idx] != last_registers.regs[idx] ? '*' : ' ', test_regs.regs[idx]);
					outbp += strlen(outbp);
				}
				*outbp++ = '\n';
			}
		}
		sprintf(outbp, "SR:%c%04x/%04x PC: %08x ISP: %08x", test_sr != last_registers.sr ? '*' : ' ', test_regs.sr, test_regs.expsr, r->pc, r->ssp);
	}
	outbp += strlen(outbp);
	if (cpu_lvl >= 2 && cpu_lvl <= 4) {
		sprintf(outbp, " MSP: %08x", r->msp);
		outbp += strlen(outbp);
	}
	*outbp++ = '\n';

	if (before >= 0) {
		uae_u16 s = before ? test_sr : test_regs.sr; // current value
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
		*outbp++ = '\n';
	}

	if (!fpu_model)
		return;

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
		sprintf(outbp, "FP%d:%c%04x-%08x%08x %f",
			i,
			memcmp(f1, f2, sizeof(struct fpureg)) ? '*' : ' ',
			f->exp, f->m[0], f->m[1],
			*((long double*)f));
		outbp += strlen(outbp);
	}
	sprintf(outbp, "\nFPSR:%c%08x FPCR:%c%08x FPIAR:%c%08x\n",
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

static uae_u8 last_exception[256], last_exception_extra;
static int last_exception_len;
static uae_u8 alternate_exception1[256];
static uae_u8 alternate_exception2[256];
static uae_u8 alternate_exception3[256];
static uae_u8 masked_exception[256];
static int mask_exception;
static int exception_stored;

static int compare_exception(uae_u8 *s1, uae_u8 *s2, int len, int domask, uae_u8 *mask)
{
	if (!domask) {
		return memcmp(s1, s2, len);
	} else {
		for (int i = 0; i < len; i++) {
			if (mask[i])
				continue;
			if (s1[i] != s2[i])
				return 1;
		}
		return 0;
	}
}

static uae_u8 *validate_exception(struct registers *regs, uae_u8 *p, short excnum, short *gotexcnum, short *experr, short *extratrace)
{
	int exclen = 0;
	uae_u8 *exc;
	uae_u8 *op = p;
	uae_u8 *sp = (uae_u8*)regs->excframe;
	uae_u32 v;
	uae_u8 excdatalen = *p++;
	int size;
	int excrwp = 0;
	int alts = 0;

	mask_exception = 0;
	if (!excdatalen) {
		return p;
	}

	if (excdatalen != 0xff) {
		// check possible extra trace
		last_exception_extra = *p++;
		if ((last_exception_extra & 0x7f) == 9) {
			exceptioncount[0][last_exception_extra & 0x7f]++;
			uae_u32 ret = (regs->tracedata[1] << 16) | regs->tracedata[2];
			uae_u16 sr = regs->tracedata[0];
			if (regs->tracecnt == 0) {
				sprintf(outbp, "Expected trace exception but got none\n");
				outbp += strlen(outbp);
				*experr = 1;
			} else if (!(last_exception_extra & 0x80)) {
				// Trace stacked with group 2 exception
				if (!(sr & 0x2000) || (sr | 0x2000 | 0xc000) != (regs->sr | 0x2000 | 0xc000)) {
					sprintf(outbp, "Trace (%d stacked) SR mismatch: %04x != %04x\n", excnum, sr, regs->sr);
					outbp += strlen(outbp);
					*experr = 1;
				}
				uae_u32 retv = exceptiontableinuse + (excnum - 2) * 2;
				if (ret != retv) {
					sprintf(outbp, "Trace (%d stacked) PC mismatch: %08x != %08x\n", excnum, ret, retv);
					outbp += strlen(outbp);
					*experr = 1;
				}
				*extratrace = 1;
			} else {
				// Standalone Trace
				uae_u16 vsr = (p[0] << 8) | (p[1]);
				p += 2;
				v = opcode_memory_addr;
				p = restore_rel_ordered(p, &v);
				if (vsr != sr) {
					sprintf(outbp, "Trace (non-stacked) SR mismatch: %04x != %04x (PC=%08x)\n", sr, vsr, v);
					outbp += strlen(outbp);
					*experr = 1;
				}
				if (v != ret) {
					sprintf(outbp, "Trace (non-stacked) PC mismatch: %08x != %08x (SR=%04x)\n", ret, v, vsr);
					outbp += strlen(outbp);
					*experr = 1;
				}
			}
		} else if (!last_exception_extra && excnum != 9) {
			if (regs->tracecnt > 0) {
				uae_u32 ret = (regs->tracedata[1] << 16) | regs->tracedata[2];
				uae_u16 sr = regs->tracedata[0];
				sprintf(outbp, "Got unexpected trace exception: SR=%04x PC=%08x.\n", sr, ret);
				outbp += strlen(outbp);
				if (excnum >= 2) {
					sprintf(outbp, "Exception %d also pending.\n", excnum);
					outbp += strlen(outbp);
				}
				*experr = 1;
			}
		} else if (last_exception_extra) {
			end_test();
			printf("Unsupported exception extra %d\n", last_exception_extra);
			exit(0);
		}
		// trace only
		if (excnum == 9 && *gotexcnum == 4) {
			sp = (uae_u8 *)regs->tracedata;
			*gotexcnum = 9;
		}
	}

	last_exception_len = 0;
	if (excnum == 1)
		return p;

	exc = last_exception;
	if (excdatalen != 0xff) {
		if (cpu_lvl == 0) {
			if (excnum == 2 || excnum == 3) {
				// status (with undocumented opcode part)
				uae_u8 status = p[0];
				uae_u8 opcode0 = p[1];
				uae_u8 opcode1 = p[2];
				p += 1 + 2;
				uae_u8 opcode0b = opcode0;
				uae_u8 opcode1b = opcode1;
				if (status & 0x20) {
					opcode0b = p[0];
					opcode1b = p[1];
					p += 2;
				}
				exc[0] = opcode0;
				exc[1] = (opcode1 & ~0x1f) | (status & 0x1f);
				excrwp = ((status & 0x10) == 0) ? 1 : 0;
				if (status & 2)
					excrwp = 2;
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
				if (basicexcept) {
					// I/N field is not always as documented
					memcpy(alternate_exception1, exc, exclen);
					alternate_exception1[1] ^= 0x08; // I/N
					alts = 1;
					if (status & 0x80) {
						// opcode field is not always current opcode
						memcpy(alternate_exception2, exc, exclen);
						alternate_exception2[0] = opcode0b;
						alternate_exception2[1] = (opcode1b & ~0x1f) | (status & 0x1f);
						alternate_exception2[6] = opcode0b;
						alternate_exception2[7] = opcode1b;
						memcpy(alternate_exception3, alternate_exception2, exclen);
						alternate_exception3[1] ^= 0x08; // I/N
						alts += 2;
					}
				}
			}
		} else if (cpu_lvl > 0) {
			// sr
			exc[0] = regs->sr >> 8;
			exc[1] = regs->sr;
			pl(exc + 2, regs->pc);
			const uae_u16 t0 = *p++;
			const uae_u16 t1 = *p++;
			// frame type
			uae_u16 frame = (t0 << 8) | t1;
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
			case 8: // 68010 bus/address error
				{
					exc[8] = *p++;
					exc[9] = *p++;
					excrwp = ((exc[8] & 1) == 0) ? 1 : 0;
					if (exc[9] & 2)
						excrwp = 2;
					uae_u32 fault_addr = opcode_memory_addr;
					p = restore_rel_ordered(p, &fault_addr);
					pl(exc + 10, fault_addr);
					// data out
					exc[16] = *p++;
					exc[17] = *p++;
					// data in
					exc[20] = *p++;
					exc[21] = *p++;
					// inst
					exc[24] = *p++;
					exc[25] = *p++;
					exc[14] = exc[15] = 0;
					sp[14] = sp[15] = 0;
					exc[18] = exc[19] = 0;
					sp[18] = sp[19] = 0;
					exc[22] = exc[23] = 0;
					sp[22] = sp[23] = 0;
					if (basicexcept) {
						exclen = 14;
					} else {
						// ignore undocumented data
						exclen = 26;
						// read input buffer may contain either actual data read from memory or previous read data
						// this depends on hardware, cpu does dummy read cycle and some hardware returns memory data, some ignore it.
						memcpy(alternate_exception1, exc, exclen);
						memcpy(alternate_exception2, exc, exclen);
						alternate_exception1[20] = p[0];
						alternate_exception1[21] = p[1];
						memcpy(alternate_exception3, alternate_exception1, exclen);
						// same with instruction input buffer if branch instruction generates address error
						alternate_exception2[24] = p[0];
						alternate_exception2[25] = p[1];
						alternate_exception3[24] = p[0];
						alternate_exception3[25] = p[1];
						if (excnum == 2 || !is_valid_test_addr_readwrite(fault_addr - 4) || !is_valid_test_addr_readwrite(fault_addr + 4)) {
							// bus error read: cpu may still read the data, depends on hardware.
							// ignore input buffer contents
							mask_exception = 1;
							memset(masked_exception, 0, sizeof(masked_exception));
							masked_exception[20] = 1;
							masked_exception[21] = 1;
						}
						alts = 3;
					}
					p += 2;
				}
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
			printf("Exception %d length mismatch %d != %d\n", excnum, excdatalen, p - op - 1);
			exit(0);
		}
	} else {
		exclen = last_exception_len;
	}

	exceptioncount[excrwp][*gotexcnum]++;

	if (exclen == 0 || *gotexcnum != excnum)
		return p;
	int err = 0;
	if (compare_exception(exc, sp, exclen, mask_exception, masked_exception)) {
		err = 1;
		if (err && alts > 0) {
			if (alts >= 1 && !compare_exception(alternate_exception1, sp, exclen, mask_exception, masked_exception))
				err = 0;
			if (alts >= 2 && !compare_exception(alternate_exception2, sp, exclen, mask_exception, masked_exception))
				err = 0;
			if (alts >= 3 && !compare_exception(alternate_exception3, sp, exclen, mask_exception, masked_exception))
				err = 0;
		}
	}
	if (err) {
		sprintf(outbp, "Exception %d stack frame mismatch:\n", excnum);
		outbp += strlen(outbp);
		strcpy(outbp, "Expected: ");
		outbp += strlen(outbp);
		hexdump(exc, exclen);
		strcpy(outbp, "Got     : ");
		outbp += strlen(outbp);
		hexdump(sp, exclen);
		*experr = 1;
	}
	exception_stored = exclen;
	return p;
}

static int getexceptioncycles(int exc)
{
	if (cpu_lvl == 0) {
		switch (exc)
		{
		case 2:
		case 3:
			return 58;
		case 4:
		case 5:
		case 6:
		case 8:
		case 9:
		case 10:
		case 11:
			return 34;
		case 7:
			return 30;
		case 24:
		case 25:
		case 26:
		case 27:
		case 28:
		case 29:
		case 30:
		case 31:
			return 44;
		default:
			return 34;
		}
	} else {
		switch (exc)
		{
		case 2:
		case 3:
			return 126;
		case 4:
		case 5:
		case 6:
		case 8:
		case 9:
		case 10:
		case 11:
		case 14:
			return 38;
		case 7:
			return 38;
		case 24:
		case 25:
		case 26:
		case 27:
		case 28:
		case 29:
		case 30:
		case 31:
			return 48;
		default:
			return 38;
		}
	}
}

#ifdef AMIGA
// 7MHz 68000 PAL A500 only!
static int get_cycles_amiga(void)
{
	uae_u16 vstart = (test_regs.cycles >> 24) & 0xff;
	uae_u16 vend = (test_regs.cycles >> 8) & 0xff;
	uae_u16 hstart = (test_regs.cycles >> 16) & 0xff;
	uae_u16 hend = (test_regs.cycles >> 0) & 0xff;

	// trace exception?
	if (test_regs.cyclest != 0xffffffff) {
		vend = (test_regs.cyclest >> 8) & 0xff;
		hend = (test_regs.cyclest >> 0) & 0xff;
	}

	if (test_regs.cycles2 & 0x00010000) {
		if (vstart > vend) {
			vstart += 0x100;
			vend += 0x138 + 1;
		} else {
			vstart += 0x100;
			vend += 0x100;
		}
	} else {
		if (vstart > vend) {
			vend += 0x100;
		}
	}

	// hpos 0-1: vertical count hasn't been increased yet
	if (hstart <= 1) {
		vstart++;
	}
	if (hend <= 1) {
		vend++;
	}

	if (hstart >= hend) {
		hend += 227;
		vend--;
	}
	int startcycle = vstart * 227 + hstart;
	int endcycle = vend * 227 + hend;
	int gotcycles = (endcycle - startcycle) * 2;
	return gotcycles;
}
#endif

static int check_cycles(int exc, int extratrace)
{
	int gotcycles = 0;

	if (cyclecounter_addr != 0xffffffff) {
		gotcycles = (test_regs.cycles & 0xffff) - (test_regs.cycles >> 16);
		if (gotcycles == 0) {
			end_test();
			printf("Cycle counter hardware address 0x%08x returned zero cycles.\n", cyclecounter_addr);
			exit(0);
		}
	} else {
#ifdef AMIGA
		gotcycles = get_cycles_amiga();
#else
		end_test();
		printf("No cycle count support\n");
		exit(0);
#endif
	}

	int expectedcycles = last_registers.cycles;
	int exceptioncycles = getexceptioncycles(exc);
	if (cpu_lvl == 0) {
		// move.w CYCLEREG,cycles
		gotcycles -= 20;
		// RTE
		gotcycles -= 20;
		// <test instruction>
		// EXCEPTION
		expectedcycles += exceptioncycles;
		// bsr.b
		gotcycles -= 18;
		// move.w sr,-(sp)
		gotcycles -= 14;
		// move.w CYCLEREG,cycles
		gotcycles -= 8;
	} else {
		// move.w CYCLEREG,cycles
		gotcycles -= 20;
		// move.w #x,dummy
		gotcycles -= 20;
		// RTE
		gotcycles -= 24;
		// <test instruction>
		// ILLEGAL
		expectedcycles += exceptioncycles;
		// bsr.b
		gotcycles -= 18;
		// move.w sr,-(sp)
		gotcycles -= 12;
		// move.w CYCLEREG,cycles
		gotcycles -= 8;
	}
	gotcycles += cycles_adjust;

	if (extratrace) {
		expectedcycles += getexceptioncycles(9);
	}

	if (0 || abs(gotcycles - expectedcycles) > cycles_range) {
		addinfo();
		sprintf(outbp, "Got %d cycles (%d + %d) but expected %d (%d + %d) cycles\n",
			gotcycles, gotcycles - exceptioncycles, exceptioncycles,
			expectedcycles, expectedcycles - exceptioncycles, exceptioncycles);
		outbp += strlen(outbp);
		return 0;
	}
	return 1;
}

// regs: registers before execution of test code
// test_reg: registers used during execution of test code, also modified by test code.
// last_registers: registers after modifications from data files. Test ok if test_reg == last_registers.

static uae_u8 *validate_test(uae_u8 *p, short ignore_errors, short ignore_sr)
{
	uae_u8 regs_changed[16] = { 0 };
	uae_u8 regs_fpuchanged[8] = { 0 };
	uae_u8 sr_changed = 0, ccr_changed = 0, pc_changed = 0;
	uae_u8 fpiar_changed = 0, fpsr_changed = 0, fpcr_changed = 0;
	short exc = -1;

	for (int i = 0; i < 16; i++) {
		if (last_registers.regs[i] != test_regs.regs[i]) {
			regs_changed[i] = 1;
		}
	}
	if ((last_registers.sr & test_ccrignoremask & 0xff00) != (test_regs.sr & test_ccrignoremask & 0xff00)) {
		sr_changed = 1;
	}
	if ((last_registers.sr & test_ccrignoremask & 0x00ff) != (test_regs.sr & test_ccrignoremask & 0x00ff)) {
		ccr_changed = 1;
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

	short experr = 0;
	int errflag = 0;
	int errflag_orig = 0;
	short extratrace = 0;
	short exceptionnum = 0;
	uae_u8 *outbp_old = outbp;
	exception_stored = 0;

	for (;;) {
		uae_u8 v = *p;
		if (v & CT_END) {
			exc = v & CT_EXCEPTION_MASK;
			short cpuexc = test_regs.exc & 65535;
			short cpuexc010 = test_regs.exc010 & 65535;
			p++;
			if ((v & CT_END_INIT) == CT_END_INIT) {
				end_test();
				printf("Unexpected CT_END_INIT %02x %08x\n", v, p - test_data);
				endinfo();
				exit(0);
			}
			if (cpu_lvl > 0 && exc >= 2 && cpuexc010 != cpuexc) {
				if (dooutput) {
					sprintf(outbp, "Exception: vector number does not match vector offset! (%d <> %d)\n", exc, cpuexc010);
					experr = 1;
					outbp += strlen(outbp);
					errflag |= 1 << 16;
				}
				break;
			}
			if (ignore_errors) {
				if (exc) {
					p = validate_exception(&test_regs, p, exc, &cpuexc, &experr, &extratrace);
				}
				errflag_orig |= errflag;
				errflag = 0;
				break;
			}
			if (exc == 0 && cpuexc == 4) {
				// successful complete generates exception 4 with matching PC
				if (last_registers.pc != test_regs.pc && dooutput) {
					sprintf(outbp, "PC: expected %08x but got %08x\n", last_registers.pc, test_regs.pc);
					outbp += strlen(outbp);
					errflag |= 1 << 16;
				}
				break;
			}
			if (exc) {
				p = validate_exception(&test_regs, p, exc, &cpuexc, &experr, &extratrace);
				if (experr) {
					errflag |= 1 << 16;
					errflag_orig |= errflag;
				}
				if (basicexcept && (cpuexc == 2 || cpuexc == 3)) {
					errflag &= ~(1 << 0);
					errflag &= ~(1 << 7);
				}
				exceptionnum = cpuexc;
			}
			if (exc != cpuexc && exc >= 2) {
				if (dooutput) {
					if (cpuexc == 4 && last_registers.pc == test_regs.pc) {
						sprintf(outbp, "Exception: expected %d but got no exception.\n", exc);
					} else if (cpuexc == 4) {
						sprintf(outbp, "Exception: expected %d but got %d (or no exception)\n", exc, cpuexc);
					} else {
						sprintf(outbp, "Exception: expected %d but got %d\n", exc, cpuexc);
					}
					experr = 1;
				}
				outbp += strlen(outbp);
				errflag |= 1 << 16;
			}
			break;
		}
		short mode = v & CT_DATA_MASK;

		if (mode < CT_AREG + 8 && (v & CT_SIZE_MASK) != CT_SIZE_FPU) {
			uae_u32 val = last_registers.regs[mode];
			int size;
			p = restore_value(p, &val, &size);
			if (val != test_regs.regs[mode] && !ignore_errors && !skipregchange) {
				if (dooutput) {
					sprintf(outbp, "%c%d: expected %08x but got %08x\n", mode < CT_AREG ? 'D' : 'A', mode & 7, val, test_regs.regs[mode]);
					outbp += strlen(outbp);
				}
				errflag |= 1 << 0;
			}
			regs_changed[mode] = 0;
			last_registers.regs[mode] = val;
		} else if (mode < CT_AREG && (v & CT_SIZE_MASK) == CT_SIZE_FPU && !skipregchange) {
			struct fpureg val;
			p = restore_fpvalue(p, &val);
			if (memcmp(&val, &test_regs.fpuregs[mode], sizeof(struct fpureg)) && !ignore_errors) {
				if (dooutput) {
					sprintf(outbp, "FP%d: expected %04x-%08x%08x but got %04x-%08x%08x\n", mode,
						val.exp, val.m[0], val.m[1],
						test_regs.fpuregs[mode].exp, test_regs.fpuregs[mode].m[0], test_regs.fpuregs[mode].m[1]);
					outbp += strlen(outbp);
				}
				errflag |= 1 << 1;
			}
			regs_fpuchanged[mode] = 0;
			xmemcpy(&last_registers.fpuregs[mode], &val, sizeof(struct fpureg));
		} else if (mode == CT_SR) {
			uae_u32 val = last_registers.sr;
			int size;
			// High 16 bit: ignore mask, low 16 bit: SR/CCR
			p = restore_value(p, &val, &size);
			test_ccrignoremask = ~(val >> 16);
			if ((val & (sr_undefined_mask & test_ccrignoremask)) != (test_regs.sr & (sr_undefined_mask & test_ccrignoremask)) && !ignore_errors && !ignore_sr) {
				if (dooutput) {
					sprintf(outbp, "SR: expected %04x -> %04x but got %04x", test_sr & 0xffff, val & 0xffff, test_regs.sr & 0xffff);
					outbp += strlen(outbp);
					if (test_ccrignoremask != 0xffff) {
						sprintf(outbp, " (%04x)", test_ccrignoremask);
						outbp += strlen(outbp);
					}
					*outbp++ = '\n';
					errflag |= 1 << 2;
					errflag |= 1 << 7;
				}
				// SR check
				uae_u16 mask = test_ccrignoremask & 0xff00;
				if ((val & (sr_undefined_mask & mask)) == (test_regs.sr & (sr_undefined_mask & mask))) {
					errflag &= ~(1 << 2);
				}
				// CCR check
				mask = test_ccrignoremask & 0x00ff;
				if (skipccrchange || ((val & (sr_undefined_mask & mask)) == (test_regs.sr & (sr_undefined_mask & mask)))) {
					errflag &= ~(1 << 7);
				}
			}
			sr_changed = 0;
			ccr_changed = 0;
			last_registers.sr = val;
			if (!(test_regs.expsr & 0x2000)) {
				sprintf(outbp, "SR S-bit is not set at start of exception handler!\n");
				outbp += strlen(outbp);
				errflag |= 1 << 16;
			}
			if ((test_regs.expsr & 0xff) != (test_regs.sr & 0xff)) {
				sprintf(outbp, "Exception stacked CCR != CCR at start of exception handler!\n");
				outbp += strlen(outbp);
				errflag |= 1 << 16;
			}

		} else if (mode == CT_PC) {
			uae_u32 val = last_registers.pc;
			p = restore_rel(p, &val, 0);
			pc_changed = 0;
			last_registers.pc = val;
		} else if (mode == CT_CYCLES) {
			uae_u32 val = last_registers.cycles;
			int size;
			p = restore_value(p, &val, &size);
			last_registers.cycles = val;
			gotcycles = 1;
		} else if (mode == CT_FPCR) {
			uae_u32 val = last_registers.fpcr;
			int size;
			p = restore_value(p, &val, &size);
			if (val != test_regs.fpcr && !ignore_errors) {
				if (dooutput) {
					sprintf(outbp, "FPCR: expected %08x -> %08x but got %08x\n", test_fpcr, val, test_regs.fpcr);
					outbp += strlen(outbp);
				}
				errflag |= 1 << 3;
			}
			fpcr_changed = 0;
			last_registers.fpcr = val;
		} else if (mode == CT_FPSR) {
			uae_u32 val = last_registers.fpsr;
			int size;
			p = restore_value(p, &val, &size);
			if (val != test_regs.fpsr && !ignore_errors) {
				if (dooutput) {
					sprintf(outbp, "FPSR: expected %08x -> %08x but got %08x\n", test_fpsr, val, test_regs.fpsr);
					outbp += strlen(outbp);
				}
				errflag |= 1 << 4;
			}
			fpsr_changed = 0;
			last_registers.fpsr = val;
		} else if (mode == CT_FPIAR) {
			uae_u32 val = last_registers.fpiar;
			p = restore_rel(p, &val, 0);
			if (val != test_regs.fpiar && !ignore_errors) {
				if (dooutput) {
					sprintf(outbp, "FPIAR: expected %08x but got %08x\n", val, test_regs.fpiar);
					outbp += strlen(outbp);
				}
				errflag |= 1 << 5;
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
				if (mval != val && !ignore_errors && !skipmemwrite) {
					if (dooutput) {
						sprintf(outbp, "Memory byte write: address %08x, expected %02x but got %02x\n", (uae_u32)addr, val, mval);
						outbp += strlen(outbp);
					}
					errflag |= 1 << 6;
				}
				addr[0] = oldval;
				break;
				case 1:
				mval = (addr[0] << 8) | (addr[1]);
				if (mval != val && !ignore_errors && !skipmemwrite) {
					if (dooutput) {
						sprintf(outbp, "Memory word write: address %08x, expected %04x but got %04x\n", (uae_u32)addr, val, mval);
						outbp += strlen(outbp);
					}
					errflag |= 1 << 6;
				}
				addr[0] = oldval >> 8;
				addr[1] = oldval;
				break;
				case 2:
				mval = gl(addr);
				if (mval != val && !ignore_errors && !skipmemwrite) {
					if (dooutput) {
						sprintf(outbp, "Memory long write: address %08x, expected %08x but got %08x\n", (uae_u32)addr, val, mval);
						outbp += strlen(outbp);
					}
					errflag |= 1 << 6;
				}
				pl(addr, oldval);
				break;
			}
		} else {
			end_test();
			printf("Unknown test data %02x mode %d\n", v, mode);
			exit(0);
		}
	}
	if (!ignore_errors) {
		if (!skipregchange) {
			for (int i = 0; i < 16; i++) {
				if (regs_changed[i]) {
					if (dooutput) {
						sprintf(outbp, "%c%d: modified %08x -> %08x but expected no modifications\n", i < 8 ? 'D' : 'A', i & 7, last_registers.regs[i], test_regs.regs[i]);
						outbp += strlen(outbp);
					}
					errflag |= 1 << 0;
				}
			}
		}
		if (!ignore_sr) {
			if (sr_changed) {
				if (dooutput) {
					sprintf(outbp, "SR: modified %04x -> %04x but expected no modifications\n", last_registers.sr & 0xffff, test_regs.sr & 0xffff);
					outbp += strlen(outbp);
				}
				errflag |= 1 << 2;
			} else if (ccr_changed && !skipccrchange) {
				if (dooutput) {
					sprintf(outbp, "SR: modified %04x -> %04x but expected no modifications\n", last_registers.sr & 0xffff, test_regs.sr & 0xffff);
					outbp += strlen(outbp);
				}
				errflag |= 1 << 2;
			}
		}
		for (int i = 0; i < 8; i++) {
			if (regs_fpuchanged[i]) {
				if (dooutput) {
					sprintf(outbp, "FP%d: modified %04x-%08x%08x -> %04x-%08x%08x but expected no modifications\n", i,
						last_registers.fpuregs[i].exp, last_registers.fpuregs[i].m[0], last_registers.fpuregs[i].m[1],
						test_regs.fpuregs[i].exp, test_regs.fpuregs[i].m[0], test_regs.fpuregs[i].m[1]);
					outbp += strlen(outbp);
				}
				errflag |= 1 << 1;
			}
		}
		if (fpsr_changed) {
			if (dooutput) {
				sprintf(outbp, "FPSR: modified %08x -> %08x but expected no modifications\n", last_registers.fpsr, test_regs.fpsr);
				outbp += strlen(outbp);
			}
			errflag |= 1 << 3;
		}
		if (fpcr_changed) {
			if (dooutput) {
				sprintf(outbp, "FPCR: modified %08x -> %08x but expected no modifications\n", last_registers.fpcr, test_regs.fpcr);
				outbp += strlen(outbp);
			}
			errflag |= 1 << 4;
		}
		if (fpiar_changed) {
			if (dooutput) {
				sprintf(outbp, "FPIAR: modified %08x -> %08x but expected no modifications\n", last_registers.fpiar, test_regs.fpiar);
				outbp += strlen(outbp);
			}
			errflag |= 1 << 5;
		}
		if (cycles && cpu_lvl <= 1) {
			if (!gotcycles && errflag) {
				if (dooutput) {
					sprintf(outbp, "No Cycle count data available.\n");
					outbp += strlen(outbp);
				}
			} else {
				if (!check_cycles(exc, extratrace)) {
					errflag |= 1 << 8;
				}
			}
		}
		errflag_orig |= errflag;
	}

	// if excskipccr + bus, address, divide by zero or chk + only CCR mismatch detected: clear error
	if (excskipccr && errflag == (1 << 7) && (exceptionnum == 2 || exceptionnum == 3 || exceptionnum == 5 || exceptionnum == 6)) {
		errflag = 0;
	}

	if (errflag && dooutput) {
		outbp = outbuffer;
		addinfo();
		strcpy(outbp, outbuffer2);
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
				sprintf(outbp, "OK: exception %d ", exc);
				outbp += strlen(outbp);
				if (exception_stored) {
					hexdump(last_exception, exception_stored);
				} else {
					*outbp++ = '\n';
				}
			}
			if ((exc == 3 || exc == 2) && cpu_lvl == 0) {
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
		errors++;
	}
	if (!errflag && errflag_orig && dooutput) {
		outbp = outbp_old;
		*outbp = 0;
		infoadded = 0;
	}
	return p;
}

static void store_addr(uae_u32 s, uae_u8 *d)
{
	if (s == 0xffffffff)
		return;
	for (int i = 0; i < SIZE_STORED_ADDRESS; i++) {
		uae_u32 ss = s + (i - SIZE_STORED_ADDRESS_OFFSET);
		if (is_valid_test_addr_read(ss)) {
			*d++ = *((uae_u8 *)ss);
		} else {
			*d++ = 0;
		}
	}
}

#ifdef AMIGA
static const int interrupt_levels[] =
{
	0, 1, 1, 1, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, -1
};

static void set_interrupt(void)
{
	if (interrupt_count < 15) {
		volatile uae_u16 *intena = (uae_u16*)0xdff09a;
		volatile uae_u16 *intreq = (uae_u16*)0xdff09c;
		uae_u16 mask = 1 << interrupt_count;
		*intena = mask | 0x8000 | 0x4000;
		*intreq = mask | 0x8000;
	}
	interrupt_count++;
	interrupt_count &= 15;
}

static void clear_interrupt(void)
{
	volatile uae_u16 *intena = (uae_u16*)0xdff09a;
	volatile uae_u16 *intreq = (uae_u16*)0xdff09c;
	*intena = 0x7fff;
	*intreq = 0x7fff;
}
#endif

static void process_test(uae_u8 *p)
{
	outbp = outbuffer2;
	outbp[0] = 0;
	infoadded = 0;
	errors = 0;

	memset(&regs, 0, sizeof(struct registers));
	regs.sr = interrupt_mask << 8;
	regs.srcaddr = 0xffffffff;
	regs.dstaddr = 0xffffffff;
	regs.branchtarget = 0xffffffff;

	endpc = opcode_memory_addr;
	startpc = opcode_memory_addr;
	start_test();

	test_ccrignoremask = 0xffff;

#ifdef AMIGA
	interrupt_count = 0;
	clear_interrupt();
#endif
	ahcnt = 0;

	for (;;) {

#ifndef M68K
		outbp = outbuffer2;
#endif

		regs.endpc = endpc;
		regs.pc = startpc;

		for (;;) {
			uae_u8 v = *p;
			if (v == CT_END_INIT || v == CT_END_FINISH)
				break;
			p = restore_data(p);
		}
		if (*p == CT_END_FINISH)
			break;
		p++;

		int stackcopysize = 0;
		for (int i = 0; i < 32; i += 2) {
			if (!is_valid_test_addr_readwrite(regs.regs[15] + i))
				break;
			stackcopysize += 2;
		}

		store_addr(regs.srcaddr, srcaddr);
		store_addr(regs.dstaddr, dstaddr);
		store_addr(regs.branchtarget, branchtarget);
		startpc = regs.pc;
		endpc = regs.endpc;
		uae_u8 *opcode_memory_end = (uae_u8*)endpc;

		xmemcpy(&last_registers, &regs, sizeof(struct registers));

		int fpumode = fpu_model && (opcode_memory[0] & 0xf0) == 0xf0;

		uae_u32 originalopcodeend = 0x4afc4e71;
		uae_u32 opcodeend = originalopcodeend;
		int extraccr = 0;
		int validendsize = 0;
		if (is_valid_test_addr_readwrite((uae_u32)opcode_memory_end + 2)) {
			validendsize = 2;
		} else if (is_valid_test_addr_readwrite((uae_u32)opcode_memory_end)) {
			validendsize = 1;
		}

		uae_u32 last_pc = startpc;
		uae_u32 last_fpiar = startpc;
		int old_super = -1;

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

			int maxccr = *p++;
			for (int ccr = 0;  ccr < maxccr; ccr++) {

				opcodeend = (opcodeend >> 16) | (opcodeend << 16);
				if (validendsize == 2) {
					pl(opcode_memory_end, opcodeend);
				} else if (validendsize == 1) {
					pw(opcode_memory_end, opcodeend >> 16);
				}

				if (regs.branchtarget != 0xffffffff && !(regs.branchtarget & 1)) {
					if (regs.branchtarget_mode == 1) {
						uae_u32 bv = gl((uae_u8*)regs.branchtarget);
						bv = (bv >> 16) | (bv << 16);
						pl((uae_u8*)regs.branchtarget, bv);
					} else if (regs.branchtarget_mode == 2) {
						uae_u16 bv = gw((uae_u8 *)regs.branchtarget);
						if (bv == 0x4e71)
							bv = 0x4afc;
						else
							bv = 0x4e71;
						pw((uae_u8 *)regs.branchtarget, bv);
					}
				}

				regs.ssp = super_stack_memory - 0x80;
				regs.msp = super_stack_memory;
				regs.pc = startpc;
				regs.fpiar = startpc;
				regs.cyclest = 0xffffffff;

#ifdef M68K
				if (stackcopysize > 0)
					xmemcpy((void*)regs.ssp, (void*)regs.regs[15], stackcopysize);
#endif
				xmemcpy(&test_regs, &regs, sizeof(struct registers));

				if (maxccr >= 32) {
					test_regs.sr = ccr;
				} else {
					test_regs.sr = (ccr ? 31 : 0);
				}
				test_regs.sr |= sr_mask | (interrupt_mask << 8);
				test_regs.expsr = test_regs.sr | 0x2000;
				test_sr = test_regs.sr;
				if (fpumode) {
					if (maxccr >= 32) {
						test_regs.fpsr = (ccr & 15) << 24;
						test_regs.fpcr = (ccr >> 4) << 4;
					} else {
						test_regs.fpsr = (ccr ? 15 : 0) << 24;
						test_regs.fpcr = (ccr >> 1) << 4;
					}
					test_fpsr = test_regs.fpsr;
					test_fpcr = test_regs.fpcr;
				}
				int super = (test_regs.sr & 0x2000) != 0;

				if (super != old_super) {
					stackaddr_ptr = super ? regs.ssp : regs.regs[15];
					store_addr(stackaddr_ptr, stackaddr);
					old_super = super;
				}

				if (exitcnt >= 0) {
					exitcnt--;
					if (exitcnt < 0) {
						addinfo();
						strcat(outbp, "Registers before:\n");
						outbp += strlen(outbp);
						out_regs(&regs, 1);
						end_test();
						printf(outbuffer);
						printf("\nExit count expired\n");
						exit(0);
					}
				}

				if ((*p) == CT_END_SKIP) {

					p++;

				} else {

					short ignore_errors = 0;
					short ignore_sr = 0;

					if ((ccr_mask & ccr) || (ccr == 0)) {

						reset_error_vectors();

#if 0
						volatile int *tn = (volatile int*)0x100;
						*tn = testcnt;
#endif

#ifdef AMIGA
						if (interrupttest) {
							set_interrupt();
						}
#endif

						if (cpu_lvl == 1) {
							execute_test010(&test_regs);
						} else if (cpu_lvl >= 2) {
							flushcache(cpu_lvl);
							if (fpu_model)
								execute_testfpu(&test_regs);
							else
								execute_test020(&test_regs);
						} else {
							execute_test000(&test_regs);
						}

#ifdef AMIGA
						if (interrupttest) {
							clear_interrupt();
						}
#endif

						if (ccr_mask == 0 && ccr == 0)
							ignore_sr = 1;

						set_error_vectors();

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

					testcnt++;
					if (super)
						supercnt++;

					last_pc = last_registers.pc;
					last_fpiar = last_registers.fpiar;
				}

				if (testexit()) {
					end_test();
					printf("\nAborted (%d)\n", testcnt);
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

		if (validendsize == 2) {
			pl(opcode_memory_end, originalopcodeend);
		} else if (validendsize == 1) {
			pw(opcode_memory_end, originalopcodeend >> 16);
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
#ifdef WAITEXIT
	getchar();
#endif
}

static uae_u32 read_u32(uae_u8 *headerfile, int *poffset)
{
	uae_u8 data[4] = { 0 };
	memcpy(data, headerfile + (*poffset), 4);
	(*poffset) += 4;
	return gl(data);
}

static int test_mnemo(const char *opcode)
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


	sprintf(tfname, "%s/0000.dat", opcode);
	size = -1;
	uae_u8 *headerfile = load_file(path, tfname, NULL, &size, 1, 1);
	if (!headerfile) {
		exit(0);
	}
	int headoffset = 0;
	v = read_u32(headerfile, &headoffset);
	if (v != DATA_VERSION) {
		printf("Invalid test data file (header)\n");
		exit(0);
	}

	starttimeid = read_u32(headerfile, &headoffset);
	uae_u32 hmem_lmem = read_u32(headerfile, &headoffset);
	hmem_rom = (uae_s16)(hmem_lmem >> 16);
	lmem_rom = (uae_s16)(hmem_lmem & 65535);
	test_memory_addr = read_u32(headerfile, &headoffset);
	test_memory_size = read_u32(headerfile, &headoffset);
	test_memory_end = test_memory_addr + test_memory_size;
	opcode_memory_addr = read_u32(headerfile, &headoffset);
	opcode_memory = (uae_u8*)opcode_memory_addr;
	uae_u32 lvl_mask = read_u32(headerfile, &headoffset);
	lvl = (lvl_mask >> 16) & 15;
	interrupt_mask = (lvl_mask >> 20) & 7;
	addressing_mask = (lvl_mask & 0x80000000) ? 0xffffffff : 0x00ffffff;
	interrupttest = (lvl_mask >> 26) & 1;
	sr_undefined_mask = lvl_mask & 0xffff;
	safe_memory_mode = (lvl_mask >> 23) & 3;
	fpu_model = read_u32(headerfile, &headoffset);
	test_low_memory_start = read_u32(headerfile, &headoffset);
	test_low_memory_end = read_u32(headerfile, &headoffset);
	test_high_memory_start = read_u32(headerfile, &headoffset);
	test_high_memory_end = read_u32(headerfile, &headoffset);
	safe_memory_start = (uae_u8*)read_u32(headerfile, &headoffset);
	safe_memory_end = (uae_u8*)read_u32(headerfile, &headoffset);
	user_stack_memory = read_u32(headerfile, &headoffset);
	super_stack_memory = read_u32(headerfile, &headoffset);
	exception_vectors = read_u32(headerfile, &headoffset);
	read_u32(headerfile, &headoffset);
	read_u32(headerfile, &headoffset);
	read_u32(headerfile, &headoffset);
	memcpy(inst_name, headerfile + headoffset, sizeof(inst_name) - 1);
	inst_name[sizeof(inst_name) - 1] = 0;
	free(headerfile);
	headerfile = NULL;

	int lvl2 = cpu_lvl;
	if (lvl2 == 5 && lvl2 != lvl)
		lvl2 = 4;

	if (lvl != lvl2) {
		printf("Mismatched CPU model: %u <> %u\n",
			68000 + 10 * (cpu_lvl < 5 ? cpu_lvl : 6), 68000 + (lvl < 5 ? lvl : 6) * 10);
		return 0;
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

	low_memory_offset = 0;
	if (test_low_memory_start != 0xffffffff) {
		low_memory_offset = test_low_memory_start;
	} else {
		low_memory_offset = 0x100;
		test_low_memory_end = 0xffffffff;
	}

	high_memory_offset = 0;
	if (test_high_memory_start != 0xffffffff) {
		high_memory_offset = test_high_memory_start & 0x7fff;
	} else {
		test_high_memory_end = 0xffffffff;
	}

	if (!absallocated) {
		test_memory = allocate_absolute(test_memory_addr, test_memory_size);
		if (!test_memory) {
			printf("Couldn't allocate tmem area %08x-%08x\n", (uae_u32)test_memory_addr, test_memory_size);
			exit(0);
		}
		absallocated = test_memory;
	}
	if (absallocated != test_memory) {
		printf("tmem area changed!?\n");
		exit(0);
	}

	size = test_memory_size;
	load_file(path, "tmem.dat", test_memory, &size, 1, 0);
	if (size != test_memory_size) {
		printf("tmem.dat size mismatch\n");
		exit(0);
	}

	printf("CPUlvl=%d, Mask=%08x Code=%08x SP=%08x ISP=%08x\n",
		cpu_lvl, addressing_mask, (uae_u32)opcode_memory,
		user_stack_memory, super_stack_memory);
	printf(" Low: %08x-%08x High: %08x-%08x\n",
		test_low_memory_start, test_low_memory_end,
		test_high_memory_start, test_high_memory_end);
	printf("Test: %08x-%08x Safe: %08x-%08x\n",
		test_memory_addr, test_memory_end,
		(uae_u32)safe_memory_start, (uae_u32)safe_memory_end);
	printf("%s (%s):\n", inst_name, group);

	testcnt = 0;
	memset(exceptioncount, 0, sizeof(exceptioncount));
	supercnt = 0;

	for (;;) {
		printf("%s (%s). %u...\n", tfname, group, testcnt);

		sprintf(tfname, "%s/%04d.dat", opcode, filecnt);

		test_data_size = -1;
		test_data = load_file(path, tfname, NULL, &test_data_size, 0, 1);
		if (!test_data) {
			if (askifmissing) {
				printf("Couldn't open '%s%s'. Type new path and press enter.\n", path, tfname);
				path[0] = 0;
				fgets(path, sizeof(path), stdin);
				if (path[0]) {
					path[strlen(path) - 1] = 0;
					if (path[0]) {
						continue;
					}
				}
			}
			quit = 1;
			break;
		}
		if (gl(test_data) != DATA_VERSION) {
			printf("Invalid test data file (header)\n");
			exit(0);
		}
		if (gl(test_data + 4) != starttimeid) {
			printf("Test data file header mismatch (old test data file?)\n");
			break;
		}
		if (test_data[test_data_size - 2] != CT_END_FINISH) {
			printf("Invalid test data file (footer)\n");
			free(test_data);
			exit(0);
		}

		// last file?
		int last = test_data[test_data_size - 1] == CT_END_FINISH;

		test_data_size -= 16;
		if (test_data_size <= 0)
			break;

		test_data += 16;
		process_test(test_data);
		test_data -= 16;

		free(test_data);

		if (errors || quit || last) {
			break;
		}

		filecnt++;
	}

	printf("%u ", testcnt);
	printf("S=%d", supercnt);
	for (int i = 0; i < 128; i++) {
		if (exceptioncount[0][i] || exceptioncount[1][i] || exceptioncount[2][i]) {
			if (i == 2 || i == 3) {
				printf(" E%02d=%d/%d/%d", i, exceptioncount[0][i], exceptioncount[1][i], exceptioncount[2][i]);
			} else {
				printf(" E%02d=%d", i, exceptioncount[0][i]);
			}
		}
	}
	printf("\n");

	if (!errors && !quit) {
		printf("All tests complete (total %u).\n", testcnt);
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

static int isdir(const char *dirpath, const char *name)
{
	struct stat buf;

	snprintf(tmpbuffer, sizeof(tmpbuffer), "%s%s", dirpath, name);
	return stat(tmpbuffer, &buf) == 0 && S_ISDIR(buf.st_mode);
}

int main(int argc, char *argv[])
{
	int stop_on_error = 1;

	atexit(freestuff);

#ifndef M68K

	char *params[] = { "", "lslw.w", "", NULL };
	argv = params;
	argc = 3;

	strcpy(path, "C:\\projects\\winuae\\src\\cputest\\data\\");

	vbr_zero = calloc(1, 1024);

	cpu_lvl = 0;

#else

#define _stricmp strcasecmp

	strcpy(path, "data/");

	low_memory = (uae_u8 *)0;
	high_memory = (uae_u8 *)0xffff8000;

	cpu_lvl = get_cpu_model();

	if (cpu_lvl == 5) {
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
	}

#endif

	if (argc < 2) {
		printf("cputest (<group>)/<all/mnemonic> (<start mnemonic>) (other params)\n");
		printf("mnemonic = test single mnemonic\n");
		printf("all = test all\n");
		printf("all <mnemonic> = test all, starting from <mnemonic>\n");
		printf("all <mnemonic> -next = test all, starting after <mnemonic>\n");
		printf("-continue = don't stop on error (all mode only)\n");
		printf("-ccrmask = ignore CCR bits that are not set.\n");
		printf("-nodisasm = do not disassemble failed test.\n");
		printf("-basicexc = do only basic checks when exception is 2 or 3.\n");
		printf("-skipexcccr = ignore CCR if DivByZero, CHK, Address/Bus error exception.\n");
		printf("-skipmem = do not validate memory writes.\n");
		printf("-skipreg = do not validate registers.\n");
		printf("-askifmissing = ask for new path if dat file is missing.\n");
		printf("-exit n = exit after n tests.\n");
		printf("-cycles [range adjust] = check cycle counts.\n");
		printf("-cyclecnt <address>. Use custom hardware cycle counter.\n");
		return 0;
	}

	opcode[0] = 0;
	strcpy(group, "default");

	check_undefined_sr = 1;
	ccr_mask = 0xff;
	disasm = 1;
	exitcnt = -1;
	cyclecounter_addr = 0xffffffff;
	cycles_range = 2;

	for (int i = 1; i < argc; i++) {
		char *s = argv[i];
		char *next = i + 1 < argc && argv[i + 1][0] != '-' ? argv[i + 1] : NULL;
		if (s[0] != '-' && opcode[0] == 0 && strlen(s) < sizeof(opcode) - 1) {
			strcpy(opcode, s);
			continue;
		}
		if (!_stricmp(s, "-continue")) {
			stop_on_error = 0;
		} else if (!_stricmp(s, "-noundefined")) {
			check_undefined_sr = 0;
		} else if (!_stricmp(s, "-ccrmask")) {
			ccr_mask = 0;
			if (next) {
				ccr_mask = ~getparamval(next);
				i++;
			}
		} else if (!_stricmp(s, "-silent")) {
			dooutput = 0;
		} else if (!_stricmp(s, "-68000")) {
			cpu_lvl = 0;
		} else if (!_stricmp(s, "-68010")) {
			cpu_lvl = 1;
		} else if (!_stricmp(s, "-68020")) {
			cpu_lvl = 2;
		} else if (!_stricmp(s, "-68030")) {
			cpu_lvl = 3;
		} else if (!_stricmp(s, "-68040")) {
			cpu_lvl = 4;
		} else if (!_stricmp(s, "-68060")) {
			cpu_lvl = 5;
		} else if (!_stricmp(s, "-nodisasm")) {
			disasm = 0;
		} else if (!_stricmp(s, "-basicexc")) {
			basicexcept = 1;
		} else if (!_stricmp(s, "-skipexcccr")) {
			excskipccr = 1;
		} else if (!_stricmp(s, "-skipmem")) {
			skipmemwrite = 1;
		} else if (!_stricmp(s, "-skipreg")) {
			skipregchange = 1;
		} else if (!_stricmp(s, "-skipccr")) {
			skipccrchange = 1;
		} else if (!_stricmp(s, "-askifmissing")) {
			askifmissing = 1;
		} else if (!_stricmp(s, "-next")) {
			nextall = 1;
		} else if (!_stricmp(s, "-exit")) {
			if (next) {
				exitcnt = atoi(next);
				i++;
			}
		} else if (!_stricmp(s, "-cycles")) {
			cycles = 1;
			if (i + 1 < argc && argv[i][0] != '-') {
				i++;
				cycles_range = atoi(argv[i]);
				if (i + 1 < argc && argv[i][0] != '-') {
					i++;
					cycles_adjust = atoi(argv[i]);
				}
			}
		} else if (!_stricmp(s, "-cyclecnt")) {
			if (next) {
				cyclecounter_addr = getparamval(next);
				cycles = 1;
			}
		}
	}

#ifdef M68K
	if(cyclecounter_addr != 0xffffffff) {
		// yes, this is far too ugly
		extern uae_u32 cyclereg_address1;
		extern uae_u32 cyclereg_address2;
		extern uae_u32 cyclereg_address3;
		extern uae_u32 cyclereg_address4;
		extern uae_u32 cyclereg_address5;
		extern uae_u32 cyclereg_address6;
		*((uae_u32*)((uae_u32)(&cyclereg_address1) + 2)) = cyclecounter_addr;
		*((uae_u32*)((uae_u32)(&cyclereg_address2) + 2)) = cyclecounter_addr;
		*((uae_u32*)((uae_u32)(&cyclereg_address3) + 2)) = cyclecounter_addr;
		*((uae_u32*)((uae_u32)(&cyclereg_address4) + 2)) = cyclecounter_addr;
		*((uae_u32*)((uae_u32)(&cyclereg_address5) + 2)) = cyclecounter_addr;
		*((uae_u32*)((uae_u32)(&cyclereg_address6) + 2)) = cyclecounter_addr;
	}
#endif

	DIR *groupd = NULL;
	
	char *p = strchr(opcode, '/');
	if (p) {
		strcpy(tmpbuffer, opcode);
		strcpy(group, opcode);
		group[p - opcode] = 0;
		strcpy(opcode, tmpbuffer + (p - opcode) + 1);
	}
	
	if (!strcmp(group, "all")) {
		groupd = opendir(path);
	}

	int cpumodel = 68000 + (cpu_lvl == 5 ? 6 : cpu_lvl) * 10;
	sprintf(cpustr, "%u_", cpumodel);
	char *pathptr = path + strlen(path);

	for (;;) {

		if (groupd) {
			pathptr[0] = 0;
			struct dirent *groupdr = readdir(groupd);
			if (!groupdr)
				break;
			if (!isdir(path, groupdr->d_name))
				continue;
			if (groupdr->d_name[0] == '.')
				continue;
			if (strnicmp(cpustr, groupdr->d_name, strlen(cpustr)))
				continue;
			sprintf(pathptr, "%s/", groupdr->d_name);		
			strcpy(group, groupdr->d_name + strlen(cpustr));
		} else {
			sprintf(pathptr, "%u_%s/",cpumodel, group);
		}

		low_memory_size = -1;
		low_memory_temp = load_file(path, "lmem.dat", NULL, &low_memory_size, 0, 1);
		high_memory_size = -1;
		high_memory_temp = load_file(path, "hmem.dat", NULL, &high_memory_size, 0, 1);

#ifndef M68K
		low_memory = calloc(1, 32768);
		if (high_memory_size > 0)
			high_memory = calloc(1, high_memory_size);
#endif

		low_memory_back = calloc(1, 32768);
		if (!low_memory_temp) {
			low_memory_temp = calloc(1, 32768);
		}

		if (high_memory_size > 0) {
			high_memory_back = calloc(1, high_memory_size);
		}

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
				int d = isdir(path, dr->d_name);
				if (d && dr->d_name[0] != '.') {
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
			if (argc >= 3 && argv[2][0] != '-') {
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
			if (nextall) {
				first += MAX_FILE_LEN;
			}
			int err = 0;
			for (int i = first; i < diroff; i += MAX_FILE_LEN) {
				if (test_mnemo(dirs + i)) {
					err = 1;
					if (stop_on_error)
						break;
				}
			}

			free(dirs);

			if (err && stop_on_error)
				break;

		} else {
			if (test_mnemo(opcode)) {
				if (stop_on_error)
					break;
			}
		}

		free(low_memory_temp);
		free(high_memory_temp);
		free(low_memory_back);
		free(high_memory_back);

		if (!groupd)
			break;
	}

	if (groupd)
		closedir(groupd);

	return 0;
}
