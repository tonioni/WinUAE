 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Generator for planar to chunky conversions
  *
  * Copyright 1997 Bernd Schmidt
  */


#include <stdio.h>

#include "sysconfig.h"
#include "sysdeps.h"

/* We can't include custom.h here.  */
#define MAX_WORDS_PER_LINE 50


static char *gen_ind (char *a, int b)
{
    char buf[200];
    sprintf (buf, "%d(%s)", b, a);
    return strdup (buf);
}

static char *gen_indx (char *a, int b, char *c, int d)
{
    char buf[200];
    sprintf (buf, "%d(%s,%s,%d)", b, a, c, d);
    return strdup (buf);
}

static char *gen_indsx (char *a, char *sym, int b, char *c, int d)
{
    char buf[200];
    sprintf (buf, "%s+%d(%s,%s,%d)", sym, b, a, c, d);
    return strdup (buf);
}

#define reg(a) "%" a
#define ind(a,b) #b"("a")"
#define imm(a) "$"#a
#ifdef USE_UNDERSCORE
#define sym(a) "_"#a
#else
#define sym(a) #a
#endif
#define indx(a,b,c,d) #b"("a","c","#d")"
#define indsx(a,s,b,c,d) s"+"#b"("a","c","#d")"

static int labelno = 0;

static int get_label (void)
{
    return labelno++;
}
static void declare_label (int nr)
{
    printf (".L%d:\n", nr);
}
static int new_label (void)
{
    int nr = get_label ();
    declare_label (nr);
    return nr;
}
static void gen_label (int nr) { printf (".L%d", nr); }
static void jnz (int nr) { printf ("\tjnz "); gen_label (nr); printf ("\n"); }
static void jnc (int nr) { printf ("\tjnc "); gen_label (nr); printf ("\n"); }
static void jc (int nr) { printf ("\tjc "); gen_label (nr); printf ("\n"); }
static void jmp (int nr) { printf ("\tjmp "); gen_label (nr); printf ("\n"); }
static void movl (char *src, char *dst) { printf ("\tmovl %s,%s\n", src, dst); }
static void movw (char *src, char *dst) { printf ("\tmovl %s,%s\n", src, dst); }
static void movb (char *src, char *dst) { printf ("\tmovl %s,%s\n", src, dst); }
static void movzbl (char *src, char *dst) { printf ("\tmovzbl %s,%s\n", src, dst); }
static void leal (char *src, char *dst) { printf ("\tleal %s,%s\n", src, dst); }
static void addl (char *src, char *dst) { printf ("\taddl %s,%s\n", src, dst); }
static void subl (char *src, char *dst) { printf ("\tsubl %s,%s\n", src, dst); }
static void cmpl (char *src, char *dst) { printf ("\tcmpl %s,%s\n", src, dst); }
static void andl (unsigned long mask, char *dst) { printf ("\tandl $0x%0lx,%s\n", mask, dst); }
static void orl (char *src, char *dst) { printf ("\torl %s,%s\n", src, dst); }
static void imull (unsigned long val, char *dst) { printf ("\timull $0x%08lx,%s\n", val, dst); }
static void decl (char *dst) { printf ("\tdecl %s\n", dst); }
static void incl (char *dst) { printf ("\tincl %s\n", dst); }
static void bswapl (char *dst) { printf ("\tbswap %s\n", dst); }
static void shrl (int count, char *dst) { printf ("\tshrl $%d,%s\n", count, dst); }
static void shll (int count, char *dst) { printf ("\tshll $%d,%s\n", count, dst); }
static void pushl (char *src) { printf ("\tpushl %s\n", src); }
static void popl (char *dst) { printf ("\tpopl %s\n", dst); }
static void ret (void) { printf ("\tret\n"); }
static void align (int a) { printf ("\t.p2align %d,0x90\n", a); }

