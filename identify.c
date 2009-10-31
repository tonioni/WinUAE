/*
* UAE - The Un*x Amiga Emulator
*
* Routines for labelling amiga internals.
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef DEBUGGER

#include "memory.h"
#include "identify.h"

const struct mem_labels int_labels[] =
{
	{ L"Reset:SSP",  0x0000 },
	{ L"EXECBASE",   0x0004 },
	{ L"BUS ERROR",  0x0008 },
	{ L"ADR ERROR",  0x000C },
	{ L"ILLEG OPC",  0x0010 },
	{ L"DIV BY 0",   0x0014 },
	{ L"CHK",        0x0018 },
	{ L"TRAPV",      0x001C },
	{ L"PRIVIL VIO", 0x0020 },
	{ L"TRACE",      0x0024 },
	{ L"LINEA EMU",  0x0028 },
	{ L"LINEF EMU",  0x002C },
	{ L"INT Uninit", 0x003C },
	{ L"INT Unjust", 0x0060 },
	{ L"Lvl 1 Int",  0x0064 },
	{ L"Lvl 2 Int",  0x0068 },
	{ L"Lvl 3 Int",  0x006C },
	{ L"Lvl 4 Int",  0x0070 },
	{ L"Lvl 5 Int",  0x0074 },
	{ L"Lvl 6 Int",  0x0078 },
	{ L"NMI",        0x007C },
	{ 0, 0 }
};

const struct mem_labels trap_labels[] =
{
	{ L"TRAP 00",    0x0080 },
	{ L"TRAP 01",    0x0084 },
	{ L"TRAP 02",    0x0088 },
	{ L"TRAP 03",    0x008C },
	{ L"TRAP 04",    0x0090 },
	{ L"TRAP 05",    0x0094 },
	{ L"TRAP 06",    0x0098 },
	{ L"TRAP 07",    0x009C },
	{ L"TRAP 08",    0x00A0 },
	{ L"TRAP 09",    0x00A4 },
	{ L"TRAP 10",    0x00A8 },
	{ L"TRAP 11",    0x00AC },
	{ L"TRAP 12",    0x00B0 },
	{ L"TRAP 13",    0x00B4 },
	{ L"TRAP 14",    0x00B8 },
	{ L"TRAP 15",    0x00BC },
	{ 0, 0 }
};

const struct mem_labels mem_labels[] =
{
	{ L"CIAB PRA",   0xBFD000 },
	{ L"CIAB PRB",   0xBFD100 },
	{ L"CIAB DDRA",  0xBFD200 },
	{ L"CIAB DDRB",  0xBFD300 },
	{ L"CIAB TALO",  0xBFD400 },
	{ L"CIAB TAHI",  0xBFD500 },
	{ L"CIAB TBLO",  0xBFD600 },
	{ L"CIAB TBHI",  0xBFD700 },
	{ L"CIAB TDLO",  0xBFD800 },
	{ L"CIAB TDMD",  0xBFD900 },
	{ L"CIAB TDHI",  0xBFDA00 },
	{ L"CIAB SDR",   0xBFDC00 },
	{ L"CIAB ICR",   0xBFDD00 },
	{ L"CIAB CRA",   0xBFDE00 },
	{ L"CIAB CRB",   0xBFDF00 },
	{ L"CIAA PRA",   0xBFE001 },
	{ L"CIAA PRB",   0xBFE101 },
	{ L"CIAA DDRA",  0xBFE201 },
	{ L"CIAA DDRB",  0xBFE301 },
	{ L"CIAA TALO",  0xBFE401 },
	{ L"CIAA TAHI",  0xBFE501 },
	{ L"CIAA TBLO",  0xBFE601 },
	{ L"CIAA TBHI",  0xBFE701 },
	{ L"CIAA TDLO",  0xBFE801 },
	{ L"CIAA TDMD",  0xBFE901 },
	{ L"CIAA TDHI",  0xBFEA01 },
	{ L"CIAA SDR",   0xBFEC01 },
	{ L"CIAA ICR",   0xBFED01 },
	{ L"CIAA CRA",   0xBFEE01 },
	{ L"CIAA CRB",   0xBFEF01 },
	{ L"CLK S1",     0xDC0000 },
	{ L"CLK S10",    0xDC0004 },
	{ L"CLK MI1",    0xDC0008 },
	{ L"CLK MI10",   0xDC000C },
	{ L"CLK H1",     0xDC0010 },
	{ L"CLK H10",    0xDC0014 },
	{ L"CLK D1",     0xDC0018 },
	{ L"CLK D10",    0xDC001C },
	{ L"CLK MO1",    0xDC0020 },
	{ L"CLK MO10",   0xDC0024 },
	{ L"CLK Y1",     0xDC0028 },
	{ L"CLK Y10",    0xDC002E },
	{ L"CLK WEEK",   0xDC0030 },
	{ L"CLK CD",     0xDC0034 },
	{ L"CLK CE",     0xDC0038 },
	{ L"CLK CF",     0xDC003C },
	{ NULL, 0 }
};

/* This table was generated from the list of AGA chip names in
* AGA.guide available on aminet. It could well have errors in it. */

