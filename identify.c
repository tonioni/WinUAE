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

struct mem_labels int_labels[] =
{
    { "Reset:SSP",  0x0000 },
    { "EXECBASE",   0x0004 },
    { "BUS ERROR",  0x0008 },
    { "ADR ERROR",  0x000C },
    { "ILLEG OPC",  0x0010 },
    { "DIV BY 0",   0x0014 },
    { "CHK",        0x0018 },
    { "TRAPV",      0x001C },
    { "PRIVIL VIO", 0x0020 },
    { "TRACE",      0x0024 },
    { "LINEA EMU",  0x0028 },
    { "LINEF EMU",  0x002C },
    { "INT Uninit", 0x003C },
    { "INT Unjust", 0x0060 },
    { "Lvl 1 Int",  0x0064 },
    { "Lvl 2 Int",  0x0068 },
    { "Lvl 3 Int",  0x006C },
    { "Lvl 4 Int",  0x0070 },
    { "Lvl 5 Int",  0x0074 },
    { "Lvl 6 Int",  0x0078 },
    { "NMI",        0x007C },
    { 0, 0 }
};

struct mem_labels trap_labels[] =
{
    { "TRAP 00",    0x0080 },
    { "TRAP 01",    0x0084 },
    { "TRAP 02",    0x0088 },
    { "TRAP 03",    0x008C },
    { "TRAP 04",    0x0090 },
    { "TRAP 05",    0x0094 },
    { "TRAP 06",    0x0098 },
    { "TRAP 07",    0x009C },
    { "TRAP 08",    0x00A0 },
    { "TRAP 09",    0x00A4 },
    { "TRAP 10",    0x00A8 },
    { "TRAP 11",    0x00AC },
    { "TRAP 12",    0x00B0 },
    { "TRAP 13",    0x00B4 },
    { "TRAP 14",    0x00B8 },
    { "TRAP 15",    0x00BC },
    { 0, 0 }
};

struct mem_labels mem_labels[] =
{
    { "CIAB PRA",   0xBFD000 },
    { "CIAB PRB",   0xBFD100 },
    { "CIAB DDRA",  0xBFD200 },
    { "CIAB DDRB",  0xBFD300 },
    { "CIAB TALO",  0xBFD400 },
    { "CIAB TAHI",  0xBFD500 },
    { "CIAB TBLO",  0xBFD600 },
    { "CIAB TBHI",  0xBFD700 },
    { "CIAB TDLO",  0xBFD800 },
    { "CIAB TDMD",  0xBFD900 },
    { "CIAB TDHI",  0xBFDA00 },
    { "CIAB SDR",   0xBFDC00 },
    { "CIAB ICR",   0xBFDD00 },
    { "CIAB CRA",   0xBFDE00 },
    { "CIAB CRB",   0xBFDF00 },
    { "CIAA PRA",   0xBFE001 },
    { "CIAA PRB",   0xBFE101 },
    { "CIAA DDRA",  0xBFE201 },
    { "CIAA DDRB",  0xBFE301 },
    { "CIAA TALO",  0xBFE401 },
    { "CIAA TAHI",  0xBFE501 },
    { "CIAA TBLO",  0xBFE601 },
    { "CIAA TBHI",  0xBFE701 },
    { "CIAA TDLO",  0xBFE801 },
    { "CIAA TDMD",  0xBFE901 },
    { "CIAA TDHI",  0xBFEA01 },
    { "CIAA SDR",   0xBFEC01 },
    { "CIAA ICR",   0xBFED01 },
    { "CIAA CRA",   0xBFEE01 },
    { "CIAA CRB",   0xBFEF01 },
    { "CLK S1",     0xDC0000 },
    { "CLK S10",    0xDC0004 },
    { "CLK MI1",    0xDC0008 },
    { "CLK MI10",   0xDC000C },
    { "CLK H1",     0xDC0010 },
    { "CLK H10",    0xDC0014 },
    { "CLK D1",     0xDC0018 },
    { "CLK D10",    0xDC001C },
    { "CLK MO1",    0xDC0020 },
    { "CLK MO10",   0xDC0024 },
    { "CLK Y1",     0xDC0028 },
    { "CLK Y10",    0xDC002E },
    { "CLK WEEK",   0xDC0030 },
    { "CLK CD",     0xDC0034 },
    { "CLK CE",     0xDC0038 },
    { "CLK CF",     0xDC003C },
    { NULL, 0 }
};