static void shiftleftl (int count, char *dst)
{
    if (count == 0)
	return;
    if (count < 0)
	shrl (-count, dst);
    else {
	char *indb0;
	switch (count) {
	 case 1:
	    addl (dst, dst);
	    break;
	 case 2: case 3:
	    indb0 = gen_indx ("", 0, dst, 1 << count);
	    leal (indb0, dst);
	    free (indb0);
	    break;
	 default:
	    shll (count, dst);
	}
    }
}

static void declare_fn (char *name)
{
    printf ("\t.globl %s\n", name);
/*    printf ("\t.type %s,@function\n", name); */
    align (5);
    printf ("%s:\n", name);
}

#define esi reg("esi")
#define edi reg("edi")
#define ebp reg("ebp")
#define esp reg("esp")
#define eax reg("eax")
#define ebx reg("ebx")
#define ecx reg("ecx")
#define edx reg("edx")

/* Modes:
 * 0: normal
 * 1: only generate every second plane, set memory
 * 2: only generate every second plane, starting at second plane, or to memory
 */

/* Normal code: one pixel per bit */
static void gen_x86_set_hires_h_toobad_k6_too_slow_someone_try_this_with_a_ppro (int pl, int mode)
{
    int plmul = mode == 0 ? 1 : 2;
    int ploff = mode == 2 ? 1 : 0;
    int i;
    int loop;
    char buf[40];
    char *indb0;

    sprintf (buf, sym (set_hires_h_%d_%d), pl, mode);
    declare_fn (buf);

    pushl (ebp);
    pushl (esi);
    pushl (edi);
    pushl (ebx);

    if (pl == 0) {
	movl (ind (esp, 20), ebp);
	movl (ind (esp, 24), esi);
    }
    movl (imm (0), edi);

    loop = get_label ();
    jmp (loop);
    align (5);
    declare_label (loop);

    if (pl > 0)
      movl (ind (esp, 24), esi);
    if (mode == 2) {
	if (pl > 0)
	  movl (ind (esp, 20), ebp);
	movl (indx (ebp, 0, edi, 8), ecx);
	movl (indx (ebp, 4, edi, 8), ebx);
    }
    for (i = 0; i <= pl; i+=2) {
	int realpl = i * plmul + ploff;
	char *data1 = (i == 0 && mode != 2 ? ecx : edx);
	char *data2 = (i == 0 && mode != 2 ? ebx : eax);

	if (i < pl) {
	    indb0 = gen_indx (esi, (realpl + plmul)*MAX_WORDS_PER_LINE*2, edi, 1);
	    movzbl (indb0, ebp);
	    free (indb0);
	    imull (0x08040201, ebp);
	}

	indb0 = gen_indx (esi, realpl*MAX_WORDS_PER_LINE*2, edi, 1);
	movzbl (indb0, data2);
	free (indb0);

	if (i == pl || i == pl - 1)
	    incl (edi);
	imull (0x08040201, data2);
	if (i < pl) {
	    movl (ebp, esi);
	    andl (0x08080808, ebp);
	    shiftleftl (realpl + plmul - 7, esi);
	}
	movl (data2, data1);
	andl (0x08080808, data2);
	shiftleftl (realpl - 7, data1);
	if (i < pl) {
	    andl (0x01010101 << (realpl + plmul), esi);
	}
	andl (0x01010101 << realpl, data1);
	shiftleftl (realpl - 3, data2);
	if (i < pl) {
	    shiftleftl (realpl + plmul - 3, ebp);
	}
	if (i < pl) {
	    orl (esi, ecx);
	    movl (ind (esp, 24), esi);
	    orl (ebp, ebx);
	}
	if (i > 0 || mode == 2) {
	    orl (edx, ecx);
	    orl (eax, ebx);
	}
    }
    if (pl > 0)
      movl (ind (esp, 20), ebp);
    cmpl (ind (esp, 28), edi);
    movl (ecx, indx (ebp, -8, edi, 8));
    movl (ebx, indx (ebp, -4, edi, 8));
    jc (loop);

    popl (reg ("ebx"));
    popl (reg ("edi"));
    popl (reg ("esi"));
    popl (reg ("ebp"));
    ret ();
    printf ("\n\n");
}

