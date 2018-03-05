/*
*  Copyright (C) 2002-2004  The DOSBox Team
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU Library General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software
*  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/*
*  Converted to WinUAE by Toni Wilen 2009
*  
*  Freetype to Win32 CreateFont() conversion by TW in 2010
*/

#include "sysconfig.h"
#include "sysdeps.h"
#include "uae.h"

#define WINFONT
#define C_LIBPNG

#include "epsonprinter.h"
#include "win32.h"
#include "parser.h"
#include "threaddep/thread.h"
#include "uae/io.h"

#include <math.h>

//#define DEBUGPRINT _T("C:\\Users\\twilen\\Desktop\\CMD_file.1")
int pngprint = 0;

#ifdef C_LIBPNG
#include <zlib.h>
#include <png.h>
#endif

#define PARAM16(I) (params[I+1]*256+params[I])
#define PIXX ((Bitu)floor(curX*dpiX+0.5))
#define PIXY ((Bitu)floor(curY*dpiY+0.5))

#define true 1
#define false 0

#ifdef WINFONT
static const TCHAR *epsonprintername;
static HFONT curFont;
static float curFontHorizPoints, curFontVertPoints;
static TCHAR *curFontName;
static HDC memHDC;
static LPOUTLINETEXTMETRIC otm;
#else
static FT_Library FTlib;
static FT_Face curFont;
#endif
static Real64 curX, curY;
static Bit16u dpiX, dpiY, ESCCmd;
static int ESCSeen;
static Bit8u numParam, neededParam;
static Bit8u params[20];
static Bit16u style;
static Real64 cpi, actcpi;
static Bit8u score;
static Real64 topMargin, bottomMargin, rightMargin, leftMargin;
static Real64 pageWidth, pageHeight, defaultPageWidth, defaultPageHeight;
static Real64 lineSpacing, horiztabs[32];
static Bit8u numHorizTabs;
static Real64 verttabs[16];
static Bit8u numVertTabs, curCharTable, printQuality;
static enum Typeface LQtypeFace;
static Real64 extraIntraSpace;
static int charRead, autoFeed, printUpperContr;
static int printColor, colorPrinted;
static struct bitGraphicParams {
	Bit16u horizDens, vertDens;
	int adjacent;
	Bit8u bytesColumn;
	Bit16u remBytes;
	Bit8u column[6];
	Bit8u readBytesColumn;
	int pin9;
} bitGraph;
static Bit8u densk, densl, densy, densz;
static Bit16u curMap[256], charTables[4];
static Real64 definedUnit;
static int multipoint;
static Real64 multiPointSize, multicpi, hmi;
static Bit8u msb;
static Bit16u numPrintAsChar;
static void *outputHandle;
static Bit16u multipageOutput, multiPageCounter;
static HDC printerDC;
static int justification;
#define CHARBUFFERSIZE 1000
static int charcnt;
static Bit8u charbuffer[CHARBUFFERSIZE];

static uae_u8 *page, *cpage;
static int page_w, page_h, page_pitch;
static int pagesize;
static HMODULE ft;
static int pins = 24;

static void printCharBuffer(void);

static Bit8u colors[] = {
	0x00, 0x00, 0x00, // 0 black
	0xff, 0x00, 0xff, // 1 magenta (/green)
	0x00, 0xff, 0xff, // 2 cyan (/red)
	0xff, 0x00, 0xff, // 3 violet
	0xff, 0xff, 0x00, // 4 yellow (/blue)
	0xff, 0x00, 0x00, // 5 red
	0x00, 0xff, 0x00  // 6 green
};

// Various ASCII codepage to unicode maps

static const Bit16u cp437Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x00ec,0x00c4,0x00c5,
	0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00f2,0x00fb,0x00f9,0x00ff,0x00d6,0x00dc,0x00a2,0x00a3,0x00a5,0x20a7,0x0192,
	0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,0x00bf,0x2310,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
	0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
	0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0
};

static const Bit16u cp737Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x0391,0x0392,0x0393,0x0394,0x0395,0x0396,0x0397,0x0398,0x0399,0x039a,0x039b,0x039c,0x039d,0x039e,0x039f,0x03a0,
	0x03a1,0x03a3,0x03a4,0x03a5,0x03a6,0x03a7,0x03a8,0x03a9,0x03b1,0x03b2,0x03b3,0x03b4,0x03b5,0x03b6,0x03b7,0x03b8,
	0x03b9,0x03ba,0x03bb,0x03bc,0x03bd,0x03be,0x03bf,0x03c0,0x03c1,0x03c3,0x03c2,0x03c4,0x03c5,0x03c6,0x03c7,0x03c8,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
	0x03c9,0x03ac,0x03ad,0x03ae,0x03ca,0x03af,0x03cc,0x03cd,0x03cb,0x03ce,0x0386,0x0388,0x0389,0x038a,0x038c,0x038e,
	0x038f,0x00b1,0x2265,0x2264,0x03aa,0x03ab,0x00f7,0x2248,0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0
};

static const Bit16u cp775Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x0106,0x00fc,0x00e9,0x0101,0x00e4,0x0123,0x00e5,0x0107,0x0142,0x0113,0x0156,0x0157,0x012b,0x0179,0x00c4,0x00c5,
	0x00c9,0x00e6,0x00c6,0x014d,0x00f6,0x0122,0x00a2,0x015a,0x015b,0x00d6,0x00dc,0x00f8,0x00a3,0x00d8,0x00d7,0x00a4,
	0x0100,0x012a,0x00f3,0x017b,0x017c,0x017a,0x201d,0x00a6,0x00a9,0x00ae,0x00ac,0x00bd,0x00bc,0x0141,0x00ab,0x00bb,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x0104,0x010c,0x0118,0x0116,0x2563,0x2551,0x2557,0x255d,0x012e,0x0160,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x0172,0x016a,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x017d,
	0x0105,0x010d,0x0119,0x0117,0x012f,0x0161,0x0173,0x016b,0x017e,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
	0x00d3,0x00df,0x014c,0x0143,0x00f5,0x00d5,0x00b5,0x0144,0x0136,0x0137,0x013b,0x013c,0x0146,0x0112,0x0145,0x2019,
	0x00ad,0x00b1,0x201c,0x00be,0x00b6,0x00a7,0x00f7,0x201e,0x00b0,0x2219,0x00b7,0x00b9,0x00b3,0x00b2,0x25a0,0x00a0
};

static const Bit16u cp850Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x00ec,0x00c4,0x00c5,
	0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00f2,0x00fb,0x00f9,0x00ff,0x00d6,0x00dc,0x00f8,0x00a3,0x00d8,0x00d7,0x0192,
	0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,0x00bf,0x00ae,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x00c1,0x00c2,0x00c0,0x00a9,0x2563,0x2551,0x2557,0x255d,0x00a2,0x00a5,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x00e3,0x00c3,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x00a4,
	0x00f0,0x00d0,0x00ca,0x00cb,0x00c8,0x0131,0x00cd,0x00ce,0x00cf,0x2518,0x250c,0x2588,0x2584,0x00a6,0x00cc,0x2580,
	0x00d3,0x00df,0x00d4,0x00d2,0x00f5,0x00d5,0x00b5,0x00fe,0x00de,0x00da,0x00db,0x00d9,0x00fd,0x00dd,0x00af,0x00b4,
	0x00ad,0x00b1,0x2017,0x00be,0x00b6,0x00a7,0x00f7,0x00b8,0x00b0,0x00a8,0x00b7,0x00b9,0x00b3,0x00b2,0x25a0,0x00a0
};

static const Bit16u cp852Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x016f,0x0107,0x00e7,0x0142,0x00eb,0x0150,0x0151,0x00ee,0x0179,0x00c4,0x0106,
	0x00c9,0x0139,0x013a,0x00f4,0x00f6,0x013d,0x013e,0x015a,0x015b,0x00d6,0x00dc,0x0164,0x0165,0x0141,0x00d7,0x010d,
	0x00e1,0x00ed,0x00f3,0x00fa,0x0104,0x0105,0x017d,0x017e,0x0118,0x0119,0x00ac,0x017a,0x010c,0x015f,0x00ab,0x00bb,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x00c1,0x00c2,0x011a,0x015e,0x2563,0x2551,0x2557,0x255d,0x017b,0x017c,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x0102,0x0103,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x00a4,
	0x0111,0x0110,0x010e,0x00cb,0x010f,0x0147,0x00cd,0x00ce,0x011b,0x2518,0x250c,0x2588,0x2584,0x0162,0x016e,0x2580,
	0x00d3,0x00df,0x00d4,0x0143,0x0144,0x0148,0x0160,0x0161,0x0154,0x00da,0x0155,0x0170,0x00fd,0x00dd,0x0163,0x00b4,
	0x00ad,0x02dd,0x02db,0x02c7,0x02d8,0x00a7,0x00f7,0x00b8,0x00b0,0x00a8,0x02d9,0x0171,0x0158,0x0159,0x25a0,0x00a0
};

static const Bit16u cp855Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x0452,0x0402,0x0453,0x0403,0x0451,0x0401,0x0454,0x0404,0x0455,0x0405,0x0456,0x0406,0x0457,0x0407,0x0458,0x0408,
	0x0459,0x0409,0x045a,0x040a,0x045b,0x040b,0x045c,0x040c,0x045e,0x040e,0x045f,0x040f,0x044e,0x042e,0x044a,0x042a,
	0x0430,0x0410,0x0431,0x0411,0x0446,0x0426,0x0434,0x0414,0x0435,0x0415,0x0444,0x0424,0x0433,0x0413,0x00ab,0x00bb,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x0445,0x0425,0x0438,0x0418,0x2563,0x2551,0x2557,0x255d,0x0439,0x0419,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x043a,0x041a,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x00a4,
	0x043b,0x041b,0x043c,0x041c,0x043d,0x041d,0x043e,0x041e,0x043f,0x2518,0x250c,0x2588,0x2584,0x041f,0x044f,0x2580,
	0x042f,0x0440,0x0420,0x0441,0x0421,0x0442,0x0422,0x0443,0x0423,0x0436,0x0416,0x0432,0x0412,0x044c,0x042c,0x2116,
	0x00ad,0x044b,0x042b,0x0437,0x0417,0x0448,0x0428,0x044d,0x042d,0x0449,0x0429,0x0447,0x0427,0x00a7,0x25a0,0x00a0
};