const struct customData custd[] =
{
#if 0
	{ L"BLTDDAT",  0xdff000 }, /* Blitter dest. early read (dummy address) */
#endif
	{ L"DMACONR",  0xdff002, 1 }, /* Dma control (and blitter status) read */
	{ L"VPOSR",    0xdff004, 1 }, /* Read vert most sig. bits (and frame flop */
	{ L"VHPOSR",   0xdff006, 1 }, /* Read vert and horiz position of beam */
#if 0
	{ L"DSKDATR",  0xdff008 }, /* Disk data early read (dummy address) */
#endif
	{ L"JOY0DAT",  0xdff00A, 1 }, /* Joystick-mouse 0 data (vert,horiz) */
	{ L"JOT1DAT",  0xdff00C, 1 }, /* Joystick-mouse 1 data (vert,horiz) */
	{ L"CLXDAT",   0xdff00E, 1 }, /* Collision data reg. (read and clear) */
	{ L"ADKCONR",  0xdff010, 1 }, /* Audio,disk control register read */
	{ L"POT0DAT",  0xdff012, 1 }, /* Pot counter pair 0 data (vert,horiz) */
	{ L"POT1DAT",  0xdff014, 1 }, /* Pot counter pair 1 data (vert,horiz) */
	{ L"POTGOR",   0xdff016, 1 }, /* Pot pin data read */
	{ L"SERDATR",  0xdff018, 1 }, /* Serial port data and status read */
	{ L"DSKBYTR",  0xdff01A, 1 }, /* Disk data byte and status read */
	{ L"INTENAR",  0xdff01C, 1 }, /* Interrupt enable bits read */
	{ L"INTREQR",  0xdff01E, 1 }, /* Interrupt request bits read */
	{ L"DSKPTH",   0xdff020, 2, 1 }, /* Disk pointer (high 5 bits) */
	{ L"DSKPTL",   0xdff022, 2, 2 }, /* Disk pointer (low 15 bits) */
	{ L"DSKLEN",   0xdff024, 2, 0 }, /* Disk lentgh */
#if 0
	{ L"DSKDAT",   0xdff026 }, /* Disk DMA data write */
	{ L"REFPTR",   0xdff028 }, /* Refresh pointer */
#endif
	{ L"VPOSW",    0xdff02A, 2, 0 }, /* Write vert most sig. bits(and frame flop) */
	{ L"VHPOSW",   0xdff02C, 2, 0 }, /* Write vert and horiz pos of beam */
	{ L"COPCON",   0xdff02e, 2, 0 }, /* Coprocessor control reg (CDANG) */
	{ L"SERDAT",   0xdff030, 2, 0 }, /* Serial port data and stop bits write */
	{ L"SERPER",   0xdff032, 2, 0 }, /* Serial port period and control */
	{ L"POTGO",    0xdff034, 2, 0 }, /* Pot count start,pot pin drive enable data */
	{ L"JOYTEST",  0xdff036, 2, 0 }, /* Write to all 4 joystick-mouse counters at once */
	{ L"STREQU",   0xdff038, 2, 0 }, /* Strobe for horiz sync with VB and EQU */
	{ L"STRVBL",   0xdff03A, 2, 0 }, /* Strobe for horiz sync with VB (vert blank) */
	{ L"STRHOR",   0xdff03C, 2, 0 }, /* Strobe for horiz sync */
	{ L"STRLONG",  0xdff03E, 2, 0 }, /* Strobe for identification of long horiz line */
	{ L"BLTCON0",  0xdff040, 2, 0 }, /* Blitter control reg 0 */
	{ L"BLTCON1",  0xdff042, 2, 0 }, /* Blitter control reg 1 */
	{ L"BLTAFWM",  0xdff044, 2, 0 }, /* Blitter first word mask for source A */
	{ L"BLTALWM",  0xdff046, 2, 0 }, /* Blitter last word mask for source A */
	{ L"BLTCPTH",  0xdff048, 2, 1 }, /* Blitter pointer to source C (high 5 bits) */
	{ L"BLTCPTL",  0xdff04A, 2, 2 }, /* Blitter pointer to source C (low 15 bits) */
	{ L"BLTBPTH",  0xdff04C, 2, 1 }, /* Blitter pointer to source B (high 5 bits) */
	{ L"BLTBPTL",  0xdff04E, 2, 2 }, /* Blitter pointer to source B (low 15 bits) */
	{ L"BLTAPTH",  0xdff050, 2, 1 }, /* Blitter pointer to source A (high 5 bits) */
	{ L"BLTAPTL",  0xdff052, 2, 2 }, /* Blitter pointer to source A (low 15 bits) */
	{ L"BPTDPTH",  0xdff054, 2, 1 }, /* Blitter pointer to destn  D (high 5 bits) */
	{ L"BLTDPTL",  0xdff056, 2, 2 }, /* Blitter pointer to destn  D (low 15 bits) */
	{ L"BLTSIZE",  0xdff058, 2, 0 }, /* Blitter start and size (win/width,height) */
	{ L"BLTCON0L", 0xdff05A, 2, 4 }, /* Blitter control 0 lower 8 bits (minterms) */
	{ L"BLTSIZV",  0xdff05C, 2, 4 }, /* Blitter V size (for 15 bit vert size) */
	{ L"BLTSIZH",  0xdff05E, 2, 4 }, /* Blitter H size & start (for 11 bit H size) */
	{ L"BLTCMOD",  0xdff060, 2, 0 }, /* Blitter modulo for source C */
	{ L"BLTBMOD",  0xdff062, 2, 0 }, /* Blitter modulo for source B */
	{ L"BLTAMOD",  0xdff064, 2, 0 }, /* Blitter modulo for source A */
	{ L"BLTDMOD",  0xdff066, 2, 0 }, /* Blitter modulo for destn  D */
#if 0
	{ L"Unknown",  0xdff068 }, /* Unknown or Unused */
	{ L"Unknown",  0xdff06a }, /* Unknown or Unused */
	{ L"Unknown",  0xdff06c }, /* Unknown or Unused */
	{ L"Unknown",  0xdff06e }, /* Unknown or Unused */
#endif
	{ L"BLTCDAT",  0xdff070, 2, 0 }, /* Blitter source C data reg */
	{ L"BLTBDAT",  0xdff072, 2, 0 }, /* Blitter source B data reg */
	{ L"BLTADAT",  0xdff074, 2, 0 }, /* Blitter source A data reg */
	{ L"BLTDDAT",  0xdff076, 2, 0 }, /* Blitter destination reg */
#if 0
	{ L"SPRHDAT",  0xdff078 }, /* Ext logic UHRES sprite pointer and data identifier */
	{ L"BPLHDAT",  0xdff07A }, /* Ext logic UHRES bit plane identifier */
#endif
	{ L"LISAID",   0xdff07C, 1, 8 }, /* Chip revision level for Denise/Lisa */
	{ L"DSKSYNC",  0xdff07E, 2 }, /* Disk sync pattern reg for disk read */
	{ L"COP1LCH",  0xdff080, 2, 1 }, /* Coprocessor first location reg (high 5 bits) */
	{ L"COP1LCL",  0xdff082, 2, 2 }, /* Coprocessor first location reg (low 15 bits) */
	{ L"COP2LCH",  0xdff084, 2, 1 }, /* Coprocessor second reg (high 5 bits) */
	{ L"COP2LCL",  0xdff086, 2, 2 }, /* Coprocessor second reg (low 15 bits) */
	{ L"COPJMP1",  0xdff088, 2 }, /* Coprocessor restart at first location */
	{ L"COPJMP2",  0xdff08A, 2 }, /* Coprocessor restart at second location */
#if 0
	{ L"COPINS",   0xdff08C }, /* Coprocessor inst fetch identify */
#endif
	{ L"DIWSTRT",  0xdff08E, 2 }, /* Display window start (upper left vert-hor pos) */
	{ L"DIWSTOP",  0xdff090, 2 }, /* Display window stop (lower right vert-hor pos) */
	{ L"DDFSTRT",  0xdff092, 2 }, /* Display bit plane data fetch start.hor pos */
	{ L"DDFSTOP",  0xdff094, 2 }, /* Display bit plane data fetch stop.hor pos */
	{ L"DMACON",   0xdff096, 2 }, /* DMA control write (clear or set) */
	{ L"CLXCON",   0xdff098, 2 }, /* Collision control */
	{ L"INTENA",   0xdff09A, 2 }, /* Interrupt enable bits (clear or set bits) */
	{ L"INTREQ",   0xdff09C, 2 }, /* Interrupt request bits (clear or set bits) */
	{ L"ADKCON",   0xdff09E, 2 }, /* Audio,disk,UART,control */
	{ L"AUD0LCH",  0xdff0A0, 2, 1 }, /* Audio channel 0 location (high 5 bits) */
	{ L"AUD0LCL",  0xdff0A2, 2, 2 }, /* Audio channel 0 location (low 15 bits) */
	{ L"AUD0LEN",  0xdff0A4, 2 }, /* Audio channel 0 lentgh */
	{ L"AUD0PER",  0xdff0A6, 2 }, /* Audio channel 0 period */
	{ L"AUD0VOL",  0xdff0A8, 2 }, /* Audio channel 0 volume */
	{ L"AUD0DAT",  0xdff0AA, 2 }, /* Audio channel 0 data */
#if 0
	{ L"Unknown",  0xdff0AC }, /* Unknown or Unused */
	{ L"Unknown",  0xdff0AE }, /* Unknown or Unused */
#endif
	{ L"AUD1LCH",  0xdff0B0, 2, 1 }, /* Audio channel 1 location (high 5 bits) */
	{ L"AUD1LCL",  0xdff0B2, 2, 2 }, /* Audio channel 1 location (low 15 bits) */
	{ L"AUD1LEN",  0xdff0B4, 2 }, /* Audio channel 1 lentgh */
	{ L"AUD1PER",  0xdff0B6, 2 }, /* Audio channel 1 period */
	{ L"AUD1VOL",  0xdff0B8, 2 }, /* Audio channel 1 volume */
	{ L"AUD1DAT",  0xdff0BA, 2 }, /* Audio channel 1 data */
#if 0
	{ L"Unknown",  0xdff0BC }, /* Unknown or Unused */
	{ L"Unknown",  0xdff0BE }, /* Unknown or Unused */
#endif
	{ L"AUD2LCH",  0xdff0C0, 2, 1 }, /* Audio channel 2 location (high 5 bits) */
	{ L"AUD2LCL",  0xdff0C2, 2, 2 }, /* Audio channel 2 location (low 15 bits) */
	{ L"AUD2LEN",  0xdff0C4, 2 }, /* Audio channel 2 lentgh */
	{ L"AUD2PER",  0xdff0C6, 2 }, /* Audio channel 2 period */
	{ L"AUD2VOL",  0xdff0C8, 2 }, /* Audio channel 2 volume */
	{ L"AUD2DAT",  0xdff0CA, 2 }, /* Audio channel 2 data */
#if 0
	{ L"Unknown",  0xdff0CC }, /* Unknown or Unused */
	{ L"Unknown",  0xdff0CE }, /* Unknown or Unused */
#endif
	{ L"AUD3LCH",  0xdff0D0, 2, 1 }, /* Audio channel 3 location (high 5 bits) */
	{ L"AUD3LCL",  0xdff0D2, 2, 2 }, /* Audio channel 3 location (low 15 bits) */
	{ L"AUD3LEN",  0xdff0D4, 2 }, /* Audio channel 3 lentgh */
	{ L"AUD3PER",  0xdff0D6, 2 }, /* Audio channel 3 period */
	{ L"AUD3VOL",  0xdff0D8, 2 }, /* Audio channel 3 volume */
	{ L"AUD3DAT",  0xdff0DA, 2 }, /* Audio channel 3 data */
#if 0
	{ L"Unknown",  0xdff0DC }, /* Unknown or Unused */
	{ L"Unknown",  0xdff0DE }, /* Unknown or Unused */
#endif
	{ L"BPL1PTH",  0xdff0E0, 2, 1 }, /* Bit plane pointer 1 (high 5 bits) */
	{ L"BPL1PTL",  0xdff0E2, 2, 2 }, /* Bit plane pointer 1 (low 15 bits) */
	{ L"BPL2PTH",  0xdff0E4, 2, 1 }, /* Bit plane pointer 2 (high 5 bits) */
	{ L"BPL2PTL",  0xdff0E6, 2, 2 }, /* Bit plane pointer 2 (low 15 bits) */
	{ L"BPL3PTH",  0xdff0E8, 2, 1 }, /* Bit plane pointer 3 (high 5 bits) */
	{ L"BPL3PTL",  0xdff0EA, 2, 2 }, /* Bit plane pointer 3 (low 15 bits) */
	{ L"BPL4PTH",  0xdff0EC, 2, 1 }, /* Bit plane pointer 4 (high 5 bits) */
	{ L"BPL4PTL",  0xdff0EE, 2, 2 }, /* Bit plane pointer 4 (low 15 bits) */
	{ L"BPL5PTH",  0xdff0F0, 2, 1 }, /* Bit plane pointer 5 (high 5 bits) */
	{ L"BPL5PTL",  0xdff0F2, 2, 2 }, /* Bit plane pointer 5 (low 15 bits) */
	{ L"BPL6PTH",  0xdff0F4, 2, 1|8 }, /* Bit plane pointer 6 (high 5 bits) */
	{ L"BPL6PTL",  0xdff0F6, 2, 2|8 }, /* Bit plane pointer 6 (low 15 bits) */
	{ L"BPL7PTH",  0xdff0F8, 2, 1|8 }, /* Bit plane pointer 7 (high 5 bits) */
	{ L"BPL7PTL",  0xdff0FA, 2, 2|8 }, /* Bit plane pointer 7 (low 15 bits) */
	{ L"BPL8PTH",  0xdff0FC, 2, 1|8 }, /* Bit plane pointer 8 (high 5 bits) */
	{ L"BPL8PTL",  0xdff0FE, 2, 2|8 }, /* Bit plane pointer 8 (low 15 bits) */
	{ L"BPLCON0",  0xdff100, 2 }, /* Bit plane control reg (misc control bits) */
	{ L"BPLCON1",  0xdff102, 2 }, /* Bit plane control reg (scroll val PF1,PF2) */
	{ L"BPLCON2",  0xdff104, 2 }, /* Bit plane control reg (priority control) */
	{ L"BPLCON3",  0xdff106, 2|8 }, /* Bit plane control reg (enhanced features) */
	{ L"BPL1MOD",  0xdff108, 2 }, /* Bit plane modulo (odd planes,or active- fetch lines if bitplane scan-doubling is enabled */
	{ L"BPL2MOD",  0xdff10A, 2 }, /* Bit plane modulo (even planes or inactive- fetch lines if bitplane scan-doubling is enabled */
	{ L"BPLCON4",  0xdff10C, 2|8 }, /* Bit plane control reg (bitplane and sprite masks) */
	{ L"CLXCON2",  0xdff10e, 2|8 }, /* Extended collision control reg */
	{ L"BPL1DAT",  0xdff110, 2 }, /* Bit plane 1 data (parallel to serial con- vert) */
	{ L"BPL2DAT",  0xdff112, 2 }, /* Bit plane 2 data (parallel to serial con- vert) */
	{ L"BPL3DAT",  0xdff114, 2 }, /* Bit plane 3 data (parallel to serial con- vert) */
	{ L"BPL4DAT",  0xdff116, 2 }, /* Bit plane 4 data (parallel to serial con- vert) */
	{ L"BPL5DAT",  0xdff118, 2 }, /* Bit plane 5 data (parallel to serial con- vert) */
	{ L"BPL6DAT",  0xdff11a, 2 }, /* Bit plane 6 data (parallel to serial con- vert) */
	{ L"BPL7DAT",  0xdff11c, 2|8 }, /* Bit plane 7 data (parallel to serial con- vert) */
	{ L"BPL8DAT",  0xdff11e, 2|8 }, /* Bit plane 8 data (parallel to serial con- vert) */
	{ L"SPR0PTH",  0xdff120, 2, 1 }, /* Sprite 0 pointer (high 5 bits) */
	{ L"SPR0PTL",  0xdff122, 2, 2 }, /* Sprite 0 pointer (low 15 bits) */
	{ L"SPR1PTH",  0xdff124, 2, 1 }, /* Sprite 1 pointer (high 5 bits) */
	{ L"SPR1PTL",  0xdff126, 2, 2 }, /* Sprite 1 pointer (low 15 bits) */
	{ L"SPR2PTH",  0xdff128, 2, 1 }, /* Sprite 2 pointer (high 5 bits) */
	{ L"SPR2PTL",  0xdff12A, 2, 2 }, /* Sprite 2 pointer (low 15 bits) */
	{ L"SPR3PTH",  0xdff12C, 2, 1 }, /* Sprite 3 pointer (high 5 bits) */
	{ L"SPR3PTL",  0xdff12E, 2, 2 }, /* Sprite 3 pointer (low 15 bits) */
	{ L"SPR4PTH",  0xdff130, 2, 1 }, /* Sprite 4 pointer (high 5 bits) */
	{ L"SPR4PTL",  0xdff132, 2, 2 }, /* Sprite 4 pointer (low 15 bits) */
	{ L"SPR5PTH",  0xdff134, 2, 1 }, /* Sprite 5 pointer (high 5 bits) */
	{ L"SPR5PTL",  0xdff136, 2, 2 }, /* Sprite 5 pointer (low 15 bits) */
	{ L"SPR6PTH",  0xdff138, 2, 1 }, /* Sprite 6 pointer (high 5 bits) */
	{ L"SPR6PTL",  0xdff13A, 2, 2 }, /* Sprite 6 pointer (low 15 bits) */
	{ L"SPR7PTH",  0xdff13C, 2, 1 }, /* Sprite 7 pointer (high 5 bits) */
	{ L"SPR7PTL",  0xdff13E, 2, 2 }, /* Sprite 7 pointer (low 15 bits) */
	{ L"SPR0POS",  0xdff140, 2 }, /* Sprite 0 vert-horiz start pos data */
	{ L"SPR0CTL",  0xdff142, 2 }, /* Sprite 0 position and control data */
	{ L"SPR0DATA", 0xdff144, 2 }, /* Sprite 0 image data register A */
	{ L"SPR0DATB", 0xdff146, 2 }, /* Sprite 0 image data register B */
	{ L"SPR1POS",  0xdff148, 2 }, /* Sprite 1 vert-horiz start pos data */
	{ L"SPR1CTL",  0xdff14A, 2 }, /* Sprite 1 position and control data */
	{ L"SPR1DATA", 0xdff14C, 2 }, /* Sprite 1 image data register A */
	{ L"SPR1DATB", 0xdff14E, 2 }, /* Sprite 1 image data register B */
	{ L"SPR2POS",  0xdff150, 2 }, /* Sprite 2 vert-horiz start pos data */
	{ L"SPR2CTL",  0xdff152, 2 }, /* Sprite 2 position and control data */
	{ L"SPR2DATA", 0xdff154, 2 }, /* Sprite 2 image data register A */
	{ L"SPR2DATB", 0xdff156, 2 }, /* Sprite 2 image data register B */
	{ L"SPR3POS",  0xdff158, 2 }, /* Sprite 3 vert-horiz start pos data */
	{ L"SPR3CTL",  0xdff15A, 2 }, /* Sprite 3 position and control data */
	{ L"SPR3DATA", 0xdff15C, 2 }, /* Sprite 3 image data register A */
	{ L"SPR3DATB", 0xdff15E, 2 }, /* Sprite 3 image data register B */
	{ L"SPR4POS",  0xdff160, 2 }, /* Sprite 4 vert-horiz start pos data */
	{ L"SPR4CTL",  0xdff162, 2 }, /* Sprite 4 position and control data */
	{ L"SPR4DATA", 0xdff164, 2 }, /* Sprite 4 image data register A */
	{ L"SPR4DATB", 0xdff166, 2 }, /* Sprite 4 image data register B */
	{ L"SPR5POS",  0xdff168, 2 }, /* Sprite 5 vert-horiz start pos data */
	{ L"SPR5CTL",  0xdff16A, 2 }, /* Sprite 5 position and control data */
	{ L"SPR5DATA", 0xdff16C, 2 }, /* Sprite 5 image data register A */
	{ L"SPR5DATB", 0xdff16E, 2 }, /* Sprite 5 image data register B */
	{ L"SPR6POS",  0xdff170, 2 }, /* Sprite 6 vert-horiz start pos data */
	{ L"SPR6CTL",  0xdff172, 2 }, /* Sprite 6 position and control data */
	{ L"SPR6DATA", 0xdff174, 2 }, /* Sprite 6 image data register A */
	{ L"SPR6DATB", 0xdff176, 2 }, /* Sprite 6 image data register B */
	{ L"SPR7POS",  0xdff178, 2 }, /* Sprite 7 vert-horiz start pos data */
	{ L"SPR7CTL",  0xdff17A, 2 }, /* Sprite 7 position and control data */
	{ L"SPR7DATA", 0xdff17C, 2 }, /* Sprite 7 image data register A */
	{ L"SPR7DATB", 0xdff17E, 2 }, /* Sprite 7 image data register B */
	{ L"COLOR00",  0xdff180, 2 }, /* Color table 00 */
	{ L"COLOR01",  0xdff182, 2 }, /* Color table 01 */
	{ L"COLOR02",  0xdff184, 2 }, /* Color table 02 */
	{ L"COLOR03",  0xdff186, 2 }, /* Color table 03 */
	{ L"COLOR04",  0xdff188, 2 }, /* Color table 04 */
	{ L"COLOR05",  0xdff18A, 2 }, /* Color table 05 */
	{ L"COLOR06",  0xdff18C, 2 }, /* Color table 06 */
	{ L"COLOR07",  0xdff18E, 2 }, /* Color table 07 */
	{ L"COLOR08",  0xdff190, 2 }, /* Color table 08 */
	{ L"COLOR09",  0xdff192, 2 }, /* Color table 09 */
	{ L"COLOR10",  0xdff194, 2 }, /* Color table 10 */
	{ L"COLOR11",  0xdff196, 2 }, /* Color table 11 */
	{ L"COLOR12",  0xdff198, 2 }, /* Color table 12 */
	{ L"COLOR13",  0xdff19A, 2 }, /* Color table 13 */
	{ L"COLOR14",  0xdff19C, 2 }, /* Color table 14 */
	{ L"COLOR15",  0xdff19E, 2 }, /* Color table 15 */
	{ L"COLOR16",  0xdff1A0, 2 }, /* Color table 16 */
	{ L"COLOR17",  0xdff1A2, 2 }, /* Color table 17 */
	{ L"COLOR18",  0xdff1A4, 2 }, /* Color table 18 */
	{ L"COLOR19",  0xdff1A6, 2 }, /* Color table 19 */
	{ L"COLOR20",  0xdff1A8, 2 }, /* Color table 20 */
	{ L"COLOR21",  0xdff1AA, 2 }, /* Color table 21 */
	{ L"COLOR22",  0xdff1AC, 2 }, /* Color table 22 */
	{ L"COLOR23",  0xdff1AE, 2 }, /* Color table 23 */
	{ L"COLOR24",  0xdff1B0, 2 }, /* Color table 24 */
	{ L"COLOR25",  0xdff1B2, 2 }, /* Color table 25 */
	{ L"COLOR26",  0xdff1B4, 2 }, /* Color table 26 */
	{ L"COLOR27",  0xdff1B6, 2 }, /* Color table 27 */
	{ L"COLOR28",  0xdff1B8, 2 }, /* Color table 28 */
	{ L"COLOR29",  0xdff1BA, 2 }, /* Color table 29 */
	{ L"COLOR30",  0xdff1BC, 2 }, /* Color table 30 */
	{ L"COLOR31",  0xdff1BE, 2 }, /* Color table 31 */
	{ L"HTOTAL",   0xdff1C0, 2|4 }, /* Highest number count in horiz line (VARBEAMEN = 1) */
	{ L"HSSTOP",   0xdff1C2, 2|4 }, /* Horiz line pos for HSYNC stop */
	{ L"HBSTRT",   0xdff1C4, 2|4 }, /* Horiz line pos for HBLANK start */
	{ L"HBSTOP",   0xdff1C6, 2|4 }, /* Horiz line pos for HBLANK stop */
	{ L"VTOTAL",   0xdff1C8, 2|4 }, /* Highest numbered vertical line (VARBEAMEN = 1) */
	{ L"VSSTOP",   0xdff1CA, 2|4 }, /* Vert line for VBLANK start */
	{ L"VBSTRT",   0xdff1CC, 2|4 }, /* Vert line for VBLANK start */
	{ L"VBSTOP",   0xdff1CE, 2|4 }, /* Vert line for VBLANK stop */
#if 0
	{ L"SPRHSTRT", 0xdff1D0 }, /* UHRES sprite vertical start */
	{ L"SPRHSTOP", 0xdff1D2 }, /* UHRES sprite vertical stop */
	{ L"BPLHSTRT", 0xdff1D4 }, /* UHRES bit plane vertical stop */
	{ L"BPLHSTOP", 0xdff1D6 }, /* UHRES bit plane vertical stop */
	{ L"HHPOSW",   0xdff1D8 }, /* DUAL mode hires H beam counter write */
	{ L"HHPOSR",   0xdff1DA }, /* DUAL mode hires H beam counter read */
#endif
	{ L"BEAMCON0", 0xdff1DC, 2|4 }, /* Beam counter control register (SHRES,UHRES,PAL) */
	{ L"HSSTRT",   0xdff1DE, 2|4 }, /* Horizontal sync start (VARHSY) */
	{ L"VSSTRT",   0xdff1E0, 2|4 }, /* Vertical sync start (VARVSY) */
	{ L"HCENTER",  0xdff1E2, 2|4 }, /* Horizontal pos for vsync on interlace */
	{ L"DIWHIGH",  0xdff1E4, 2|4 }, /* Display window upper bits for start/stop */
#if 0
	{ L"BPLHMOD",  0xdff1E6 }, /* UHRES bit plane modulo */
	{ L"SPRHPTH",  0xdff1E8 }, /* UHRES sprite pointer (high 5 bits) */
	{ L"SPRHPTL",  0xdff1EA }, /* UHRES sprite pointer (low 15 bits) */
	{ L"BPLHPTH",  0xdff1EC }, /* VRam (UHRES) bitplane pointer (hi 5 bits) */
	{ L"BPLHPTL",  0xdff1EE }, /* VRam (UHRES) bitplane pointer (lo 15 bits) */
	{ L"RESERVED", 0xdff1F0 }, /* Reserved (forever i guess!) */
	{ L"RESERVED", 0xdff1F2 }, /* Reserved (forever i guess!) */
	{ L"RESERVED", 0xdff1F4 }, /* Reserved (forever i guess!) */
	{ L"RESERVED", 0xdff1F6 }, /* Reserved (forever i guess!) */
	{ L"RESERVED", 0xdff1F8 }, /* Reserved (forever i guess!) */
	{ L"RESERVED", 0xdff1Fa }, /* Reserved (forever i guess!) */
#endif
	{ L"FMODE",    0xdff1FC, 2|8 }, /* Fetch mode register */
	{ L"NO-OP(NULL)", 0xdff1FE },   /*   Can also indicate last 2 or 3 refresh
									cycles or the restart of the COPPER after lockup.*/
	{ NULL }
};

#endif