static void gen_x86_set_hires_h (int pl, int mode)
{
    int plmul = mode == 0 ? 1 : 2;
    int ploff = mode == 2 ? 1 : 0;
    int i;
    int loop;
    char buf[40];
    char *indb0;

    sprintf (buf, sym (set_hires_h_%d_%d), pl, mode);
    declare_fn (buf);

    pushl (ebp);
    pushl (esi);
    pushl (edi);
    pushl (ebx);

    if (pl == 0) {
	movl (ind (esp, 20), ebp);
	movl (ind (esp, 24), esi);
    }
    movl (imm (0), edi);

    loop = get_label ();
    jmp (loop);
    align (5);
    declare_label (loop);

    if (pl > 0)
      movl (ind (esp, 24), esi);
    if (mode == 2) {
	if (pl > 0)
	  movl (ind (esp, 20), ebp);
	movl (indx (ebp, 0, edi, 8), ecx);
	movl (indx (ebp, 4, edi, 8), ebx);
    }
    for (i = 0; i <= pl; i+=2) {
	int realpl = i * plmul + ploff;
	char *data1 = (i == 0 && mode != 2 ? ecx : edx);
	char *data2 = (i == 0 && mode != 2 ? ebx : eax);

	if (i < pl) {
	    indb0 = gen_indx (esi, (realpl + plmul)*MAX_WORDS_PER_LINE*2, edi, 1);
	    movzbl (indb0, ebp);
	    free (indb0);
	}
	indb0 = gen_indx (esi, realpl*MAX_WORDS_PER_LINE*2, edi, 1);
	movzbl (indb0, data2);
	free (indb0);
	if (i < pl) {
	    indb0 = gen_indsx ("", sym (hirestab_h), 0, ebp, 8);
	    movl (indb0, esi);
	    free (indb0);
	    indb0 = gen_indsx ("", sym (hirestab_h), 4, ebp, 8);
	    movl (indb0, ebp);
	    free (indb0);
	}
	if (i == pl || i == pl - 1)
	  incl (edi);
	indb0 = gen_indsx ("", sym (hirestab_h), 0, data2, 8);
	movl (indb0, data1);
	free (indb0);
	indb0 = gen_indsx ("", sym (hirestab_h), 4, data2, 8);
	movl (indb0, data2);
	free (indb0);
	switch (realpl) {
	 case 0:
	    if (i < pl) {
		addl (esi, esi);
		addl (ebp, ebp);
		if (plmul == 2) {
		    addl (esi, esi);
		    addl (ebp, ebp);
		}
	    }
	    break;
	 case 1:
	    if (i < pl) {
		indb0 = gen_indx ("", 0, esi, 4*plmul);
		leal (indb0, esi);
		free (indb0);
		indb0 = gen_indx ("", 0, ebp, 4*plmul);
		leal (indb0, ebp);
		free (indb0);
	    }
	    addl (data1, data1);
	    addl (data2, data2);
	    break;
	 case 2:
	    if (i < pl) {
		if (plmul == 1)
		    leal (indx ("", 0, esi, 8), esi);
		else
		    shll (4, esi);
	    }
	    addl (data1, data1);
	    addl (data2, data2);
	    if (i < pl) {
		if (plmul == 1)
		    leal (indx ("", 0, ebp, 8), ebp);
		else
		    shll (4, ebp);
	    }
	    addl (data1, data1);
	    addl (data2, data2);
	    break;
	 case 3:
	    if (i < pl)
	      shll (3 + plmul, esi);
	    indb0 = gen_indx ("", 0, data1, 8);
	    leal (indb0, data1);
	    free (indb0);
	    if (i < pl)
	      shll (3 + plmul, ebp);
	    indb0 = gen_indx ("", 0, data2, 8);
	    leal (indb0, data2);
	    free (indb0);
	    break;
	 case 4: case 5: case 6: case 7:
	    shll (realpl, data1);
	    shll (realpl, data2);
	    if (i < pl) {
		shll (realpl+plmul, esi);
		shll (realpl+plmul, ebp);
	    }
	    break;
	}
	
	if (i < pl) {
	    orl (esi, ecx);
	    orl (ebp, ebx);
	    if (i + 2 <= pl)
	      movl (ind (esp, 24), esi);
	}
	if (i + 2 > pl && pl > 0)
	  movl (ind (esp, 20), ebp);
	if (i > 0 || mode == 2) {
	    orl (data1, ecx);
	    orl (data2, ebx);
	}
    }

    cmpl (ind (esp, 28), edi);
    movl (ecx, indx (ebp, -8, edi, 8));
    movl (ebx, indx (ebp, -4, edi, 8));
    jc (loop);

    popl (reg ("ebx"));
    popl (reg ("edi"));
    popl (reg ("esi"));
    popl (reg ("ebp"));
    ret ();
    printf ("\n\n");
}

