/*
 * E-UAE - The portable Amiga Emulator
 *
 * Generate pixel output code.
 *
 * (c) 2006 Richard Drummond
 */

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

/* Output for big-endian target if true, little-endian is false. */
int do_bigendian;

typedef enum
{
    DEPTH_8BPP,
    DEPTH_16BPP,
    DEPTH_32BPP,
} DEPTH_T;
#define DEPTH_MAX DEPTH_32BPP

static const char *get_depth_str (DEPTH_T bpp)
{
   if (bpp == DEPTH_8BPP)
	return "8";
   else if (bpp == DEPTH_16BPP)
	return "16";
   else
	return "32";
}

static const char *get_depth_type_str (DEPTH_T bpp)
{
    if (bpp == DEPTH_8BPP)
	return "uae_u8";
    else if (bpp == DEPTH_16BPP)
	return "uae_u16";
    else
	return "uae_u32";
}


typedef enum
{
    HMODE_NORMAL,
    HMODE_DOUBLE,
    HMODE_DOUBLE2X,
    HMODE_HALVE,
    HMODE_HALVE2
} HMODE_T;
#define HMODE_MAX HMODE_HALVE2

static const char *get_hmode_str (HMODE_T hmode)
{
    if (hmode == HMODE_DOUBLE)
	return "_stretch1";
   else if (hmode == HMODE_DOUBLE2X)
	return "_stretch2";
   else if (hmode == HMODE_HALVE)
	return "_shrink1";
   else if (hmode == HMODE_HALVE2)
	return "_shrink2";
   else
	return "";
}


typedef enum
{
    CMODE_NORMAL,
    CMODE_DUALPF,
    CMODE_EXTRAHB,
    CMODE_HAM
} CMODE_T;
#define CMODE_MAX CMODE_HAM


static FILE *outfile;
static unsigned int outfile_indent = 0;

void set_outfile (FILE *f)
{
    outfile = f;
}

int set_indent (int indent)
{
    int old_indent = outfile_indent;
    outfile_indent = indent;
    return old_indent;
}

void outln (const char *s)
{
    unsigned int i;
    for (i = 0; i < outfile_indent; i++)
	fputc (' ', outfile);
    fprintf (outfile, "%s\n", s);
}

void outlnf (const char *s, ...)
{
    va_list ap;
    unsigned int i;
    for (i = 0; i < outfile_indent; i++)
	fputc (' ', outfile);
    va_start (ap, s);
    vfprintf (outfile, s, ap);
    fputc ('\n', outfile);
}

static void out_linetoscr_decl (DEPTH_T bpp, HMODE_T hmode, int aga, int spr)
{
    outlnf ("static int NOINLINE linetoscr_%s%s%s%s (int spix, int dpix, int stoppos)",
	    get_depth_str (bpp),
	    get_hmode_str (hmode), aga ? "_aga" : "", spr ? "_spr" : "");
}

static void out_linetoscr_do_srcpix (DEPTH_T bpp, HMODE_T hmode, int aga, CMODE_T cmode)
{
    if (aga && cmode != CMODE_DUALPF)
	outln ( 	"    spix_val = pixdata.apixels[spix] ^ xor_val;");
    else if (cmode != CMODE_HAM)
	outln ( 	"    spix_val = pixdata.apixels[spix];");
}

static void out_linetoscr_do_dstpix (DEPTH_T bpp, HMODE_T hmode, int aga, CMODE_T cmode)
{
    if (aga && cmode == CMODE_HAM)
	outln (	        "    dpix_val = CONVERT_RGB (ham_linebuf[spix]);");
    else if (cmode == CMODE_HAM)
	outln (		"    dpix_val = xcolors[ham_linebuf[spix]];");
    else if (aga && cmode == CMODE_DUALPF) {
	outln (		"    if (spritepixels[spix]) {");
	outln (		"        dpix_val = colors_for_drawing.acolors[spritepixels[spix]];");
	outln (		"    } else {");
	outln (		"        unsigned int val = lookup[spix_val];");
	outln (		"        if (lookup_no[spix_val] == 2)");
	outln (		"            val += dblpfofs[bpldualpf2of];");
	outln (		"	 val ^= xor_val;");
	outln (		"        dpix_val = colors_for_drawing.acolors[val];");
	outln (		"    }");
    } else if (cmode == CMODE_DUALPF) {
	outln (		"    dpix_val = colors_for_drawing.acolors[lookup[spix_val]];");
    } else if (aga && cmode == CMODE_EXTRAHB) {
	outln (		"    if (spix_val >= 32 && spix_val < 64) {");
	outln (		"        unsigned int c = (colors_for_drawing.color_regs_aga[spix_val - 32] >> 1) & 0x7F7F7F;");
	outln (		"        dpix_val = CONVERT_RGB (c);");
	outln (		"    } else");
	outln (		"        dpix_val = colors_for_drawing.acolors[spix_val];");
    } else if (cmode == CMODE_EXTRAHB) {
	outln (		"    if (spix_val <= 31)");
	outln (		"        dpix_val = colors_for_drawing.acolors[spix_val];");
	outln (		"    else");
	outln (		"        dpix_val = xcolors[(colors_for_drawing.color_regs_ecs[spix_val - 32] >> 1) & 0x777];");
    } else
	outln (		"    dpix_val = colors_for_drawing.acolors[spix_val];");
}