static const Bit16u cp857Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x0131,0x00c4,0x00c5,
	0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00f2,0x00fb,0x00f9,0x0130,0x00d6,0x00dc,0x00f8,0x00a3,0x00d8,0x015e,0x015f,
	0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x011e,0x011f,0x00bf,0x00ae,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x00c1,0x00c2,0x00c0,0x00a9,0x2563,0x2551,0x2557,0x255d,0x00a2,0x00a5,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x00e3,0x00c3,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x00a4,
	0x00ba,0x00aa,0x00ca,0x00cb,0x00c8,0x0000,0x00cd,0x00ce,0x00cf,0x2518,0x250c,0x2588,0x2584,0x00a6,0x00cc,0x2580,
	0x00d3,0x00df,0x00d4,0x00d2,0x00f5,0x00d5,0x00b5,0x0000,0x00d7,0x00da,0x00db,0x00d9,0x00ec,0x00ff,0x00af,0x00b4,
	0x00ad,0x00b1,0x0000,0x00be,0x00b6,0x00a7,0x00f7,0x00b8,0x00b0,0x00a8,0x00b7,0x00b9,0x00b3,0x00b2,0x25a0,0x00a0
};

static const Bit16u cp860Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x00c7,0x00fc,0x00e9,0x00e2,0x00e3,0x00e0,0x00c1,0x00e7,0x00ea,0x00ca,0x00e8,0x00cd,0x00d4,0x00ec,0x00c3,0x00c2,
	0x00c9,0x00c0,0x00c8,0x00f4,0x00f5,0x00f2,0x00da,0x00f9,0x00cc,0x00d5,0x00dc,0x00a2,0x00a3,0x00d9,0x20a7,0x00d3,
	0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,0x00bf,0x00d2,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
	0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
	0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0
};

static const Bit16u cp861Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,0x00ea,0x00eb,0x00e8,0x00d0,0x00f0,0x00de,0x00c4,0x00c5,
	0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00fe,0x00fb,0x00dd,0x00fd,0x00d6,0x00dc,0x00f8,0x00a3,0x00d8,0x20a7,0x0192,
	0x00e1,0x00ed,0x00f3,0x00fa,0x00c1,0x00cd,0x00d3,0x00da,0x00bf,0x2310,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
	0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
	0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0
};

static const Bit16u cp862Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x05d0,0x05d1,0x05d2,0x05d3,0x05d4,0x05d5,0x05d6,0x05d7,0x05d8,0x05d9,0x05da,0x05db,0x05dc,0x05dd,0x05de,0x05df,
	0x05e0,0x05e1,0x05e2,0x05e3,0x05e4,0x05e5,0x05e6,0x05e7,0x05e8,0x05e9,0x05ea,0x00a2,0x00a3,0x00a5,0x20a7,0x0192,
	0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,0x00bf,0x2310,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
	0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
	0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0
};

static const Bit16u cp863Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x00c7,0x00fc,0x00e9,0x00e2,0x00c2,0x00e0,0x00b6,0x00e7,0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x2017,0x00c0,0x00a7,
	0x00c9,0x00c8,0x00ca,0x00f4,0x00cb,0x00cf,0x00fb,0x00f9,0x00a4,0x00d4,0x00dc,0x00a2,0x00a3,0x00d9,0x00db,0x0192,
	0x00a6,0x00b4,0x00f3,0x00fa,0x00a8,0x00b8,0x00b3,0x00af,0x00ce,0x2310,0x00ac,0x00bd,0x00bc,0x00be,0x00ab,0x00bb,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
	0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
	0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0
};

static const Bit16u cp864Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x066a,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x00b0,0x00b7,0x2219,0x221a,0x2592,0x2500,0x2502,0x253c,0x2524,0x252c,0x251c,0x2534,0x2510,0x250c,0x2514,0x2518,
	0x03b2,0x221e,0x03c6,0x00b1,0x00bd,0x00bc,0x2248,0x00ab,0x00bb,0xfef7,0xfef8,0x0000,0x0000,0xfefb,0xfefc,0x0000,
	0x00a0,0x00ad,0xfe82,0x00a3,0x00a4,0xfe84,0x0000,0x0000,0xfe8e,0xfe8f,0xfe95,0xfe99,0x060c,0xfe9d,0xfea1,0xfea5,
	0x0660,0x0661,0x0662,0x0663,0x0664,0x0665,0x0666,0x0667,0x0668,0x0669,0xfed1,0x061b,0xfeb1,0xfeb5,0xfeb9,0x061f,
	0x00a2,0xfe80,0xfe81,0xfe83,0xfe85,0xfeca,0xfe8b,0xfe8d,0xfe91,0xfe93,0xfe97,0xfe9b,0xfe9f,0xfea3,0xfea7,0xfea9,
	0xfeab,0xfead,0xfeaf,0xfeb3,0xfeb7,0xfebb,0xfebf,0xfec1,0xfec5,0xfecb,0xfecf,0x00a6,0x00ac,0x00f7,0x00d7,0xfec9,
	0x0640,0xfed3,0xfed7,0xfedb,0xfedf,0xfee3,0xfee7,0xfeeb,0xfeed,0xfeef,0xfef3,0xfebd,0xfecc,0xfece,0xfecd,0xfee1,
	0xfe7d,0x0651,0xfee5,0xfee9,0xfeec,0xfef0,0xfef2,0xfed0,0xfed5,0xfef5,0xfef6,0xfedd,0xfed9,0xfef1,0x25a0,
};

static const Bit16u cp865Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x00ec,0x00c4,0x00c5,
	0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00f2,0x00fb,0x00f9,0x00ff,0x00d6,0x00dc,0x00f8,0x00a3,0x00d8,0x20a7,0x0192,
	0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,0x00bf,0x2310,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00a4,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
	0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
	0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0
};

static const Bit16u cp866Map[256] = {
	0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007,0x0008,0x0009,0x000a,0x000b,0x000c,0x000d,0x000e,0x000f,
	0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017,0x0018,0x0019,0x001a,0x001b,0x001c,0x001d,0x001e,0x001f,
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002a,0x002b,0x002c,0x002d,0x002e,0x002f,
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003a,0x003b,0x003c,0x003d,0x003e,0x003f,
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004a,0x004b,0x004c,0x004d,0x004e,0x004f,
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005a,0x005b,0x005c,0x005d,0x005e,0x005f,
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006a,0x006b,0x006c,0x006d,0x006e,0x006f,
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007a,0x007b,0x007c,0x007d,0x007e,0x007f,
	0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0416,0x0417,0x0418,0x0419,0x041a,0x041b,0x041c,0x041d,0x041e,0x041f,
	0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,0x0426,0x0427,0x0428,0x0429,0x042a,0x042b,0x042c,0x042d,0x042e,0x042f,
	0x0430,0x0431,0x0432,0x0433,0x0434,0x0435,0x0436,0x0437,0x0438,0x0439,0x043a,0x043b,0x043c,0x043d,0x043e,0x043f,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
	0x0440,0x0441,0x0442,0x0443,0x0444,0x0445,0x0446,0x0447,0x0448,0x0449,0x044a,0x044b,0x044c,0x044d,0x044e,0x044f,
	0x0401,0x0451,0x0404,0x0454,0x0407,0x0457,0x040e,0x045e,0x00b0,0x2219,0x00b7,0x221a,0x2116,0x00a4,0x25a0,0x00a0
};

static const Bit16u codepages[15] = {0, 437, 932, 850, 851, 853, 855, 860, 863, 865, 852, 857, 862, 864, 866};

// TODO: Implement all international charsets
static const Bit16u intCharSets[15][12] =
{
	{0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e}, // USA
	{0x0023, 0x0024, 0x00e0, 0x00ba, 0x00e7, 0x00a7, 0x005e, 0x0060, 0x00e9, 0x00f9, 0x00e8, 0x00a8}, // France
	{0x0023, 0x0024, 0x00a7, 0x00c4, 0x00d6, 0x00dc, 0x005e, 0x0060, 0x00e4, 0x00f6, 0x00fc, 0x00df}, // Germany
	{0x00a3, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e}, // UK
	{0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e}, // Denmark I
	{0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e}, // Sweden
	{0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e}, // Italy
	{0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e}, // Spain
	{0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e}, // Japan
	{0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e}, // Norway
	{0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e}, // Denmark II
	{0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e}, // Spain II
	{0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e}, // Latin America
	{0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e},
	{0x0023, 0x0024, 0x00a7, 0x00c4, 0x0027, 0x0022, 0x00b6, 0x0060, 0x00a9, 0x00ae, 0x2020, 0x2122} // Legal
};


static void selectCodepage(Bit16u cp)
{
	int i;
	const Bit16u *mapToUse = NULL;

	switch(cp)
	{
	case 0: // Italics, use cp437
	case 437:
		mapToUse = cp437Map;
		break;
	case 737:
		mapToUse = cp737Map;
		break;
	case 775:
		mapToUse = cp775Map;
		break;
	case 850:
		mapToUse = cp850Map;
		break;
	case 852:
		mapToUse = cp852Map;
		break;
	case 855:
		mapToUse = cp855Map;
		break;
	case 857:
		mapToUse = cp857Map;
		break;
	case 860:
		mapToUse = cp860Map;
		break;
	case 861:
		mapToUse = cp861Map;
		break;
	case 863:
		mapToUse = cp863Map;
		break;
	case 864:
		mapToUse = cp864Map;
		break;
	case 865:
		mapToUse = cp865Map;
		break;
	case 866:
		mapToUse = cp866Map;
		break;
	default:
		write_log(_T("Unsupported codepage %i. Using CP437 instead.\n"), cp);
		mapToUse = cp437Map;
	}

	for (i=0; i<256; i++)
		curMap[i] = mapToUse[i];
}