/* Squeeze: every second bit does not generate a pixel 
   Not optimized, this mode isn't useful. */
static void gen_x86_set_hires_l (int pl, int mode)
{
    int plmul = mode == 0 ? 1 : 2;
    int ploff = mode == 2 ? 1 : 0;
    int i;
    int loop;
    char buf[40];

    sprintf (buf, sym (set_hires_l_%d_%d), pl, mode);
    declare_fn (buf);

    pushl (ebp);
    pushl (esi);
    pushl (edi);
    pushl (ebx);
    
    movl (ind (esp, 20), ebp);
    movl (ind (esp, 24), esi);
    movl (imm (0), edi);

    align (5);
    loop = new_label ();

    if (mode == 2) {
	movl (indx (ebp, 0, edi, 1), ecx);
    }

    for (i = 0; i <= pl; i++) {
	int realpl = i * plmul + ploff;
	char *data1 = (i == 0 && mode != 2 ? ecx : edx);
	char *indb0;

	indb0 = gen_indx (esi, realpl*MAX_WORDS_PER_LINE*2, edi, 1);
	movzbl (indb0, data1);
	free (indb0);

	indb0 = gen_indsx ("", sym (hirestab_l), 0, data1, 4);
	movl (indb0, data1);
	free (indb0);
	if (i == pl)
	    incl (edi);
	shiftleftl (realpl, data1);
	if (i > 0 || mode == 2) {
	    orl (data1, ecx);
	}
    }
    cmpl (ind (esp, 28), edi);
    movl (ecx, indx (ebp, -4, edi, 4));
    jc (loop);

    popl (reg ("ebx"));
    popl (reg ("edi"));
    popl (reg ("esi"));
    popl (reg ("ebp"));
    ret ();
    printf ("\n\n");
}