/* This table was generated from the list of AGA chip names in 
 * AGA.guide available on aminet. It could well have errors in it. */

struct customData custd[] =
{
#if 0
    { "BLTDDAT",  0xdff000 }, /* Blitter dest. early read (dummy address) */
#endif
    { "DMACONR",  0xdff002, 1, 0 }, /* Dma control (and blitter status) read */
    { "VPOSR",    0xdff004, 1, 0 }, /* Read vert most sig. bits (and frame flop */
    { "VHPOSR",   0xdff006, 1, 0 }, /* Read vert and horiz position of beam */
#if 0
    { "DSKDATR",  0xdff008 }, /* Disk data early read (dummy address) */
#endif
    { "JOY0DAT",  0xdff00A, 1, 0 }, /* Joystick-mouse 0 data (vert,horiz) */
    { "JOT1DAT",  0xdff00C, 1, 0 }, /* Joystick-mouse 1 data (vert,horiz) */
    { "CLXDAT",   0xdff00E, 1, 0 }, /* Collision data reg. (read and clear) */
    { "ADKCONR",  0xdff010, 1, 0 }, /* Audio,disk control register read */
    { "POT0DAT",  0xdff012, 1, 0 }, /* Pot counter pair 0 data (vert,horiz) */
    { "POT1DAT",  0xdff014, 1, 0 }, /* Pot counter pair 1 data (vert,horiz) */
    { "POTGOR",   0xdff016, 1, 0 }, /* Pot pin data read */
    { "SERDATR",  0xdff018, 1, 0 }, /* Serial port data and status read */
    { "DSKBYTR",  0xdff01A, 1, 0 }, /* Disk data byte and status read */
    { "INTENAR",  0xdff01C, 1, 0 }, /* Interrupt enable bits read */
    { "INTREQR",  0xdff01E, 1, 0 }, /* Interrupt request bits read */
    { "DSKPTH",   0xdff020, 2, 1 }, /* Disk pointer (high 5 bits) */
    { "DSKPTL",   0xdff022, 2, 2 }, /* Disk pointer (low 15 bits) */
    { "DSKLEN",   0xdff024, 2, 0 }, /* Disk lentgh */
#if 0
    { "DSKDAT",   0xdff026 }, /* Disk DMA data write */
    { "REFPTR",   0xdff028 }, /* Refresh pointer */
#endif
    { "VPOSW",    0xdff02A, 2, 0 }, /* Write vert most sig. bits(and frame flop) */
    { "VHPOSW",   0xdff02C, 2, 0 }, /* Write vert and horiz pos of beam */
    { "COPCON",   0xdff02e, 2, 0 }, /* Coprocessor control reg (CDANG) */
    { "SERDAT",   0xdff030, 2, 0 }, /* Serial port data and stop bits write */
    { "SERPER",   0xdff032, 2, 0 }, /* Serial port period and control */
    { "POTGO",    0xdff034, 2, 0 }, /* Pot count start,pot pin drive enable data */
    { "JOYTEST",  0xdff036, 2, 0 }, /* Write to all 4 joystick-mouse counters at once */
    { "STREQU",   0xdff038, 2, 0 }, /* Strobe for horiz sync with VB and EQU */
    { "STRVBL",   0xdff03A, 2, 0 }, /* Strobe for horiz sync with VB (vert blank) */
    { "STRHOR",   0xdff03C, 2, 0 }, /* Strobe for horiz sync */
    { "STRLONG",  0xdff03E, 2, 0 }, /* Strobe for identification of long horiz line */
    { "BLTCON0",  0xdff040, 2, 0 }, /* Blitter control reg 0 */
    { "BLTCON1",  0xdff042, 2, 0 }, /* Blitter control reg 1 */
    { "BLTAFWM",  0xdff044, 2, 0 }, /* Blitter first word mask for source A */
    { "BLTALWM",  0xdff046, 2, 0 }, /* Blitter last word mask for source A */
    { "BLTCPTH",  0xdff048, 2, 1 }, /* Blitter pointer to source C (high 5 bits) */
    { "BLTCPTL",  0xdff04A, 2, 2 }, /* Blitter pointer to source C (low 15 bits) */
    { "BLTBPTH",  0xdff04C, 2, 1 }, /* Blitter pointer to source B (high 5 bits) */
    { "BLTBPTL",  0xdff04E, 2, 2 }, /* Blitter pointer to source B (low 15 bits) */
    { "BLTAPTH",  0xdff050, 2, 1 }, /* Blitter pointer to source A (high 5 bits) */
    { "BLTAPTL",  0xdff052, 2, 2 }, /* Blitter pointer to source A (low 15 bits) */
    { "BPTDPTH",  0xdff054, 2, 1 }, /* Blitter pointer to destn  D (high 5 bits) */
    { "BLTDPTL",  0xdff056, 2, 2 }, /* Blitter pointer to destn  D (low 15 bits) */
    { "BLTSIZE",  0xdff058, 2, 0 }, /* Blitter start and size (win/width,height) */
    { "BLTCON0L", 0xdff05A, 2, 0 }, /* Blitter control 0 lower 8 bits (minterms) */
    { "BLTSIZV",  0xdff05C, 2, 0 }, /* Blitter V size (for 15 bit vert size) */
    { "BLTSIZH",  0xdff05E, 2, 0 }, /* Blitter H size & start (for 11 bit H size) */
    { "BLTCMOD",  0xdff060, 2, 0 }, /* Blitter modulo for source C */
    { "BLTBMOD",  0xdff062, 2, 0 }, /* Blitter modulo for source B */
    { "BLTAMOD",  0xdff064, 2, 0 }, /* Blitter modulo for source A */
    { "BLTDMOD",  0xdff066, 2, 0 }, /* Blitter modulo for destn  D */
#if 0
    { "Unknown",  0xdff068 }, /* Unknown or Unused */
    { "Unknown",  0xdff06a }, /* Unknown or Unused */
    { "Unknown",  0xdff06c }, /* Unknown or Unused */
    { "Unknown",  0xdff06e }, /* Unknown or Unused */
#endif
    { "BLTCDAT",  0xdff070, 2, 0 }, /* Blitter source C data reg */
    { "BLTBDAT",  0xdff072, 2, 0 }, /* Blitter source B data reg */
    { "BLTADAT",  0xdff074, 2, 0 }, /* Blitter source A data reg */
    { "BLTDDAT",  0xdff076, 2, 0 }, /* Blitter destination reg */
#if 0
    { "SPRHDAT",  0xdff078 }, /* Ext logic UHRES sprite pointer and data identifier */
    { "BPLHDAT",  0xdff07A }, /* Ext logic UHRES bit plane identifier */
#endif
    { "LISAID",   0xdff07C, 1, 0 }, /* Chip revision level for Denise/Lisa */
    { "DSKSYNC",  0xdff07E, 2, 0 }, /* Disk sync pattern reg for disk read */
    { "COP1LCH",  0xdff080, 2, 1 }, /* Coprocessor first location reg (high 5 bits) */
    { "COP1LCL",  0xdff082, 2, 2 }, /* Coprocessor first location reg (low 15 bits) */
    { "COP2LCH",  0xdff084, 2, 1 }, /* Coprocessor second reg (high 5 bits) */
    { "COP2LCL",  0xdff086, 2, 2 }, /* Coprocessor second reg (low 15 bits) */
    { "COPJMP1",  0xdff088, 2, 0 }, /* Coprocessor restart at first location */
    { "COPJMP2",  0xdff08A, 2, 0 }, /* Coprocessor restart at second location */
#if 0
    { "COPINS",   0xdff08C }, /* Coprocessor inst fetch identify */
#endif
    { "DIWSTRT",  0xdff08E, 2, 0 }, /* Display window start (upper left vert-hor pos) */
    { "DIWSTOP",  0xdff090, 2, 0 }, /* Display window stop (lower right vert-hor pos) */
    { "DDFSTRT",  0xdff092, 2, 0 }, /* Display bit plane data fetch start.hor pos */
    { "DDFSTOP",  0xdff094, 2, 0 }, /* Display bit plane data fetch stop.hor pos */
    { "DMACON",   0xdff096, 2, 0 }, /* DMA control write (clear or set) */
    { "CLXCON",   0xdff098, 2, 0 }, /* Collision control */
    { "INTENA",   0xdff09A, 2, 0 }, /* Interrupt enable bits (clear or set bits) */
    { "INTREQ",   0xdff09C, 2, 0 }, /* Interrupt request bits (clear or set bits) */
    { "ADKCON",   0xdff09E, 2, 0 }, /* Audio,disk,UART,control */
    { "AUD0LCH",  0xdff0A0, 2, 1 }, /* Audio channel 0 location (high 5 bits) */
    { "AUD0LCL",  0xdff0A2, 2, 2 }, /* Audio channel 0 location (low 15 bits) */
    { "AUD0LEN",  0xdff0A4, 2, 0 }, /* Audio channel 0 lentgh */
    { "AUD0PER",  0xdff0A6, 2, 0 }, /* Audio channel 0 period */
    { "AUD0VOL",  0xdff0A8, 2, 0 }, /* Audio channel 0 volume */
    { "AUD0DAT",  0xdff0AA, 2, 0 }, /* Audio channel 0 data */
#if 0
    { "Unknown",  0xdff0AC }, /* Unknown or Unused */
    { "Unknown",  0xdff0AE }, /* Unknown or Unused */
#endif
    { "AUD1LCH",  0xdff0B0, 2, 0 }, /* Audio channel 1 location (high 5 bits) */
    { "AUD1LCL",  0xdff0B2, 2, 0 }, /* Audio channel 1 location (low 15 bits) */
    { "AUD1LEN",  0xdff0B4, 2, 0 }, /* Audio channel 1 lentgh */
    { "AUD1PER",  0xdff0B6, 2, 0 }, /* Audio channel 1 period */
    { "AUD1VOL",  0xdff0B8, 2, 0 }, /* Audio channel 1 volume */
    { "AUD1DAT",  0xdff0BA, 2, 0 }, /* Audio channel 1 data */
#if 0
    { "Unknown",  0xdff0BC }, /* Unknown or Unused */
    { "Unknown",  0xdff0BE }, /* Unknown or Unused */
#endif
    { "AUD2LCH",  0xdff0C0, 2, 0 }, /* Audio channel 2 location (high 5 bits) */
    { "AUD2LCL",  0xdff0C2, 2, 0 }, /* Audio channel 2 location (low 15 bits) */
    { "AUD2LEN",  0xdff0C4, 2, 0 }, /* Audio channel 2 lentgh */
    { "AUD2PER",  0xdff0C6, 2, 0 }, /* Audio channel 2 period */
    { "AUD2VOL",  0xdff0C8, 2, 0 }, /* Audio channel 2 volume */
    { "AUD2DAT",  0xdff0CA, 2, 0 }, /* Audio channel 2 data */
#if 0
    { "Unknown",  0xdff0CC }, /* Unknown or Unused */
    { "Unknown",  0xdff0CE }, /* Unknown or Unused */
#endif
    { "AUD3LCH",  0xdff0D0, 2, 0 }, /* Audio channel 3 location (high 5 bits) */
    { "AUD3LCL",  0xdff0D2, 2, 0 }, /* Audio channel 3 location (low 15 bits) */
    { "AUD3LEN",  0xdff0D4, 2, 0 }, /* Audio channel 3 lentgh */
    { "AUD3PER",  0xdff0D6, 2, 0 }, /* Audio channel 3 period */
    { "AUD3VOL",  0xdff0D8, 2, 0 }, /* Audio channel 3 volume */
    { "AUD3DAT",  0xdff0DA, 2, 0 }, /* Audio channel 3 data */
#if 0
    { "Unknown",  0xdff0DC }, /* Unknown or Unused */
    { "Unknown",  0xdff0DE }, /* Unknown or Unused */
#endif
    { "BPL1PTH",  0xdff0E0, 2 }, /* Bit plane pointer 1 (high 5 bits) */
    { "BPL1PTL",  0xdff0E2, 2 }, /* Bit plane pointer 1 (low 15 bits) */
    { "BPL2PTH",  0xdff0E4, 2 }, /* Bit plane pointer 2 (high 5 bits) */
    { "BPL2PTL",  0xdff0E6, 2 }, /* Bit plane pointer 2 (low 15 bits) */
    { "BPL3PTH",  0xdff0E8, 2 }, /* Bit plane pointer 3 (high 5 bits) */
    { "BPL3PTL",  0xdff0EA, 2 }, /* Bit plane pointer 3 (low 15 bits) */
    { "BPL4PTH",  0xdff0EC, 2 }, /* Bit plane pointer 4 (high 5 bits) */
    { "BPL4PTL",  0xdff0EE, 2 }, /* Bit plane pointer 4 (low 15 bits) */
    { "BPL5PTH",  0xdff0F0, 2 }, /* Bit plane pointer 5 (high 5 bits) */
    { "BPL5PTL",  0xdff0F2, 2 }, /* Bit plane pointer 5 (low 15 bits) */
    { "BPL6PTH",  0xdff0F4, 2 }, /* Bit plane pointer 6 (high 5 bits) */
    { "BPL6PTL",  0xdff0F6, 2 }, /* Bit plane pointer 6 (low 15 bits) */
    { "BPL7PTH",  0xdff0F8, 2 }, /* Bit plane pointer 7 (high 5 bits) */
    { "BPL7PTL",  0xdff0FA, 2 }, /* Bit plane pointer 7 (low 15 bits) */
    { "BPL8PTH",  0xdff0FC, 2 }, /* Bit plane pointer 8 (high 5 bits) */
    { "BPL8PTL",  0xdff0FE, 2 }, /* Bit plane pointer 8 (low 15 bits) */
    { "BPLCON0",  0xdff100, 2 }, /* Bit plane control reg (misc control bits) */
    { "BPLCON1",  0xdff102, 2 }, /* Bit plane control reg (scroll val PF1,PF2) */
    { "BPLCON2",  0xdff104, 2 }, /* Bit plane control reg (priority control) */
    { "BPLCON3",  0xdff106, 2 }, /* Bit plane control reg (enhanced features) */
    { "BPL1MOD",  0xdff108, 2 }, /* Bit plane modulo (odd planes,or active- fetch lines if bitplane scan-doubling is enabled */
    { "BPL2MOD",  0xdff10A, 2 }, /* Bit plane modulo (even planes or inactive- fetch lines if bitplane scan-doubling is enabled */
    { "BPLCON4",  0xdff10C, 2 }, /* Bit plane control reg (bitplane and sprite masks) */
    { "CLXCON2",  0xdff10e, 2 }, /* Extended collision control reg */
    { "BPL1DAT",  0xdff110, 2 }, /* Bit plane 1 data (parallel to serial con- vert) */
    { "BPL2DAT",  0xdff112, 2 }, /* Bit plane 2 data (parallel to serial con- vert) */
    { "BPL3DAT",  0xdff114, 2 }, /* Bit plane 3 data (parallel to serial con- vert) */
    { "BPL4DAT",  0xdff116, 2 }, /* Bit plane 4 data (parallel to serial con- vert) */
    { "BPL5DAT",  0xdff118, 2 }, /* Bit plane 5 data (parallel to serial con- vert) */
    { "BPL6DAT",  0xdff11a, 2 }, /* Bit plane 6 data (parallel to serial con- vert) */
    { "BPL7DAT",  0xdff11c, 2 }, /* Bit plane 7 data (parallel to serial con- vert) */
    { "BPL8DAT",  0xdff11e, 2 }, /* Bit plane 8 data (parallel to serial con- vert) */
    { "SPR0PTH",  0xdff120, 2 }, /* Sprite 0 pointer (high 5 bits) */
    { "SPR0PTL",  0xdff122, 2 }, /* Sprite 0 pointer (low 15 bits) */
    { "SPR1PTH",  0xdff124, 2 }, /* Sprite 1 pointer (high 5 bits) */
    { "SPR1PTL",  0xdff126, 2 }, /* Sprite 1 pointer (low 15 bits) */
    { "SPR2PTH",  0xdff128, 2 }, /* Sprite 2 pointer (high 5 bits) */
    { "SPR2PTL",  0xdff12A, 2 }, /* Sprite 2 pointer (low 15 bits) */
    { "SPR3PTH",  0xdff12C, 2 }, /* Sprite 3 pointer (high 5 bits) */
    { "SPR3PTL",  0xdff12E, 2 }, /* Sprite 3 pointer (low 15 bits) */
    { "SPR4PTH",  0xdff130, 2 }, /* Sprite 4 pointer (high 5 bits) */
    { "SPR4PTL",  0xdff132, 2 }, /* Sprite 4 pointer (low 15 bits) */
    { "SPR5PTH",  0xdff134, 2 }, /* Sprite 5 pointer (high 5 bits) */
    { "SPR5PTL",  0xdff136, 2 }, /* Sprite 5 pointer (low 15 bits) */
    { "SPR6PTH",  0xdff138, 2 }, /* Sprite 6 pointer (high 5 bits) */
    { "SPR6PTL",  0xdff13A, 2 }, /* Sprite 6 pointer (low 15 bits) */
    { "SPR7PTH",  0xdff13C, 2 }, /* Sprite 7 pointer (high 5 bits) */
    { "SPR7PTL",  0xdff13E, 2 }, /* Sprite 7 pointer (low 15 bits) */
    { "SPR0POS",  0xdff140, 2 }, /* Sprite 0 vert-horiz start pos data */
    { "SPR0CTL",  0xdff142, 2 }, /* Sprite 0 position and control data */
    { "SPR0DATA", 0xdff144, 2 }, /* Sprite 0 image data register A */
    { "SPR0DATB", 0xdff146, 2 }, /* Sprite 0 image data register B */
    { "SPR1POS",  0xdff148, 2 }, /* Sprite 1 vert-horiz start pos data */
    { "SPR1CTL",  0xdff14A, 2 }, /* Sprite 1 position and control data */
    { "SPR1DATA", 0xdff14C, 2 }, /* Sprite 1 image data register A */
    { "SPR1DATB", 0xdff14E, 2 }, /* Sprite 1 image data register B */
    { "SPR2POS",  0xdff150, 2 }, /* Sprite 2 vert-horiz start pos data */
    { "SPR2CTL",  0xdff152, 2 }, /* Sprite 2 position and control data */
    { "SPR2DATA", 0xdff154, 2 }, /* Sprite 2 image data register A */
    { "SPR2DATB", 0xdff156, 2 }, /* Sprite 2 image data register B */
    { "SPR3POS",  0xdff158, 2 }, /* Sprite 3 vert-horiz start pos data */
    { "SPR3CTL",  0xdff15A, 2 }, /* Sprite 3 position and control data */
    { "SPR3DATA", 0xdff15C, 2 }, /* Sprite 3 image data register A */
    { "SPR3DATB", 0xdff15E, 2 }, /* Sprite 3 image data register B */
    { "SPR4POS",  0xdff160, 2 }, /* Sprite 4 vert-horiz start pos data */
    { "SPR4CTL",  0xdff162, 2 }, /* Sprite 4 position and control data */
    { "SPR4DATA", 0xdff164, 2 }, /* Sprite 4 image data register A */
    { "SPR4DATB", 0xdff166, 2 }, /* Sprite 4 image data register B */
    { "SPR5POS",  0xdff168, 2 }, /* Sprite 5 vert-horiz start pos data */
    { "SPR5CTL",  0xdff16A, 2 }, /* Sprite 5 position and control data */
    { "SPR5DATA", 0xdff16C, 2 }, /* Sprite 5 image data register A */
    { "SPR5DATB", 0xdff16E, 2 }, /* Sprite 5 image data register B */
    { "SPR6POS",  0xdff170, 2 }, /* Sprite 6 vert-horiz start pos data */
    { "SPR6CTL",  0xdff172, 2 }, /* Sprite 6 position and control data */
    { "SPR6DATA", 0xdff174, 2 }, /* Sprite 6 image data register A */
    { "SPR6DATB", 0xdff176, 2 }, /* Sprite 6 image data register B */
    { "SPR7POS",  0xdff178, 2 }, /* Sprite 7 vert-horiz start pos data */
    { "SPR7CTL",  0xdff17A, 2 }, /* Sprite 7 position and control data */
    { "SPR7DATA", 0xdff17C, 2 }, /* Sprite 7 image data register A */
    { "SPR7DATB", 0xdff17E, 2 }, /* Sprite 7 image data register B */
    { "COLOR00",  0xdff180, 2 }, /* Color table 00 */
    { "COLOR01",  0xdff182, 2 }, /* Color table 01 */
    { "COLOR02",  0xdff184, 2 }, /* Color table 02 */
    { "COLOR03",  0xdff186, 2 }, /* Color table 03 */
    { "COLOR04",  0xdff188, 2 }, /* Color table 04 */
    { "COLOR05",  0xdff18A, 2 }, /* Color table 05 */
    { "COLOR06",  0xdff18C, 2 }, /* Color table 06 */
    { "COLOR07",  0xdff18E, 2 }, /* Color table 07 */
    { "COLOR08",  0xdff190, 2 }, /* Color table 08 */
    { "COLOR09",  0xdff192, 2 }, /* Color table 09 */
    { "COLOR10",  0xdff194, 2 }, /* Color table 10 */
    { "COLOR11",  0xdff196, 2 }, /* Color table 11 */
    { "COLOR12",  0xdff198, 2 }, /* Color table 12 */
    { "COLOR13",  0xdff19A, 2 }, /* Color table 13 */
    { "COLOR14",  0xdff19C, 2 }, /* Color table 14 */
    { "COLOR15",  0xdff19E, 2 }, /* Color table 15 */
    { "COLOR16",  0xdff1A0, 2 }, /* Color table 16 */
    { "COLOR17",  0xdff1A2, 2 }, /* Color table 17 */
    { "COLOR18",  0xdff1A4, 2 }, /* Color table 18 */
    { "COLOR19",  0xdff1A6, 2 }, /* Color table 19 */
    { "COLOR20",  0xdff1A8, 2 }, /* Color table 20 */
    { "COLOR21",  0xdff1AA, 2 }, /* Color table 21 */
    { "COLOR22",  0xdff1AC, 2 }, /* Color table 22 */
    { "COLOR23",  0xdff1AE, 2 }, /* Color table 23 */
    { "COLOR24",  0xdff1B0, 2 }, /* Color table 24 */
    { "COLOR25",  0xdff1B2, 2 }, /* Color table 25 */
    { "COLOR26",  0xdff1B4, 2 }, /* Color table 26 */
    { "COLOR27",  0xdff1B6, 2 }, /* Color table 27 */
    { "COLOR28",  0xdff1B8, 2 }, /* Color table 28 */
    { "COLOR29",  0xdff1BA, 2 }, /* Color table 29 */
    { "COLOR30",  0xdff1BC, 2 }, /* Color table 30 */
    { "COLOR31",  0xdff1BE, 2 }, /* Color table 31 */
    { "HTOTAL",   0xdff1C0, 2 }, /* Highest number count in horiz line (VARBEAMEN = 1) */
    { "HSSTOP",   0xdff1C2, 2 }, /* Horiz line pos for HSYNC stop */
    { "HBSTRT",   0xdff1C4, 2 }, /* Horiz line pos for HBLANK start */
    { "HBSTOP",   0xdff1C6, 2 }, /* Horiz line pos for HBLANK stop */
    { "VTOTAL",   0xdff1C8, 2 }, /* Highest numbered vertical line (VARBEAMEN = 1) */
    { "VSSTOP",   0xdff1CA, 2 }, /* Vert line for VBLANK start */
    { "VBSTRT",   0xdff1CC, 2 }, /* Vert line for VBLANK start */
    { "VBSTOP",   0xdff1CE, 2 }, /* Vert line for VBLANK stop */
#if 0
    { "SPRHSTRT", 0xdff1D0 }, /* UHRES sprite vertical start */
    { "SPRHSTOP", 0xdff1D2 }, /* UHRES sprite vertical stop */
    { "BPLHSTRT", 0xdff1D4 }, /* UHRES bit plane vertical stop */
    { "BPLHSTOP", 0xdff1D6 }, /* UHRES bit plane vertical stop */
    { "HHPOSW",   0xdff1D8 }, /* DUAL mode hires H beam counter write */
    { "HHPOSR",   0xdff1DA }, /* DUAL mode hires H beam counter read */
#endif
    { "BEAMCON0", 0xdff1DC, 2 }, /* Beam counter control register (SHRES,UHRES,PAL) */
    { "HSSTRT",   0xdff1DE, 2 }, /* Horizontal sync start (VARHSY) */
    { "VSSTRT",   0xdff1E0, 2 }, /* Vertical sync start (VARVSY) */
    { "HCENTER",  0xdff1E2, 2 }, /* Horizontal pos for vsync on interlace */
    { "DIWHIGH",  0xdff1E4, 2 }, /* Display window upper bits for start/stop */
#if 0
    { "BPLHMOD",  0xdff1E6 }, /* UHRES bit plane modulo */
    { "SPRHPTH",  0xdff1E8 }, /* UHRES sprite pointer (high 5 bits) */
    { "SPRHPTL",  0xdff1EA }, /* UHRES sprite pointer (low 15 bits) */
    { "BPLHPTH",  0xdff1EC }, /* VRam (UHRES) bitplane pointer (hi 5 bits) */
    { "BPLHPTL",  0xdff1EE }, /* VRam (UHRES) bitplane pointer (lo 15 bits) */
    { "RESERVED", 0xdff1F0 }, /* Reserved (forever i guess!) */
    { "RESERVED", 0xdff1F2 }, /* Reserved (forever i guess!) */
    { "RESERVED", 0xdff1F4 }, /* Reserved (forever i guess!) */
    { "RESERVED", 0xdff1F6 }, /* Reserved (forever i guess!) */
    { "RESERVED", 0xdff1F8 }, /* Reserved (forever i guess!) */
    { "RESERVED", 0xdff1Fa }, /* Reserved (forever i guess!) */
#endif
    { "FMODE",    0xdff1FC, 2 }, /* Fetch mode register */
#if 0
    { "NO-OP(NULL)", 0xdff1FE },        /*   Can also indicate last 2 or 3 refresh
					    cycles or the restart of the COPPER after lockup.*/
#endif
};

#endif