static int selectfont(Bit16u style)
{
	static TCHAR *thisFontName;
	static float thisFontHorizPoints;
	static float thisFontVertPoints;
	static Bit16u thisStyle;

	if (curFont) {
		for (;;) {
			if (thisFontName != curFontName)
				break;
			if (thisFontHorizPoints != curFontHorizPoints)
				break;
			if (thisFontVertPoints != curFontVertPoints)
				break;
			if ((thisStyle & (STYLE_ITALICS | STYLE_PROP)) != (style & (STYLE_ITALICS | STYLE_PROP)))
				break;
			// still using same font
			return 1;
		}
		DeleteObject (curFont);
		curFont = NULL;
		thisFontName = NULL;
		xfree (otm);
		otm = NULL;
	}
	thisFontHorizPoints = curFontHorizPoints;
	thisFontVertPoints = curFontVertPoints;
	thisStyle = style;
	thisFontName = curFontName;

	int ly = GetDeviceCaps (memHDC, LOGPIXELSY);
	int lx = GetDeviceCaps (memHDC, LOGPIXELSX);
	int rounds = 0;
	while (rounds < 2) {
		curFont = CreateFont (thisFontVertPoints * dpiY / ly + 0.5, thisFontHorizPoints * dpiX / lx + 0.5,
			0, 0,
			FW_NORMAL,
			(style & STYLE_ITALICS) ? TRUE : FALSE,
			FALSE,
			FALSE,
			DEFAULT_CHARSET,
			OUT_TT_PRECIS,
			CLIP_DEFAULT_PRECIS,
			PROOF_QUALITY,
			((style & STYLE_PROP) ? VARIABLE_PITCH : FIXED_PITCH) | FF_DONTCARE,
			thisFontName);
		if (curFont)
			break;
		rounds++;
		if (style & STYLE_PROP)
			thisFontName = curFontName = _T("Times New Roman");
		else
			thisFontName = curFontName = _T("Courier New");
	}
	if (curFont) {
		SelectObject (memHDC, curFont);
		int size = GetOutlineTextMetrics (memHDC, 0, NULL);
		if (size == 0) {
			DeleteObject(curFont);
			curFont = NULL;
		} else {
			otm = (LPOUTLINETEXTMETRIC)xmalloc (uae_u8, size);
			GetOutlineTextMetrics (memHDC, size, otm);
		}
	}
	return curFont ? 1 : 0;
}

static void updateFont(void)
{
	Real64 horizPoints = 10.5;
	Real64 vertPoints = 10.5;
	TCHAR *fontName;

	if (curFont != NULL)
#ifdef WINFONT
		DeleteObject (curFont);
#else
		FT_Done_Face(curFont);
#endif
	curFont = NULL;
	int prop = style & STYLE_PROP;

	switch (LQtypeFace)
	{
	case roman:
	default:
		if (prop)
			fontName = _T("Times New Roman");
		else
			fontName = _T("Courier New");
		break;
	case sansserif:
		if (prop)
			fontName = _T("Arial");
		else
			fontName = _T("Lucida Console");
		break;
	}

#ifdef WINFONT
	curFontName = fontName;
#else
	if (!ft) {
		write_log(_T("EPSONPRINTER: No freetype6.dll, unable to load font %s\n"), fontName);
		curFont = NULL;
	} else if (FT_New_Face(FTlib, fontName, 0, &curFont)) {
		char windowsdir[MAX_DPATH];
		GetWindowsDirectoryA (windowsdir, sizeof windowsdir);
		strcat (windowsdir, "\\Fonts\\");
		strcat (windowsdir, fontName);
		if (FT_New_Face(FTlib, windowsdir, 0, &curFont))
		{
			GetWindowsDirectoryA (windowsdir, sizeof windowsdir);
			strcat (windowsdir, "\\Fonts\\");
			strcat (windowsdir, "times.ttf");
			if (FT_New_Face(FTlib, windowsdir, 0, &curFont)) {
				write_log(_T("Unable to load font %s\n"), fontName);
				curFont = NULL;
			}
		}
	}
#endif
	if (!multipoint)
	{
		actcpi = cpi;

		if (cpi != 10 && !(style & STYLE_CONDENSED))
		{
			horizPoints *= (Real64)10/(Real64)cpi;
			vertPoints *= (Real64)10/(Real64)cpi;
		}

		if (!(style & STYLE_PROP))
		{
			if (cpi == 10 && (style & STYLE_CONDENSED))
			{
				actcpi = 17.14;
				horizPoints *= (Real64)10/(Real64)17.14;
				vertPoints *= (Real64)10/(Real64)17.14;
			}

			if (cpi == 12 && (style & STYLE_CONDENSED))
			{
				actcpi = 20.0;
				horizPoints *= (Real64)10/(Real64)20.0;
				vertPoints *= (Real64)10/(Real64)20.0;
			}	
		}

		if (style & (STYLE_PROP | STYLE_CONDENSED))
		{
			horizPoints /= (Real64)2.0;
			vertPoints /= (Real64)2.0;
		}

		if ((style & STYLE_DOUBLEWIDTH) || (style & STYLE_DOUBLEWIDTHONELINE))
		{
			actcpi /= 2;
			horizPoints *= (Real64)2.0;
		}

		if (style & STYLE_DOUBLEHEIGHT)
			vertPoints *= (Real64)2.0;
	}
	else
	{
		actcpi = multicpi;
		horizPoints = vertPoints = multiPointSize;
	}

	if ((style & STYLE_SUPERSCRIPT) || (style & STYLE_SUBSCRIPT))
	{
		horizPoints *= (Real64)2/(Real64)3;
		vertPoints *= (Real64)2/(Real64)3;
		actcpi /= (Real64)2/(Real64)3;
	}

#ifdef WINFONT
	curFontHorizPoints = horizPoints;
	curFontVertPoints = vertPoints;
#else
	if (curFont)
		FT_Set_Char_Size(curFont, (Bit16u)horizPoints*64, (Bit16u)vertPoints*64, dpiX, dpiY);
#endif

	if (style & STYLE_ITALICS || charTables[curCharTable] == 0)
	{
#ifndef WINFONT
		FT_Matrix  matrix;
		matrix.xx = 0x10000L;
		matrix.xy = (FT_Fixed)(0.20 * 0x10000L);
		matrix.yx = 0;
		matrix.yy = 0x10000L;
		if (curFont)
			FT_Set_Transform(curFont, &matrix, 0);
#endif
	}
}

static void getfname (TCHAR *fname)
{
	TCHAR tmp[MAX_DPATH];
	int number = 0;

	fetch_screenshotpath (tmp, sizeof tmp / sizeof (TCHAR));
	for (;;) {
		FILE *fp;
		_stprintf (fname, _T("%sPRINT_%03d.png"), tmp, number);
		if ((fp = uae_tfopen(fname, _T("rb"))) == NULL)
			return;
		number++;
		fclose (fp);
	}
}

static int volatile prt_thread_mode;

STATIC_INLINE void getcolor (uae_u8 *Tpage, uae_u8 *Tcpage, int x, int y, int Tpage_pitch, Bit8u *r, Bit8u *g, Bit8u *b)
{
	Bit8u pixel = *((Bit8u*)Tpage + x + (y*Tpage_pitch));
	Bit8u c = *((Bit8u*)Tcpage + x + (y*Tpage_pitch));
	Bit8u color_r = 0, color_g = 0, color_b = 0;
	if (c) {
		Bit32u color = 0;
		int cindex = 0;
		while (c) {
			if (c & 1) {
				color_r |= (255 - colors[cindex * 3 + 0]) * pixel / 255;
				color_g |= (255 - colors[cindex * 3 + 1]) * pixel / 255;
				color_b |= (255 - colors[cindex * 3 + 2]) * pixel / 255;
			}
			cindex++;
			c >>= 1;
		}
	}
	*r = 255 - color_r;
	*g = 255 - color_g;
	*b = 255 - color_b;
}

static void *prt_thread (void *p) 
{
	Bit16u x, y;
	HDC TprinterDC = printerDC;
	HDC TmemHDC = memHDC;
	int Tpage_w = page_w;
	int Tpage_h = page_h;
	int Tpage_pitch = page_pitch;
	uae_u8 *Tpage = page;
	uae_u8 *Tcpage = cpage;
	int TcolorPrinter = colorPrinted;

	write_log (_T("EPSONPRINTER: background print thread started\n"));
	prt_thread_mode = 1;
	SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_BELOW_NORMAL);

	if (TprinterDC)
	{
		int hz = GetDeviceCaps (TprinterDC, PHYSICALWIDTH);
		int vz = GetDeviceCaps (TprinterDC, PHYSICALHEIGHT);
		int topmargin = GetDeviceCaps (TprinterDC, PHYSICALOFFSETX);
		int leftmargin = GetDeviceCaps (TprinterDC, PHYSICALOFFSETY);
		HDC dc = NULL;

		write_log (_T("EPSONPRINTER: HP=%d WP=%d TM=%d LM=%d W=%d H=%d\n"),
			hz, vz, topmargin, leftmargin, Tpage_w, Tpage_h);

		if (TcolorPrinter)
			dc = GetDC (NULL);
		HBITMAP bitmap = CreateCompatibleBitmap (dc ? dc : TmemHDC, Tpage_w, Tpage_h);
		SelectObject (TmemHDC, bitmap);
		BitBlt (TmemHDC, 0, 0, Tpage_w, Tpage_h, NULL, 0, 0, WHITENESS);

		// Start new printer job?
		if (outputHandle == NULL)
		{
			DOCINFO docinfo;
			docinfo.cbSize = sizeof (docinfo);
			docinfo.lpszDocName = _T("WinUAE Epson Printer");
			docinfo.lpszOutput = NULL;
			docinfo.lpszDatatype = NULL;
			docinfo.fwType = 0;

			StartDoc (TprinterDC, &docinfo);
			multiPageCounter = 1;
		}

		StartPage (TprinterDC);

		// this really needs to use something else than SetPixel()..
		for (y=0; y<Tpage_h; y++)
		{
			for (x=0; x<Tpage_w; x++)
			{
				Bit8u r, g, b;
				getcolor (Tpage, Tcpage, x, y, Tpage_pitch, &r, &g, &b);
				if (r != 255 || g != 255 || b != 255)
					SetPixel (TmemHDC, x, y, RGB(r, g, b));
			}
		}

		BitBlt (TprinterDC, leftmargin, topmargin, Tpage_w, Tpage_h, TmemHDC, 0, 0, SRCCOPY);

		EndPage (TprinterDC);
		EndDoc (TprinterDC);

		DeleteObject (bitmap);
		if (dc)
			ReleaseDC (NULL, dc);

	}