/* Stretch: two pixels per bit */
static void gen_x86_set_lores_h (int pl, int mode)
{
    int plmul = mode == 0 ? 1 : 2;
    int ploff = mode == 2 ? 1 : 0;
    int i, j;
    int loop;
    char buf[40];

    sprintf (buf, sym (set_lores_h_%d_%d), pl, mode);
    declare_fn (buf);

    pushl (ebp);
    pushl (esi);
    pushl (edi);
    pushl (ebx);
    
    movl (ind (esp, 20), ebp);
    movl (ind (esp, 24), esi);
    movl (imm (0), edi);

    align (5);
    loop = new_label ();

    for (j = 0; j < 2; j++) {
	if (mode == 2) {
	    movl (j ? ind (ebp, 8) : ind (ebp, 0), ecx);
	    movl (j ? ind (ebp, 12) : ind (ebp, 4), ebx);
	}

	for (i = 0; i <= pl; i++) {
	    int realpl = i * plmul + ploff;
	    char *data1 = (i == 0 && mode != 2 ? ecx : edx);
	    char *data2 = (i == 0 && mode != 2 ? ebx : eax);
	    char *indb0;
	    
	    indb0 = gen_indx (esi, realpl*MAX_WORDS_PER_LINE*2, edi, 1);
	    movzbl (indb0, data2);
	    free (indb0);
	    addl (data2, data2);
	    indb0 = gen_indsx ("", sym (lorestab_h), 0 + j*8, data2, 8);
	    movl (indb0, data1);
	    free (indb0);
	    indb0 = gen_indsx ("", sym (lorestab_h), 4 + j*8, data2, 8);
	    movl (indb0, data2);
	    free (indb0);
	    shiftleftl (realpl, data1);
	    shiftleftl (realpl, data2);
	    if (i > 0 || mode == 2) {
		orl (data1, ecx);
		orl (data2, ebx);
	    }
	}
	movl (ecx, j ? ind (ebp, 8) : ind (ebp, 0));
	movl (ebx, j ? ind (ebp, 12) : ind (ebp, 4));
    }
    incl (edi);
    cmpl (ind (esp, 28), edi);
    leal (ind (ebp, 16), ebp);
    jc (loop);

    popl (reg ("ebx"));
    popl (reg ("edi"));
    popl (reg ("esi"));
    popl (reg ("ebp"));
    ret ();
    printf ("\n\n");
}


/* Normal code: one pixel per bit */
static void gen_c_set_hires_h (int pl, int mode, int header)
{
    int plmul = mode == 0 ? 1 : 2;
    int ploff = mode == 2 ? 1 : 0;
    int i;

    if (header)
	printf("extern ");
    printf ("void set_hires_h_%d_%d (uae_u32 *app, uae_u8 *ptr, int len)", pl, mode);
    if (header) {
	printf (";\n");
	return;
    }

    printf ("\n\{\n\tint i;\n\tfor (i = 0; i < len; i++) {\n\t\tuae_u32 v1, v2;\n");

    if (mode == 2) {
	printf ("\t\tv1 = app[i*2 + 0]; v2 = app[i*2 + 1];\n");
    }

    for (i = 0; i <= pl; i++) {
	int realpl = i * plmul + ploff;
	char *asgn = (i == 0 && mode != 2 ? "=" : "|=");
	
	printf ("\t\t{\n");
	printf ("\t\t\tunsigned int data = *(ptr + i  + %d);\n", MAX_WORDS_PER_LINE*2*realpl);
	
	printf ("\t\t\tv1 %s hirestab_h[data][0] << %d;\n", asgn, realpl);
	printf ("\t\t\tv2 %s hirestab_h[data][1] << %d;\n", asgn, realpl);
	printf ("\t\t}\n");
    }
    printf ("\t\tapp[i*2 + 0] = v1;\n");
    printf ("\t\tapp[i*2 + 1] = v2;\n");
    printf ("\t}\n");
    printf ("}\n\n");
}

/* Squeeze: every second bit does not generate a pixel 
   Not optimized, this mode isn't useful. */
static void gen_c_set_hires_l (int pl, int mode, int header)
{
    int plmul = mode == 0 ? 1 : 2;
    int ploff = mode == 2 ? 1 : 0;
    int i;

    if (header)
	printf("extern ");
    printf ("void set_hires_l_%d_%d (uae_u32 *app, uae_u8 *ptr, int len)", pl, mode);
    if (header) {
	printf (";\n");
	return;
    }

    printf ("\n\{\n\tint i;\n\tfor (i = 0; i < len; i++) {\n\t\tuae_u32 v1;\n");

    if (mode == 2) {
	printf ("\t\tv1 = app[i];\n");
    }

    for (i = 0; i <= pl; i++) {
	int realpl = i * plmul + ploff;
	char *asgn = (i == 0 && mode != 2 ? "=" : "|=");
	
	printf ("\t\t{\n");
	printf ("\t\t\tunsigned int data = *(ptr + i  + %d);\n", MAX_WORDS_PER_LINE*2*realpl);

	printf ("\t\t\tv1 %s hirestab_l[data][0] << %d;\n", asgn, realpl);
	printf ("\t\t}\n");
    }
    printf ("\t\tapp[i] = v1;\n");
    printf ("\t}\n");
    printf ("}\n\n");
}