static void out_linetoscr_do_incspix (DEPTH_T bpp, HMODE_T hmode, int aga, CMODE_T cmode)
{
    if (hmode == HMODE_HALVE2) {
	outln (		"    spix++;");
	outln (		"    tmp_val = dpix_val;");
	out_linetoscr_do_srcpix (bpp, hmode, aga, cmode);
	out_linetoscr_do_dstpix (bpp, hmode, aga, cmode);
	outlnf (	"    dpix_val = merge_2pixel%d (dpix_val, tmp_val);", bpp == 0 ? 8 : bpp == 1 ? 16 : 32);
	outln (		"    spix++;");
    } else if (hmode == HMODE_HALVE)
	outln (		"    spix += 2;");
    else
	outln (		"    spix++;");
}

static void out_sprite (int off)
{
    outlnf ( "    if (spritepixels[sprx + MAX_PIXELS_PER_LINE * %d]) {", off);
    outlnf ( "        buf[dpix] = colors_for_drawing.acolors[spritepixels[sprx + MAX_PIXELS_PER_LINE * %d]];", off);
    outlnf ( "    } else {");
    outlnf ( "        buf[dpix] = out_val;");
    outlnf ( "    }");
    outlnf ( "    dpix++;");
}

static void out_linetoscr_mode (DEPTH_T bpp, HMODE_T hmode, int aga, int spr, CMODE_T cmode)
{
    int old_indent = set_indent (8);

    if (aga && cmode == CMODE_DUALPF) {
	outln (        "int *lookup    = bpldualpfpri ? dblpf_ind2_aga : dblpf_ind1_aga;");
	outln (        "int *lookup_no = bpldualpfpri ? dblpf_2nd2     : dblpf_2nd1;");
    } else if (cmode == CMODE_DUALPF)
	outln (        "int *lookup = bpldualpfpri ? dblpf_ind2 : dblpf_ind1;");


    /* TODO: add support for combining pixel writes in 8-bpp modes. */

    if (bpp == DEPTH_16BPP && hmode != HMODE_DOUBLE && hmode != HMODE_DOUBLE2X && spr == 0) {
	outln (		"int rem;");
	outln (		"if (((long)&buf[dpix]) & 2) {");
	outln (		"    uae_u32 spix_val;");
	outln (		"    uae_u32 dpix_val;");
	if (hmode == HMODE_HALVE2)
	    outln (	"    uae_u32 tmp_val;");

	out_linetoscr_do_srcpix (bpp, hmode, aga, cmode);
	out_linetoscr_do_dstpix (bpp, hmode, aga, cmode);
	out_linetoscr_do_incspix (bpp, hmode, aga, cmode);

	outln (		"    buf[dpix++] = dpix_val;");
	outln (		"}");
	outln (		"if (dpix >= stoppos)");
	outln (		"    return spix;");
	outln (		"rem = (((long)&buf[stoppos]) & 2);");
	outln (		"if (rem)");
	outln (		"    stoppos--;");
    }


    outln (		"while (dpix < stoppos) {");
    if (spr)
	outln (		"    int sprx = spix;");
    outln (		"    uae_u32 spix_val;");
    outln (		"    uae_u32 dpix_val;");
    outln (		"    uae_u32 out_val;");
    if (hmode == HMODE_HALVE2)
	outln (		"    uae_u32 tmp_val;");
    outln (		"");

    out_linetoscr_do_srcpix (bpp, hmode, aga, cmode);
    out_linetoscr_do_dstpix (bpp, hmode, aga, cmode);
    out_linetoscr_do_incspix (bpp, hmode, aga, cmode);

    outln (		"    out_val = dpix_val;");

    if (hmode != HMODE_DOUBLE && hmode != HMODE_DOUBLE2X && bpp == DEPTH_16BPP && spr == 0) {
	out_linetoscr_do_srcpix (bpp, hmode, aga, cmode);
	out_linetoscr_do_dstpix (bpp, hmode, aga, cmode);
	out_linetoscr_do_incspix (bpp, hmode, aga, cmode);

	if (do_bigendian)
	    outln (	"    out_val = (out_val << 16) | (dpix_val & 0xFFFF);");
	else
	    outln (	"    out_val = (out_val & 0xFFFF) | (dpix_val << 16);");
    }

    if (hmode == HMODE_DOUBLE) {
	if (bpp == DEPTH_8BPP) {
	    outln (	"    *((uae_u16 *)&buf[dpix]) = (uae_u16) out_val;");
	    outln (	"    dpix += 2;");
	} else if (bpp == DEPTH_16BPP) {
	    if (spr) {
		out_sprite (0);
		out_sprite (1);
	    } else {
		outln (	"    *((uae_u32 *)&buf[dpix]) = out_val;");
		outln (	"    dpix += 2;");
	    }
	} else {
	    if (spr) {
		out_sprite (0);
		out_sprite (1);
	    } else {
		outln (	"    buf[dpix++] = out_val;");
		outln (	"    buf[dpix++] = out_val;");
	    }
	}
    } else if (hmode == HMODE_DOUBLE2X) {
	if (bpp == DEPTH_8BPP) {
	    outln (	"    *((uae_u32 *)&buf[dpix]) = (uae_u32) out_val;");
	    outln (	"    dpix += 4;");
	} else if (bpp == DEPTH_16BPP) {
	    if (spr) {
		out_sprite (0);
		out_sprite (1);
		out_sprite (2);
		out_sprite (3);
	    } else {
		outln (	"    *((uae_u32 *)&buf[dpix]) = out_val;");
		outln (	"    dpix += 2;");
		outln (	"    *((uae_u32 *)&buf[dpix]) = out_val;");
		outln (	"    dpix += 2;");
	    }
	} else {
	    if (spr) {
		out_sprite (0);
		out_sprite (1);
		out_sprite (2);
		out_sprite (3);
	    } else {
		outln (	"    buf[dpix++] = out_val;");
		outln (	"    buf[dpix++] = out_val;");
		outln (	"    buf[dpix++] = out_val;");
		outln (	"    buf[dpix++] = out_val;");
	    }
	}
    } else {
	if (bpp == DEPTH_16BPP) {
	    if (spr) {
		out_sprite (0);
	    } else {
		outln (	"    *((uae_u32 *)&buf[dpix]) = out_val;");
		outln (	"    dpix += 2;");
	    }
	} else {
	    if (spr) {
		out_sprite (0);
	    } else {
		outln (	"    buf[dpix++] = out_val;");
	    }
	}
    }

    outln (		"}");


    if (bpp == DEPTH_16BPP && hmode != HMODE_DOUBLE && hmode != HMODE_DOUBLE2X && spr == 0) {
	outln (		"if (rem) {");
	outln (		"    uae_u32 spix_val;");
	outln (		"    uae_u32 dpix_val;");
	if (hmode == HMODE_HALVE2)
	    outln (	"    uae_u32 tmp_val;");

	out_linetoscr_do_srcpix (bpp, hmode, aga, cmode);
	out_linetoscr_do_dstpix (bpp, hmode, aga, cmode);
	out_linetoscr_do_incspix (bpp, hmode, aga, cmode);

	outln (		"    buf[dpix++] = dpix_val;");
	outln (		"}");
    }

    set_indent (old_indent);

    return;
}