#ifdef C_LIBPNG
	else
	{
		png_structp png_ptr;
		png_infop info_ptr;
		png_bytep * row_pointers;
		png_color palette[256];
		Bitu i;
		TCHAR fname[MAX_DPATH];
		FILE *fp;
		Bit8u *bm = NULL;

		getfname (fname);
		/* Open the actual file */
		fp = uae_tfopen(fname, _T("wb"));
		if (!fp) 
		{
			write_log(_T("EPSONPRINTER: Can't open file %s for printer output\n"), fname);
			goto end;
		}

		/* First try to alloacte the png structures */
		png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,NULL, NULL);
		if (!png_ptr)
			goto end;
		info_ptr = png_create_info_struct(png_ptr);
		if (!info_ptr) {
			png_destroy_write_struct(&png_ptr,(png_infopp)NULL);
			goto end;
		}

		/* Finalize the initing of png library */
		png_init_io(png_ptr, fp);
		png_set_compression_level(png_ptr,Z_BEST_COMPRESSION);

		/* set other zlib parameters */
		png_set_compression_mem_level(png_ptr, 8);
		png_set_compression_strategy(png_ptr,Z_DEFAULT_STRATEGY);
		png_set_compression_window_bits(png_ptr, 15);
		png_set_compression_method(png_ptr, 8);
		png_set_compression_buffer_size(png_ptr, 8192);

		// Allocate an array of scanline pointers
		row_pointers = (png_bytep*)malloc(Tpage_h*sizeof(png_bytep));

		if (TcolorPrinter) {
			png_set_IHDR(png_ptr, info_ptr, Tpage_w, Tpage_h,
				8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
				PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
			bm = xcalloc (Bit8u, Tpage_w * Tpage_h * 3);
			for (i=0; i<Tpage_h; i++) 
				row_pointers[i] = bm + i * Tpage_w * 3;
			for (int y = 0; y < Tpage_h; y++) {
				for (int x = 0; x < Tpage_w; x++) {
					Bit8u r, g, b;
					getcolor (Tpage, Tcpage, x, y, Tpage_pitch, &r, &g, &b);
					bm[y * Tpage_w * 3 + x * 3 + 0] = r;
					bm[y * Tpage_w * 3 + x * 3 + 1] = g;
					bm[y * Tpage_w * 3 + x * 3 + 2] = b;
				}
			}
		} else {
			png_set_IHDR(png_ptr, info_ptr, Tpage_w, Tpage_h,
				8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
				PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
			for (i=0;i<256;i++) 
			{
				palette[i].red = 255 - i;
				palette[i].green = 255 - i;
				palette[i].blue = 255 - i;
			}
			png_set_PLTE(png_ptr, info_ptr, palette,256);
			for (i=0; i<Tpage_h; i++) 
				row_pointers[i] = ((Bit8u*)Tpage+(i*Tpage_pitch));
		}

		// tell the png library what to encode.
		png_set_rows(png_ptr, info_ptr, row_pointers);

		// Write image to file
		png_write_png(png_ptr, info_ptr, 0, NULL);

		/*close file*/
		fclose(fp);

		/*Destroy PNG structs*/
		png_destroy_write_struct(&png_ptr, &info_ptr);

		/*clean up dynamically allocated RAM.*/
		xfree (row_pointers);
		xfree (bm);
		ShellExecute (NULL, _T("open"), fname, NULL, NULL, SW_SHOWNORMAL);
	}
#endif
end:
	xfree (Tpage);
	xfree (Tcpage);
	if (TprinterDC)
		DeleteObject (TprinterDC);
	DeleteObject (TmemHDC);
	write_log (_T("EPSONPRINTER: background thread finished\n"));
	return 0;
}

static void outputPage(void)
{
	prt_thread_mode = 0;
	if (uae_start_thread (_T("epson"), prt_thread, NULL, NULL)) {
		while (prt_thread_mode == 0)
			Sleep(5);
		memHDC = NULL;
		printerDC = NULL;
		page = NULL;
		cpage = NULL;
		if (curFont)
			DeleteObject (curFont);
		curFont = NULL;
	}
}

static void newPage(int save)
{
	printCharBuffer ();
	if (save)
		outputPage ();
	if (page == NULL) {
		page = xcalloc (uae_u8, pagesize);
		cpage = xcalloc (uae_u8, pagesize);
		printerDC = CreateDC (NULL, epsonprintername, NULL, NULL);
		memHDC = CreateCompatibleDC (NULL);
	}
	curY = topMargin;
	memset (page, 0, pagesize);
}

static void initPrinter(void)
{
	Bitu i;
	curX = curY = 0.0;
	ESCSeen = false;
	ESCCmd = 0;
	numParam = neededParam = 0;
	topMargin = 0.0;
	leftMargin = 0.0;
	rightMargin = pageWidth = defaultPageWidth;
	bottomMargin = pageHeight = defaultPageHeight;
	lineSpacing = (Real64)1/6;
	cpi = 10.0;
	curCharTable = 1;
	style = 0;
	extraIntraSpace = 0.0;
	printUpperContr = true;
	bitGraph.remBytes = 0;
	densk = 0;
	densl = 1;
	densy = 2;
	densz = 3;
	charTables[0] = 0; // Italics
	charTables[1] = charTables[2] = charTables[3] = 437;
	definedUnit = -1;
	multipoint = false;
	multiPointSize = 0.0;
	multicpi = 0.0;
	hmi = -1.0;
	msb = 255;
	numPrintAsChar = 0;
	LQtypeFace = roman;
	justification = JUST_LEFT;
	charcnt = 0;
	printColor = 0;

	selectCodepage(charTables[curCharTable]);

	updateFont();

	newPage(false);

	// Default tabs => Each eight characters
	for (i = 0; i < 32; i++)
		horiztabs[i] = i * 8 * (1.0 / (Real64)cpi);
	numHorizTabs = 32;

	numVertTabs = 255;
}

static void resetPrinterHard(void)
{
	charRead = false;
	initPrinter();
}

static int printer_init(Bit16u dpi2, Bit16u width, Bit16u height, const TCHAR *printername, int multipageOutput2, int numpins)
{
	pins = numpins;
#ifndef WINFONT
	if (ft == NULL || FT_Init_FreeType(&FTlib))
	{
		write_log(_T("EPSONPRINTER: Unable to init Freetype2. ASCII printing disabled\n"));
		return 0;
	}
#endif
	dpiX = dpiY = dpi2;
	multipageOutput = multipageOutput2;

	defaultPageWidth = (Real64)width/(Real64)10;
	defaultPageHeight = (Real64)height/(Real64)10;

	if (printername)
	{
#if 0
		// Show Print dialog to obtain a printer device context
		PRINTDLG pd;
		pd.lStructSize = sizeof(PRINTDLG); 
		pd.hDevMode = (HANDLE) NULL; 
		pd.hDevNames = (HANDLE) NULL; 
		pd.Flags = PD_RETURNDC; 
		pd.hwndOwner = NULL; 
		pd.hDC = (HDC) NULL; 
		pd.nFromPage = 1; 
		pd.nToPage = 1; 
		pd.nMinPage = 0; 
		pd.nMaxPage = 0; 
		pd.nCopies = 1; 
		pd.hInstance = NULL; 
		pd.lCustData = 0L; 
		pd.lpfnPrintHook = (LPPRINTHOOKPROC) NULL; 
		pd.lpfnSetupHook = (LPSETUPHOOKPROC) NULL; 
		pd.lpPrintTemplateName = (LPWSTR) NULL; 
		pd.lpSetupTemplateName = (LPWSTR)  NULL; 
		pd.hPrintTemplate = (HANDLE) NULL; 
		pd.hSetupTemplate = (HANDLE) NULL; 
		PrintDlg(&pd);
		printerDC = pd.hDC;
#endif
		epsonprintername = printername;
		printerDC = CreateDC (NULL, epsonprintername, NULL, NULL);
		if (!printerDC)
			return 0;

		dpiX = GetDeviceCaps(printerDC, LOGPIXELSX);
		dpiY = GetDeviceCaps(printerDC, LOGPIXELSY);
		defaultPageWidth = (Real64)GetDeviceCaps(printerDC, HORZRES) / dpiX;
		defaultPageHeight = (Real64)GetDeviceCaps(printerDC, VERTRES) / dpiY;
	}

	// Create page
	page_w = (Bitu)(defaultPageWidth*dpiX);
	page_h = (Bitu)(defaultPageHeight*dpiY);
	pagesize =  page_w * page_h;
	page_pitch = page_w;
	page = xcalloc (uae_u8, pagesize);
	cpage = xcalloc (uae_u8, pagesize);
	curFont = NULL;
	charRead = false;
	autoFeed = false;
	outputHandle = NULL;
	write_log (_T("EPSONPRINTER: Page size: %dx%d DPI: %dx%d\n"),
		page_w, page_h, dpiX, dpiY);

	initPrinter();

	memHDC = CreateCompatibleDC (NULL);

	return 1;
};


static void printer_close(void)
{
	if (page != NULL) {
		xfree (page);
		page = NULL;
		xfree (cpage);
		cpage = NULL;
#ifndef WINFONT
		if (ft)
			FT_Done_FreeType(FTlib);
#endif
		write_log (_T("EPSONPRINTER: end\n"));
	}
	xfree (otm);
	otm = NULL;
	if (curFont)
		DeleteObject (curFont);
	curFont = NULL;
	if (printerDC)
		DeleteDC(printerDC);
	printerDC = NULL;
	if (memHDC)
		DeleteDC(memHDC);
	memHDC = NULL;
};