/* Stretch: two pixels per bit */
static void gen_c_set_lores_h (int pl, int mode, int header)
{
    int plmul = mode == 0 ? 1 : 2;
    int ploff = mode == 2 ? 1 : 0;
    int i;

    if (header)
	printf("extern ");
    printf ("void set_lores_h_%d_%d (uae_u32 *app, uae_u8 *ptr, int len)", pl, mode);
    if (header) {
	printf (";\n");
	return;
    }

    printf ("\n\{\n\tint i;\n\tfor (i = 0; i < len; i++) {\n\t\tuae_u32 v1, v2, v3, v4;\n");

    if (mode == 2) {
	printf ("\t\tv1 = app[i*4 + 0]; v2 = app[i*4 + 1]; v3 = app[i*4 + 2]; v4 = app[i*4 + 3];\n");
    }

    for (i = 0; i <= pl; i++) {
	int realpl = i * plmul + ploff;
	char *asgn = (i == 0 && mode != 2 ? "=" : "|=");

	printf ("\t\t{\n");
	printf ("\t\t\tunsigned int data = *(ptr + i  + %d);\n", MAX_WORDS_PER_LINE*2*realpl);

	printf ("\t\t\tv1 %s lorestab_h[data][0] << %d;\n", asgn, realpl);
	printf ("\t\t\tv2 %s lorestab_h[data][1] << %d;\n", asgn, realpl);
	printf ("\t\t\tv3 %s lorestab_h[data][2] << %d;\n", asgn, realpl);
	printf ("\t\t\tv4 %s lorestab_h[data][3] << %d;\n", asgn, realpl);
	printf ("\t\t}\n");
    }
    printf ("\t\tapp[i*4 + 0] = v1;\n");
    printf ("\t\tapp[i*4 + 1] = v2;\n");
    printf ("\t\tapp[i*4 + 2] = v3;\n");
    printf ("\t\tapp[i*4 + 3] = v4;\n");
    printf ("\t}\n");
    printf ("}\n\n");
}

int main(int argc, char **argv)
{
    int pl;
    int outmode;

    if (argc != 2)
	return 1;
    if (strcmp (argv[1], "C") == 0)
	outmode = 0;
    else if (strcmp (argv[1], "H") == 0)
	outmode = 1;
    else if (strcmp (argv[1], "x86") == 0)
	outmode = 2;
    else
	return 1;

    switch (outmode) {
     case 0:
	printf ("#include \"sysconfig.h\"\n");
	printf ("#include \"sysdeps.h\"\n");
	printf ("#include \"custom.h\"\n");
	printf ("#include \"p2c.h\"\n");
	break;
     case 1:
	printf ("#define MAX_WORDS_PER_LINE %d\n", MAX_WORDS_PER_LINE);
	break;
     case 2:
	printf ("#define MAX_WORDS_PER_LINE %d\n", MAX_WORDS_PER_LINE);
	printf (".text\n");
	break;
    }
    for (pl = 0; pl < 8; pl++) {
	int j;
	for (j = 0; j < (pl < 4 ? 3 : 1); j++) {
	    switch (outmode) {
	     case 0: case 1:
		gen_c_set_hires_h (pl, j, outmode);
		gen_c_set_hires_l (pl, j, outmode);
		gen_c_set_lores_h (pl, j, outmode);
		break;
	     case 2:
		gen_x86_set_hires_h (pl, j);
		gen_x86_set_hires_l (pl, j);
		gen_x86_set_lores_h (pl, j);
		break;
	    }
	}
    }

    return 0;
}