static void out_linetoscr (DEPTH_T bpp, HMODE_T hmode, int aga, int spr)
{
    if (aga)
	outln  ("#ifdef AGA");

    out_linetoscr_decl (bpp, hmode, aga, spr);
    outln  (	"{");

    outlnf (	"    %s *buf = (%s *) xlinebuffer;", get_depth_type_str (bpp), get_depth_type_str (bpp));
    if (aga)
	outln (	"    uae_u8 xor_val = bplxor;");
    outln  (	"");

    outln  (	"    if (dp_for_drawing->ham_seen) {");
    out_linetoscr_mode (bpp, hmode, aga, spr, CMODE_HAM);
    outln  (	"    } else if (bpldualpf) {");
    out_linetoscr_mode (bpp, hmode, aga, spr, CMODE_DUALPF);
    outln  (	"    } else if (bplehb) {");
    out_linetoscr_mode (bpp, hmode, aga, spr, CMODE_EXTRAHB);
    outln  (	"    } else {");
    out_linetoscr_mode (bpp, hmode, aga, spr, CMODE_NORMAL);

    outln  (	"    }\n");
    outln  (	"    return spix;");
    outln  (	"}");

    if (aga)
	outln (	"#endif");
    outln  (	"");
}

int main (int argc, char *argv[])
{
   DEPTH_T bpp;
   int aga, spr;
   HMODE_T hmode;
   unsigned int i;

    do_bigendian = 0;

    for (i = 1; i < argc; i++) {
	if (argv[i][0] != '-')
	    continue;
	if (argv[i][1] == 'b' && argv[i][2] == '\0')
	    do_bigendian = 1;
    }

   set_outfile (stdout);

   outln ("/*");
   outln (" * E-UAE - The portable Amiga emulator.");
   outln (" *");
   outln (" * This file was generated by genlinetoscr. Don't edit.");
   outln (" */");
   outln ("");

   for (bpp = DEPTH_8BPP; bpp <= DEPTH_MAX; bpp++) {
	for (aga = 0; aga <= 1 ; aga++) {
	    for (spr = 0; spr <= 1; spr++) {
		if (spr && !aga)
		    continue;
		for (hmode = HMODE_NORMAL; hmode <= HMODE_MAX; hmode++)
		    out_linetoscr (bpp, hmode, aga, spr);
	    }
	}
    }
    return 0;
}