static void setupBitImage(Bit8u dens, Bit16u numCols, int pin9)
{
	switch (dens)
	{
	case 0:
		bitGraph.horizDens = 60;
		bitGraph.vertDens = 60;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 1;
		break;
	case 1:
		bitGraph.horizDens = 120;
		bitGraph.vertDens = 60;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 1;
		break;
	case 2:
		bitGraph.horizDens = 120;
		bitGraph.vertDens = 60;
		bitGraph.adjacent = false;
		bitGraph.bytesColumn = 1;
		break;
	case 3:
		bitGraph.horizDens = 60;
		bitGraph.vertDens = 240;
		bitGraph.adjacent = false;
		bitGraph.bytesColumn = 1;
		break;
	case 4:
		bitGraph.horizDens = 80;
		bitGraph.vertDens = 60;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 1;
		break;
	case 5:
		bitGraph.horizDens = 80;
		bitGraph.vertDens = 72;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 1;
		break;
	case 6:
		bitGraph.horizDens = 90;
		bitGraph.vertDens = 60;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 1;
		break;
	case 7:
		bitGraph.horizDens = 144;
		bitGraph.vertDens = 72;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 1;
		break;
	case 32:
		bitGraph.horizDens = 60;
		bitGraph.vertDens = 180;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 3;
		break;
	case 33:
		bitGraph.horizDens = 120;
		bitGraph.vertDens = 180;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 3;
		break;
	case 38:
		bitGraph.horizDens = 90;
		bitGraph.vertDens = 180;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 3;
		break;
	case 39:
		bitGraph.horizDens = 180;
		bitGraph.vertDens = 180;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 3;
		break;
	case 40:
		bitGraph.horizDens = 360;
		bitGraph.vertDens = 180;
		bitGraph.adjacent = false;
		bitGraph.bytesColumn = 3;
		break;
	case 64:
		bitGraph.horizDens = 60;
		bitGraph.vertDens = 360;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 6;
		break;
	case 65:
		bitGraph.horizDens = 120;
		bitGraph.vertDens = 360;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 6;
		break;
	case 70:
		bitGraph.horizDens = 90;
		bitGraph.vertDens = 360;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 6;
		break;
	case 71:
		bitGraph.horizDens = 180;
		bitGraph.vertDens = 360;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 6;
		break;
	case 72:
		bitGraph.horizDens = 360;
		bitGraph.vertDens = 360;
		bitGraph.adjacent = false;
		bitGraph.bytesColumn = 6;
		break;
	case 73:
		bitGraph.horizDens = 360;
		bitGraph.vertDens = 360;
		bitGraph.adjacent = true;
		bitGraph.bytesColumn = 6;
		break;
	default:
		write_log(_T("EPSONPRINTER: Unsupported bit image density %i\n"), dens);
	}
	bitGraph.pin9 = false;
	if (pins == 9) {
		if (pin9) {
			bitGraph.pin9 = true;
			bitGraph.bytesColumn = 1;
		}	
		bitGraph.vertDens = 72;
	}
	bitGraph.remBytes = numCols * bitGraph.bytesColumn;
	bitGraph.readBytesColumn = 0;
}

static int processCommandChar(Bit8u ch)
{
	if (ESCSeen)
	{
		ESCCmd = ch;
		ESCSeen = false;
		numParam = 0;

		if (ESCCmd != 0x78)
			printCharBuffer ();

		switch (ESCCmd)
		{
		case 0x02: // Undocumented
		case 0x0e: // Select double-width printing (one line) (ESC SO)		
		case 0x0f: // Select condensed printing (ESC SI)
		case 0x23: // Cancel MSB control (ESC #)
		case 0x30: // Select 1/8-inch line spacing (ESC 0)
		case 0x31: // Select 7/72-inch line spacing (ESC 1)
		case 0x32: // Select 1/6-inch line spacing (ESC 2)
		case 0x34: // Select italic font (ESC 4)
		case 0x35: // Cancel italic font (ESC 5)
		case 0x36: // Enable printing of upper control codes (ESC 6)
		case 0x37: // Enable upper control codes (ESC 7)
		case 0x3c: // Unidirectional mode (one line) (ESC <)
		case 0x3d: // Set MSB to 0 (ESC =)
		case 0x3e: // Set MSB to 1 (ESC >)
		case 0x40: // Initialize printer (ESC @)
		case 0x45: // Select bold font (ESC E)
		case 0x46: // Cancel bold font (ESC F)
		case 0x47: // Select double-strike printing (ESC G)
		case 0x48: // Cancel double-strike printing (ESC H)
		case 0x4d: // Select 10.5-point, 12-cpi (ESC M)
		case 0x4f: // Cancel bottom margin			
		case 0x50: // Select 10.5-point, 10-cpi (ESC P)
		case 0x54: // Cancel superscript/subscript printing (ESC T)
		case 0x67: // Select 10.5-point, 15-cpi (ESC g)
		case 0x73: // Select low-speed mode (ESC s)
			neededParam = 0;
			break;
		case 0x19: // Control paper loading/ejecting (ESC EM)
		case 0x20: // Set intercharacter space (ESC SP)
		case 0x21: // Master select (ESC !)
		case 0x2b: // Set n/360-inch line spacing (ESC +)
		case 0x2d: // Turn underline on/off (ESC -)
		case 0x2f: // Select vertical tab channel (ESC /)
		case 0x33: // Set n/180-inch line spacing (ESC 3)
		case 0x41: // Set n/60-inch line spacing
		case 0x43: // Set page length in lines (ESC C)
		case 0x4a: // Advance print position vertically (ESC J n)
		case 0x4e: // Set bottom margin (ESC N)
		case 0x51: // Set right margin (ESC Q)
		case 0x52: // Select an international character set (ESC R)
		case 0x53: // Select superscript/subscript printing (ESC S)
		case 0x55: // Turn unidirectional mode on/off (ESC U)
		case 0x57: // Turn double-width printing on/off (ESC W)
		case 0x61: // Select justification (ESC a)
		case 0x6b: // Select typeface (ESC k)
		case 0x6c: // Set left margin (ESC 1)
		case 0x70: // Turn proportional mode on/off (ESC p)
		case 0x72: // Select printing color (ESC r)
		case 0x74: // Select character table (ESC t)
		case 0x77: // Turn double-height printing on/off (ESC w)
		case 0x78: // Select LQ or draft (ESC x)
			neededParam = 1;
			break;
		case 0x24: // Set absolute horizontal print position (ESC $)
		case 0x3f: // Reassign bit-image mode (ESC ?)
		case 0x4b: // Select 60-dpi graphics (ESC K)
		case 0x4c: // Select 120-dpi graphics (ESC L)
		case 0x59: // Select 120-dpi, double-speed graphics (ESC Y)
		case 0x5a: // Select 240-dpi graphics (ESC Z)
		case 0x5e: // Select 60/120-dpi, 9-pin graphics
		case 0x5c: // Set relative horizontal print position (ESC \)
		case 0x63: // Set horizontal motion index (HMI) (ESC c)
			neededParam = 2;
			break;
		case 0x2a: // Select bit image (ESC *)
		case 0x58: // Select font by pitch and point (ESC X)
			neededParam = 3;
			break;
		case 0x62: // Set vertical tabs in VFU channels (ESC b)
		case 0x42: // Set vertical tabs (ESC B)
			numVertTabs = 0;
			return true;
		case 0x44: // Set horizontal tabs (ESC D)
			numHorizTabs = 0;
			return true;
		case 0x25: // Select user-defined set (ESC %)
		case 0x26: // Define user-defined characters (ESC &)
		case 0x3a: // Copy ROM to RAM (ESC :)
			write_log(_T("User-defined characters not supported!\n"));
			return true;
		case 0x28: // Two bytes sequence
			return true;
		default:
			write_log(_T("EPSONPRINTER: Unknown command ESC %c (%02X). Unable to skip parameters.\n"), ESCCmd, ESCCmd);
			neededParam = 0;
			ESCCmd = 0;
			return true;
		}

		if (neededParam > 0)
			return true;
	}

	// Two bytes sequence
	if (ESCCmd == 0x28)
	{
		ESCCmd = 0x200 + ch;

		switch (ESCCmd)
		{
		case 0x242: // Bar code setup and print (ESC (B)
		case 0x25e: // Print data as characters (ESC (^)
			neededParam = 2;
			break;
		case 0x255: // Set unit (ESC (U)
			neededParam = 3;
			break;
		case 0x243: // Set page length in defined unit (ESC (C)
		case 0x256: // Set absolute vertical print position (ESC (V)
		case 0x276: // Set relative vertical print position (ESC (v)
			neededParam = 4;
			break;
		case 0x228: // Assign character table (ESC (t)
		case 0x22d: // Select line/score (ESC (-)
			neededParam = 5;
			break;
		case 0x263: // Set page format (ESC (c)
			neededParam = 6;
			break;
		default:
			// ESC ( commands are always followed by a "number of parameters" word parameter
			write_log(_T("EPSONPRINTER: Skipping unsupported command ESC ( %c (%02X).\n"), ESCCmd, ESCCmd);
			neededParam = 2;
			ESCCmd = 0x101;
			return true;
		}

		if (neededParam > 0)
			return true;
	}

	// Ignore VFU channel setting
	if (ESCCmd == 0x62)
	{
		ESCCmd = 0x42;
		return true;
	}

	// Collect vertical tabs
	if (ESCCmd == 0x42) 
	{
		if (ch == 0 || (numVertTabs>0 && verttabs[numVertTabs-1] > (Real64)ch*lineSpacing)) // Done
			ESCCmd = 0;
		else
			if (numVertTabs < 16)
				verttabs[numVertTabs++] = (Real64)ch*lineSpacing;
	}

	// Collect horizontal tabs
	if (ESCCmd == 0x44) 
	{
		if (ch == 0 || (numHorizTabs>0 && horiztabs[numHorizTabs-1] > (Real64)ch*(1/(Real64)cpi))) // Done
			ESCCmd = 0;
		else
			if (numHorizTabs < 32)
				horiztabs[numHorizTabs++] = (Real64)ch*(1/(Real64)cpi);
	}

	if (numParam < neededParam)
	{
		params[numParam++] = ch;

		if (numParam < neededParam)
			return true;
	}

	if (ESCCmd != 0)
	{
		switch (ESCCmd)
		{
		case 0x02: // Undocumented
			// Ignore
			break;
		case 0x0e: // Select double-width printing (one line) (ESC SO)		
			if (!multipoint)
			{
				hmi = -1;
				style |= STYLE_DOUBLEWIDTHONELINE;
				updateFont();
			}
			break;
		case 0x0f: // Select condensed printing (ESC SI)
			if (!multipoint)
			{
				hmi = -1;
				style |= STYLE_CONDENSED;
				updateFont();
			}
			break;
		case 0x19: // Control paper loading/ejecting (ESC EM)
			// We are not really loading paper, so most commands can be ignored
			if (params[0] == 'R')
				newPage(true);
			break;
		case 0x20: // Set intercharacter space (ESC SP)
			if (!multipoint)
			{
				extraIntraSpace = (Real64)params[0] / (printQuality==QUALITY_DRAFT?120:180);
				hmi = -1;
				updateFont();
			}
			break;
		case 0x21: // Master select (ESC !)
			cpi = params[0] & 0x01 ? 12:10;

			// Reset first seven bits
			style &= 0xFF80;
			if (params[0] & 0x02)
				style |= STYLE_PROP;
			if (params[0] & 0x04)
				style |= STYLE_CONDENSED;
			if (params[0] & 0x08)
				style |= STYLE_BOLD;
			if (params[0] & 0x10)
				style |= STYLE_DOUBLESTRIKE;
			if (params[0] & 0x20)
				style |= STYLE_DOUBLEWIDTH;
			if (params[0] & 0x40)
				style |= STYLE_ITALICS;
			if (params[0] & 0x80)
			{
				score = SCORE_SINGLE;
				style |= STYLE_UNDERLINE;
			}

			hmi = -1;
			multipoint = false;
			updateFont();
			break;
		case 0x23: // Cancel MSB control (ESC #)
			msb = 255;
			break;
		case 0x24: // Set absolute horizontal print position (ESC $)
			{
				Real64 newX;
				Real64 unitSize = definedUnit;
				if (unitSize < 0)
					unitSize = (Real64)60.0;

				newX = leftMargin + ((Real64)PARAM16(0)/unitSize);
				if (newX <= rightMargin)
					curX = newX;
			}
			break;
		case 0x2a: // Select bit image (ESC *)
			setupBitImage(params[0], PARAM16(1), false);
			break;
		case 0x2b: // Set n/360-inch line spacing (ESC +)
			lineSpacing = (Real64)params[0]/360;
			break;
		case 0x2d: // Turn underline on/off (ESC -)
			if (params[0] == 0 || params[0] == 48)
				style &= 0xFFFF - STYLE_UNDERLINE;
			if (params[0] == 1 || params[0] == 49)
			{
				style |= STYLE_UNDERLINE;
				score = SCORE_SINGLE;
			}
			updateFont();
			break;
		case 0x2f: // Select vertical tab channel (ESC /)
			// Ignore
			break;
		case 0x30: // Select 1/8-inch line spacing (ESC 0)
			lineSpacing = (Real64)1/8;
			break;
		case 0x31: // Select 7/72-inch line spacing (ESC 1) 9-pin ONLY
			lineSpacing = (Real64)7/72;
			break;
		case 0x32: // Select 1/6-inch line spacing (ESC 2)
			lineSpacing = (Real64)1/6;
			break;
		case 0x33: // Set n/180-inch line spacing (ESC 3)
			lineSpacing = (Real64)params[0]/180;
			break;
		case 0x34: // Select italic font (ESC 4)
			style |= STYLE_ITALICS;
			updateFont();
			break;
		case 0x35: // Cancel italic font (ESC 5)
			style &= 0xFFFF - STYLE_ITALICS;
			updateFont();
			break;
		case 0x36: // Enable printing of upper control codes (ESC 6)
			printUpperContr = true;
			break;
		case 0x37: // Enable upper control codes (ESC 7)
			printUpperContr = false;
			break;
		case 0x3c: // Unidirectional mode (one line) (ESC <)
			// We don't have a print head, so just ignore this
			break;
		case 0x3d: // Set MSB to 0 (ESC =)
			msb = 0;
			break;
		case 0x3e: // Set MSB to 1 (ESC >)
			msb = 1;
			break;
		case 0x3f: // Reassign bit-image mode (ESC ?)
			if (params[0] == 75)
				densk = params[1];
			if (params[0] == 76)
				densl = params[1];
			if (params[0] == 89)
				densy = params[1];
			if (params[0] == 90)
				densz = params[1];
			break;
		case 0x40: // Initialize printer (ESC @)
			initPrinter();
			break;
		case 0x41: // Set n/60-inch line spacing
			lineSpacing = (Real64)params[0]/60;
			break;
		case 0x43: // Set page length in lines (ESC C)
			if (params[0] != 0)
				pageHeight = bottomMargin = (Real64)params[0] * lineSpacing;
			else // == 0 => Set page length in inches
			{
				neededParam = 1;
				numParam = 0;
				ESCCmd = 0x100;
				return true;
			}
			break;
		case 0x45: // Select bold font (ESC E)
			style |= STYLE_BOLD;
			updateFont();
			break;
		case 0x46: // Cancel bold font (ESC F)
			style &= 0xFFFF - STYLE_BOLD;
			updateFont();
			break;
		case 0x47: // Select dobule-strike printing (ESC G)
			style |= STYLE_DOUBLESTRIKE;
			break;
		case 0x48: // Cancel double-strike printing (ESC H)
			style &= 0xFFFF - STYLE_DOUBLESTRIKE;
			break;
		case 0x4a: // Advance print position vertically (ESC J n)
			curY += (Real64)((Real64)params[0] / (pins == 9 ? 216 : 180));
			if (curY > bottomMargin)
				newPage(true);
			break;
		case 0x4b: // Select 60-dpi graphics (ESC K)
			setupBitImage(densk, PARAM16(0), false);
			break;
		case 0x4c: // Select 120-dpi graphics (ESC L)
			setupBitImage(densl, PARAM16(0), false);
			break;
		case 0x4d: // Select 10.5-point, 12-cpi (ESC M)
			cpi = 12;
			hmi = -1;
			multipoint = false;
			updateFont();
			break;
		case 0x4e: // Set bottom margin (ESC N)
			topMargin = 0.0;
			bottomMargin = (Real64)params[0] * lineSpacing; 
			break;
		case 0x4f: // Cancel bottom (and top) margin
			topMargin = 0.0;
			bottomMargin = pageHeight;
			break;
		case 0x50: // Select 10.5-point, 10-cpi (ESC P)
			cpi = 10;
			hmi = -1;
			multipoint = false;
			updateFont();
			break;
		case 0x51: // Set right margin
			rightMargin = (Real64)(params[0]-1.0) / (Real64)cpi;
			if (rightMargin < 0)
				rightMargin = 0;
			if (rightMargin < leftMargin)
				rightMargin = leftMargin;
			break;
		case 0x52: // Select an international character set (ESC R)
			if (params[0] <= 13 || params[0] == 64)
			{
				if (params[0] == 64)
					params[0] = 14;

				curMap[0x23] = intCharSets[params[0]][0];
				curMap[0x24] = intCharSets[params[0]][1];
				curMap[0x40] = intCharSets[params[0]][2];
				curMap[0x5b] = intCharSets[params[0]][3];
				curMap[0x5c] = intCharSets[params[0]][4];
				curMap[0x5d] = intCharSets[params[0]][5];
				curMap[0x5e] = intCharSets[params[0]][6];
				curMap[0x60] = intCharSets[params[0]][7];
				curMap[0x7b] = intCharSets[params[0]][8];
				curMap[0x7c] = intCharSets[params[0]][9];
				curMap[0x7d] = intCharSets[params[0]][10];
				curMap[0x7e] = intCharSets[params[0]][11];
			}
			break;
		case 0x53: // Select superscript/subscript printing (ESC S)
			if (params[0] == 0 || params[0] == 48)
				style |= STYLE_SUBSCRIPT;
			if (params[0] == 1 || params[1] == 49)
				style |= STYLE_SUPERSCRIPT;
			updateFont();
			break;
		case 0x54: // Cancel superscript/subscript printing (ESC T)
			style &= 0xFFFF - STYLE_SUPERSCRIPT - STYLE_SUBSCRIPT;
			updateFont();
			break;
		case 0x55: // Turn unidirectional mode on/off (ESC U)
			// We don't have a print head, so just ignore this
			break;
		case 0x57: // Turn double-width printing on/off (ESC W)
			if (!multipoint)
			{
				hmi = -1;
				if (params[0] == 0 || params[0] == 48)
					style &= 0xFFFF - STYLE_DOUBLEWIDTH;
				if (params[0] == 1 || params[0] == 49)
					style |= STYLE_DOUBLEWIDTH;
				updateFont();
			}
			break;
		case 0x58: // Select font by pitch and point (ESC X)
			multipoint = true;
			// Copy currently non-multipoint CPI if no value was set so far
			if (multicpi == 0)
				multicpi = cpi;
			if (params[0] > 0)  // Set CPI
			{
				if (params[0] == 1) // Proportional spacing
					style |= STYLE_PROP;
				else if (params[0] >= 5)
					multicpi = (Real64)360 / (Real64)params[0];
			}
			if (multiPointSize == 0)
				multiPointSize = (Real64)10.5;
			if (PARAM16(1) > 0) // Set points
				multiPointSize = ((Real64)PARAM16(1)) / 2;			
			updateFont();
			break;
		case 0x59: // Select 120-dpi, double-speed graphics (ESC Y)
			setupBitImage(densy, PARAM16(0), false);
			break;
		case 0x5a: // Select 240-dpi graphics (ESC Z)
			setupBitImage(densz, PARAM16(0), false);
			break;
		case 0x5e: // Select 60/120-dpi, 9-pin graphics
			setupBitImage(densy, PARAM16(0), true);
			break;
		case 0x5c: // Set relative horizontal print position (ESC \)
			{
				Bit16s toMove = PARAM16(0);
				Real64 unitSize = definedUnit;
				if (unitSize < 0)
					unitSize = (Real64)(printQuality==QUALITY_DRAFT?120.0:180.0);
				curX += (Real64)((Real64)toMove / unitSize);
			}
			break;
		case 0x61: // Select justification (ESC a)
			printCharBuffer ();
			justification = JUST_LEFT;
			if (params[0] == 1 || params[0] == 31)
				justification = JUST_CENTER;
			if (params[0] == 2 || params[0] == 32)
				justification = JUST_RIGHT;
			if (params[0] == 3 || params[0] == 33)
				justification = JUST_FULL;
			break;
		case 0x63: // Set horizontal motion index (HMI) (ESC c)
			hmi = (Real64)PARAM16(0) / (Real64)360.0;
			extraIntraSpace = 0.0;
			break;
		case 0x67: // Select 10.5-point, 15-cpi (ESC g)
			cpi = 15;
			hmi = -1;
			multipoint = false;
			updateFont();
			break;
		case 0x6b: // Select typeface (ESC k)
			if (params[0] <= 11 || params[0] == 30 || params[0] == 31) 
				LQtypeFace = (Typeface)params[0];
			updateFont();
			break;
		case 0x6c: // Set left margin (ESC 1)
			leftMargin =  (Real64)(params[0]-1.0) / (Real64)cpi;
			if (leftMargin < 0)
				leftMargin = 0;
			if (curX < leftMargin)
				curX = leftMargin;
			break;
		case 0x70: // Turn proportional mode on/off (ESC p)
			if (params[0] == 0 || params[0] == 48)
				style &= (0xffff - STYLE_PROP);
			if (params[0] == 1 || params[0] == 49)
			{
				style |= STYLE_PROP;
				printQuality = QUALITY_LQ;
			}
			multipoint = false;
			hmi = -1;
			updateFont();
			break;
		case 0x72: // Select printing color (ESC r)
			printColor = params[0];
			if (printColor > 6)
				printColor = 0;
			break;
		case 0x73: // Select low-speed mode (ESC s)
			// Ignore
			break;
		case 0x74: // Select character table (ESC t)
			if (params[0] < 4)
				curCharTable = params[0];
			if (params[0] >= 48 && params[0] <= 51)
				curCharTable = params[0] - 48;
			selectCodepage(charTables[curCharTable]);
			updateFont();
			break;
		case 0x77: // Turn double-height printing on/off (ESC w)
			if (!multipoint)
			{
				if (params[0] == 0 || params[0] == 48)
					style &= 0xFFFF - STYLE_DOUBLEHEIGHT;
				if (params[0] == 1 || params[0] == 49)
					style |= STYLE_DOUBLEHEIGHT;
				updateFont();
			}
			break;
		case 0x78: // Select LQ or draft (ESC x)
			if (params[0] == 0 || params[0] == 48)
				printQuality = QUALITY_DRAFT;
			if (params[0] == 1 || params[0] == 49)
				printQuality = QUALITY_LQ;
			break;
		case 0x100: // Set page length in inches (ESC C NUL)
			pageHeight = (Real64)params[0];
			bottomMargin = pageHeight;
			topMargin = 0.0;
			break;
		case 0x101: // Skip unsupported ESC ( command
			neededParam = PARAM16(0);
			numParam = 0;
			break;
		case 0x228: // Assign character table (ESC (t)
			if (params[2] < 4 && params[3] < 16)
			{
				charTables[params[2]] = codepages[params[3]];
				if (params[2] == curCharTable)
					selectCodepage(charTables[curCharTable]);
			}
			break;
		case 0x22d: // Select line/score (ESC (-) 
			style &= 0xFFFF - STYLE_UNDERLINE - STYLE_STRIKETHROUGH - STYLE_OVERSCORE;
			score = params[4];
			if (score)
			{
				if (params[3] == 1)
					style |= STYLE_UNDERLINE;
				if (params[3] == 2)
					style |= STYLE_STRIKETHROUGH;
				if (params[3] == 3)
					style |= STYLE_OVERSCORE;
			}
			updateFont();
			break;
		case 0x242: // Bar code setup and print (ESC (B)
			write_log(_T("EPSONPRINTER: Barcode printing not supported\n"));
			// Find out how many bytes to skip
			neededParam = PARAM16(0);
			numParam = 0;
			break;
		case 0x243: // Set page length in defined unit (ESC (C)
			if (params[0] != 0 && definedUnit > 0)
			{
				pageHeight = bottomMargin = ((Real64)PARAM16(2)) * definedUnit;
				topMargin = 0.0;
			}
			break;
		case 0x255: // Set unit (ESC (U)
			definedUnit = (Real64)3600 / (Real64)params[2];
			break;
		case 0x256: // Set absolute vertical print position (ESC (V)
			{
				Real64 unitSize = definedUnit;
				Real64 newPos;
				if (unitSize < 0)
					unitSize = (Real64)360.0;
				newPos = topMargin + (((Real64)PARAM16(2)) * unitSize);
				if (newPos > bottomMargin)
					newPage(true);
				else
					curY = newPos;
			}
			break;
		case 0x25e: // Print data as characters (ESC (^)
			numPrintAsChar = PARAM16(0);
			break;
		case 0x263: // Set page format (ESC (c)
			if (definedUnit > 0)
			{
				topMargin = ((Real64)PARAM16(2)) * definedUnit;
				bottomMargin = ((Real64)PARAM16(4)) * definedUnit;
			}
			break;
		case 0x276: // Set relative vertical print position (ESC (v)
			{
				Real64 unitSize = definedUnit;
				Real64 newPos;
				if (unitSize < 0)
					unitSize = (Real64)360.0;
				newPos = curY + ((Real64)((Bit16s)PARAM16(2)) * unitSize);
				if (newPos > topMargin)
				{
					if (newPos > bottomMargin)
						newPage(true);
					else
						curY = newPos;	
				}
			}
			break;
		default:
			if (ESCCmd < 0x100)
				write_log(_T("EPSONPRINTER: Skipped unsupported command ESC %c (%02X)\n"), ESCCmd, ESCCmd);
			else
				write_log(_T("EPSONPRINTER: Skipped unsupported command ESC ( %c (%02X)\n"), ESCCmd-0x200, ESCCmd-0x200);
		}

		ESCCmd = 0;
		return true;
	}

	switch (ch)
	{
	case 0x07:  // Beeper (BEL)
		// BEEEP!
		return true;
	case 0x08:	// Backspace (BS)
		{
			Real64 newX = curX - (1/(Real64)actcpi);
			if (hmi > 0)
				newX = curX - hmi;
			if (newX >= leftMargin)
				curX = newX;
		}
		return true;
	case 0x09:	// Tab horizontally (HT)
		{
			// Find tab right to current pos
			Real64 moveTo = -1;
			Bit8u i;
			for (i=0; i<numHorizTabs; i++)
				if (horiztabs[i] > curX)
					moveTo = horiztabs[i];
			// Nothing found => Ignore
			if (moveTo > 0 && moveTo < rightMargin)
				curX = moveTo;
		}
		return true;
	case 0x0b:	// Tab vertically (VT)
		if (numVertTabs == 0) // All tabs cancelled => Act like CR
			curX = leftMargin;
		else if (numVertTabs == 255) // No tabs set since reset => Act like LF
		{
			curX = leftMargin;
			curY += lineSpacing;
			if (curY > bottomMargin)
				newPage(true);
		}
		else
		{
			// Find tab below current pos
			Real64 moveTo = -1;
			Bit8u i;
			for (i=0; i<numVertTabs; i++)
				if (verttabs[i] > curY)
					moveTo = verttabs[i];

			// Nothing found => Act like FF
			if (moveTo > bottomMargin || moveTo < 0)
				newPage(true);
			else
				curY = moveTo;
		}
		if (style & STYLE_DOUBLEWIDTHONELINE)
		{
			style &= 0xFFFF - STYLE_DOUBLEWIDTHONELINE;
			updateFont();
		}
		return true;
	case 0x0c:		// Form feed (FF)
		printCharBuffer ();
		if (style & STYLE_DOUBLEWIDTHONELINE)
		{
			style &= 0xFFFF - STYLE_DOUBLEWIDTHONELINE;
			updateFont();
		}
		newPage(true);
		return true;
	case 0x0d:		// Carriage Return (CR)
		printCharBuffer ();
		curX = leftMargin;
		if (!autoFeed)
			return true;
	case 0x0a:		// Line feed
		printCharBuffer ();
		if (style & STYLE_DOUBLEWIDTHONELINE)
		{
			style &= 0xFFFF - STYLE_DOUBLEWIDTHONELINE;
			updateFont();
		}
		curX = leftMargin;
		curY += lineSpacing;
		if (curY > bottomMargin)
			newPage(true);
		return true;
	case 0x0e:		//Select Real64-width printing (one line) (SO)
		if (!multipoint)
		{
			hmi = -1;
			style |= STYLE_DOUBLEWIDTHONELINE;
			updateFont();
		}
		return true;
	case 0x0f:		// Select condensed printing (SI)
		if (!multipoint)
		{
			hmi = -1;
			style |= STYLE_CONDENSED;
			updateFont();
		}
		return true;
	case 0x11:		// Select printer (DC1)
		// Ignore
		return true;
	case 0x12:		// Cancel condensed printing (DC2)
		hmi = -1;
		style &= 0xFFFF - STYLE_CONDENSED;
		updateFont();
		return true;
	case 0x13:		// Deselect printer (DC3)
		// Ignore
		return true;
	case 0x14:		// Cancel double-width printing (one line) (DC4)
		hmi = -1;
		style &= 0xFFFF - STYLE_DOUBLEWIDTHONELINE;
		updateFont();
		return true;
	case 0x18:		// Cancel line (CAN)
		return true;
	case 0x1b:		// ESC
		ESCSeen = true;
		return true;
	default:
		return false;
	}

	return false;
}


static void printBitGraph(Bit8u ch)
{
	Bitu i;
	Bitu pixsizeX, pixsizeY;
	Real64 oldY = curY;

	bitGraph.column[bitGraph.readBytesColumn++] = ch;
	bitGraph.remBytes--;

	// Only print after reading a full column
	if (bitGraph.readBytesColumn < bitGraph.bytesColumn)
		return;

	// When page dpi is greater than graphics dpi, the drawn pixels get "bigger"
	pixsizeX = dpiX/bitGraph.horizDens > 0?dpiX/bitGraph.horizDens:1;
	pixsizeY = dpiY/bitGraph.vertDens > 0?dpiY/bitGraph.vertDens:1;

	for (i=0; i<bitGraph.bytesColumn; i++)
	{
		Bits j;
		for (j=7; j>=0; j--)
		{
			Bit8u pixel = (bitGraph.column[i] >> j) & 0x01;
			if (bitGraph.pin9 && i == 1 && j == 7)
				pixel = bitGraph.column[i] & 0x01;

			if (pixel != 0)
			{
				Bitu xx;
				for (xx=0; xx<pixsizeX; xx++) {
					Bitu yy;
					for (yy=0; yy<pixsizeY; yy++)
						if (((PIXX + xx) < page_w) && ((PIXY + yy) < page_h)) {
							*((Bit8u*)page + PIXX + xx + (PIXY+yy)*page_pitch) = 255;
							(*((Bit8u*)cpage + PIXX + xx + (PIXY+yy)*page_pitch)) |= 1 << printColor;
						}
				}
			}

			curY += (Real64)1/(Real64)bitGraph.vertDens;

			if (bitGraph.pin9 && i == 1 && j == 7)
				break;
		}
	}

	curY = oldY;

	bitGraph.readBytesColumn = 0;

	// Advance to the left
	if (bitGraph.adjacent == false)
		curX += (Real64)0.5/(Real64)bitGraph.horizDens;
	else
		curX += (Real64)1/(Real64)bitGraph.horizDens;

	if (printColor)
		colorPrinted = true;
}

static void blitGlyph(uae_u8 *gbitmap, int width, int rows, int pitch, int destx, int desty, int add)
{
	int y, x;
	for (y=0; y<rows; y++)
	{
		for (x=0; x<width; x++)
		{
			// Read pixel from glyph bitmap
			Bit8u* source = gbitmap + x + y*pitch;

			// Ignore background and don't go over the border
			if (*source != 0 && (destx+x < page_w) && (desty+y < page_h) && (destx+x >= 0) && (desty+y >= 0))
			{
				Bit8u* target = (Bit8u*)page + (x+destx) + (y+desty)*page_pitch;
				Bit8u* ctarget = (Bit8u*)cpage + (x+destx) + (y+desty)*page_pitch;
				Bit8u b = *source;
				if (b >= 64) {
					b = 255;
				} else if (b == 0) {
					b = 0;
				} else {
					b = (b << 2) | (b >> 4);
				}
				if (add)
				{
					if (*target + (unsigned int)b > 255)
						*target = 255;
					else
						*target += b;
				}
				else
					*target = b;
				*ctarget |= 1 << printColor;

			}
		}
	}
	if (printColor)
		colorPrinted = true;
}

static void drawLine(int fromx, int tox, int y, int broken)
{
	int x;

	int breakmod = dpiX / 15;
	int gapstart = (breakmod * 4) / 5;

	// Draw anti-aliased line
	for (x=fromx; x<=tox; x++)
	{
		// Skip parts if broken line or going over the border
		if ((!broken || (x%breakmod <= gapstart)) && (x < page_w) && (x >= 0))
		{
			if (y < 0)
				continue;
			if (y > 0 && (y-1) < page_h) {
				*((Bit8u*)page + x + (y-1)*page_pitch) = 120;
				*((Bit8u*)cpage + x + (y-1)*page_pitch) |= 1 << printColor;
			}
			if (y < page_h) {
				*((Bit8u*)page + x + y*page_pitch) = !broken?255:120;
				*((Bit8u*)cpage + x + y*page_pitch) |= 1 << printColor;
			}
			if (y+1 < page_h) {
				*((Bit8u*)page + x + (y+1)*page_pitch) = 120;
				*((Bit8u*)cpage + x + (y+1)*page_pitch) |= 1 << printColor;
			}
		}
	}
	if (printColor)
		colorPrinted = true;
}

static void printSingleChar(Bit8u ch, int doprint)
{
	int penX = PIXX;
	int penY = PIXY;
	Bit16u lineStart;

	int bitmap_left = 0;
	int bitmap_top = 0;
	int ascender = 0;
	int width = 0;
	int rows = 0;
	int height = 0;
	int pitch = 0;
	int advancex = 0;
	uae_u8 *gbitmap = NULL;
#ifndef WINFONT
	// Do not print if no font is available
	if (!curFont)
		return;

	bitmap_left =  curFont->glyph->bitmap_left;
	bitmap_top = curFont->glyph->bitmap_top;
	ascender = curFont->size->metrics.ascender / 64;
	rows = curFont->glyph->bitmap.rows;
	advancex = curFont->glyph->advance.x / 64;

	// Find the glyph for the char to render
	index = FT_Get_Char_Index(curFont, curMap[ch]);

	// Load the glyph 
	FT_Load_Glyph(curFont, index, FT_LOAD_DEFAULT);

	// Render a high-quality bitmap
	FT_Render_Glyph(curFont->glyph, FT_RENDER_MODE_NORMAL);
	gbitmap = curFont->glyph->bitmap;
#else
	if (!selectfont (style))
		return;
	MAT2 m2 = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
	GLYPHMETRICS metrics;
	int bufsize = GetGlyphOutline (memHDC, curMap[ch], GGO_GRAY8_BITMAP, &metrics, 0, NULL, &m2); 
	if (bufsize >= 0) {
		if (bufsize == 0)
			bufsize = 4;
		gbitmap = xcalloc (uae_u8, bufsize);
		GetGlyphOutline (memHDC, curMap[ch], GGO_GRAY8_BITMAP, &metrics, bufsize, gbitmap, &m2); 

		bitmap_left = metrics.gmptGlyphOrigin.x;
		bitmap_top = metrics.gmptGlyphOrigin.y;
		width = metrics.gmBlackBoxX;
		rows = metrics.gmBlackBoxY;
		advancex = metrics.gmCellIncX;
		height = otm->otmTextMetrics.tmHeight;
		ascender = otm->otmAscent;
		pitch = (width + 3) & ~3;
	}
#endif

	int deltaY = 0;
	if (style & STYLE_SUBSCRIPT)
		deltaY = rows / 2;
	else if (style & STYLE_SUPERSCRIPT)
		deltaY = rows / 2;

	if (gbitmap) {
		penX = PIXX + bitmap_left;
		penY = PIXY - bitmap_top + ascender + deltaY;

		if (doprint) {
			// Copy bitmap into page
			blitGlyph(gbitmap, width, rows, pitch, penX, penY, false);

			// Doublestrike => Print the glyph a second time one pixel below
			if (style & STYLE_DOUBLESTRIKE)
				blitGlyph(gbitmap, width, rows, pitch, penX, penY+1, true);

			// Bold => Print the glyph a second time one pixel to the right
			if (style & STYLE_BOLD) {
				printColor = 0;
				blitGlyph(gbitmap, width, rows, pitch, penX+1, penY, true);
			}
		}
	}

	// For line printing
	lineStart = PIXX;

	if (style & STYLE_PROP)
		curX += (Real64)((Real64)(advancex)/(Real64)(dpiX));
	else
	{
		if (hmi < 0)
			curX += 1.0/(Real64)actcpi;
		else
			curX += hmi;
	}

	curX += extraIntraSpace;

	// Draw lines if desired
	if (doprint && score != SCORE_NONE && (style & (STYLE_UNDERLINE|STYLE_STRIKETHROUGH|STYLE_OVERSCORE)))
	{
		// Find out where to put the line
		Bit16u lineY = PIXY + deltaY;
		Bit16u xEnd = lineStart + advancex;

		if (style & STYLE_UNDERLINE)
			lineY = PIXY + deltaY + ascender * 100 / 70;
		if (style & STYLE_STRIKETHROUGH)
			lineY = PIXY + deltaY + ascender / 2;
		if (style & STYLE_OVERSCORE)
			lineY = PIXY + deltaY - otm->otmTextMetrics.tmDescent - ((score == SCORE_DOUBLE || score == SCORE_DOUBLEBROKEN) ? 5 : 0);

		drawLine(lineStart, xEnd, lineY, score == SCORE_SINGLEBROKEN || score == SCORE_DOUBLEBROKEN);
		if (score == SCORE_DOUBLE || score == SCORE_DOUBLEBROKEN)
			drawLine(lineStart, xEnd, lineY + 5, score == SCORE_SINGLEBROKEN || score == SCORE_DOUBLEBROKEN);
	}
#ifdef WINFONT
	xfree (gbitmap);
#endif
}

static void printCharBuffer(void)
{
	if (justification != JUST_LEFT) {
		curX = 0;
		for (int i = 0; i < charcnt; i++) {
			printSingleChar (charbuffer[i], false);
		}
		switch (justification)
		{
		case JUST_RIGHT:
			curX = rightMargin - curX;
		break;
		case JUST_CENTER:
			curX = leftMargin + (rightMargin - leftMargin) / 2  - curX / 2;
		break;
		case JUST_FULL:
		{
			float width = rightMargin - leftMargin;
			if (curX > width * 50 / 100) {
				float extraw = (width - curX) / charcnt;
				curX = leftMargin;
				for (int i = 0; i < charcnt; i++) {
					printSingleChar (charbuffer[i], true);
					curX += extraw;
				}
				charcnt = 0;
				return;
			} else {
				curX = leftMargin;
			}
		}
		break;
		default:
			curX = leftMargin;
		break;
		}
	}
	for (int i = 0; i < charcnt; i++) {
		printSingleChar(charbuffer[i], true);
	}
	charcnt = 0;
}

static void printChar(Bit8u ch)
{
#ifndef WINFONT
	FT_UInt index;
#endif

	charRead = true;

	if (page == NULL)
		return;

	// Don't think that DOS programs uses this but well: Apply MSB if desired
	if (msb != 255)
	{
		if (msb == 0)
			ch &= 0x7F;
		if (msb == 1)
			ch |= 0x80;
	}

	// Are we currently printing a bit graphic?
	if (bitGraph.remBytes > 0)
	{
		printBitGraph(ch);
		return;
	}

	// Print everything?
	if (numPrintAsChar > 0) {
		numPrintAsChar--;
	} else if (processCommandChar(ch)) {
		printCharBuffer ();
		return;
	}

	charbuffer[charcnt++] = ch;
	if (charcnt >= CHARBUFFERSIZE) {
		printCharBuffer ();
	}
}

static int isBlank(void)
{
	Bit16u x, y;
	int blank = true;

	for (y=0; y<page_h; y++)
		for (x=0; x<page_w; x++)
			if (*((Bit8u*)page + x + (y*page_pitch)) != 0)
				blank = false;

	return blank;
}

static int epson_ft (void)
{
#ifndef WINFONT
	if (!ft)
		ft = WIN32_LoadLibrary (_T("freetype6.dll"));
	if (!ft) {
		write_log (_T("EPSONPRINTER: freetype6.dll not found. Text output disabled."));
		return 0;
	}
#endif
	return 1;
}

#ifdef DEBUGPRINT
#include "zfile.h"
static int printed;
#endif

void epson_printchar(uae_u8 c)
{
#ifdef DEBUGPRINT
	if (printed)
		return;
	printed = 1;
	struct zfile *zf = zfile_fopen (DEBUGPRINT, _T("rb"), ZFD_ALL);
	for (;;) {
		int v = zfile_getc (zf);
		if (v < 0)
			break;
		printChar (v);
	}
	zfile_fclose (zf);
#else
	printChar (c);
#endif
}
int epson_init(const TCHAR *printername, int type)
{
	if (type == PARALLEL_MATRIX_EPSON9)
		pins = 9;
	else
		pins = 48;
	epson_ft ();
	write_log (_T("EPSONPRINTER%d: start\n"), pins);
	return printer_init(600, 83, 117, pngprint ? NULL : printername, 0, pins);
}
void epson_close(void)
{
	if (page && !isBlank ()) {
		outputPage();
	}
	printer_close();
#ifdef DEBUGPRINT
	printed = 0;
#endif
}
