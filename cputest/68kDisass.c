/***
 *	68k disassembler, written 2010 by Markus Fritze, www.sarnau.com
 *	
 *	This file is distributed under the GNU General Public License, version 2
 *	or at your option any later version. Read the file gpl.txt for details.
 ***/

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define bool int
#define false 0
#define true 1
typedef unsigned int uaecptr;
typedef unsigned int uae_u32;
typedef int uae_s32;
typedef unsigned short uae_u16;
typedef short uae_s16;
typedef unsigned char uae_u8;
typedef signed char uae_s8;
typedef unsigned int Uint32;
typedef int int32;

#include "68kDisass.h"

#define ADDRESS_ON_PC		1
#define USE_SYMBOLS			1

typedef enum {
	doptNoBrackets = 1,		// hide brackets around absolute addressing
	doptOpcodesSmall = 2,	// opcodes are small letters
	doptRegisterSmall = 4,	// register names are small letters
	doptStackSP = 8		// stack pointer is named "SP" instead of "A7" (except for MOVEM)
} Diss68kOptions;

static Diss68kOptions	options = doptOpcodesSmall | doptRegisterSmall | doptStackSP | doptNoBrackets;

/* all options */
static const Diss68kOptions optionsMask = doptOpcodesSmall | doptRegisterSmall | doptStackSP | doptNoBrackets;

// values <0 will hide the group
static int			optionPosAddress = 0;	// current address
static int			optionPosHexdump = 10;	// 16-bit words at this address
static int			optionPosLabel = 35;	// label, if defined
static int			optionPosOpcode = 47;	// opcode
static int			optionPosOperand = 57;	// operands for the opcode
static int			optionPosComment = 82;	// comment, if defined

/***
 *	Motorola 16-/32-Bit Microprocessor and coprocessor types
 ***/
#define MC68000			0x000001	// 16-/32-Bit Microprocessor
	#define MC68EC000	0x000002	// 16-/32-Bit Embedded Controller
	#define MC68HC000	0x000004	// Low Power 16-/32-Bit Microprocessor
#define MC68008			0x000008	// 16-Bit Microprocessor with 8-Bit Data Bus
#define MC68010			0x000010	// 16-/32-Bit Virtual Memory Microprocessor
#define MC68020			0x000020	// 32-Bit Virtual Memory Microprocessor
	#define MC68EC020	0x000040	// 32-Bit Embedded Controller (no PMMU)
#define MC68030			0x000080	// Second-Generation 32-Bit Enhanced Microprocessor
	#define MC68EC030	0x000100	// 32-Bit Embedded Controller (no PMMU)
#define MC68040			0x000200	// Third-Generation 32-Bit Microprocessor
	#define MC68LC040	0x000400	// Third-Generation 32-Bit Microprocessor (no FPU)
	#define MC68EC040	0x000800	// 32-Bit Embedded Controller (no FPU, no PMMU)
#define MC68330			0x001000	// CPU32 Integrated CPU32 Processor
#define MC68340			0x002000	// CPU32 Integrated Processor with DMA
#define MC68060			0x004000	// Fourth-Generation 32-Bit Microprocessor
	#define MC68LC060	0x008000	// Fourth-Generation 32-Bit Microprocessor (no FPU)
	#define MC68EC060	0x010000	// Fourth-Generation 32-Bit Microprocessor (no FPU, no PMMU)
#define MC_CPU32		(MC68330|MC68340)

#define MC_020			(MC68020|MC68EC020|MC68030|MC68EC030|MC68040|MC68LC040|MC68EC040|MC_CPU32|MC68060|MC68LC060|MC68EC060)
#define MC_ALL			(MC68000|MC68EC000|MC68HC000|MC68008|MC68010|MC_020)

#define MC68851			0x020000	// Paged Memory Management Unit

#define MC68881			0x040000	// Floating-PointCoprocessor
#define MC68882			0x080000	// Enhanced Floating-Point Coprocessor

#define MC_PMMU			(MC68881|MC68882)
#define MC_FPU			(MC68881|MC68882)

static int				optionCPUTypeMask = ( MC_ALL & ~MC68040 & ~MC_CPU32 & ~MC68060 ) | MC_PMMU | MC_FPU;


typedef enum {
	dtNone,
	dtByte,				// a specific number of bytes, usually 1
	dtWord,				// one 16-bit value
	dtLong,				// one 32-bit value
	dtOpcode,			// an opcode of variable length
	dtASCString,		// a 0-byte terminated ASCII string
	dtPointer,			// a generic 32-bit pointer
	dtFunctionPointer,	// a 32-bit pointer to a function
	dtStringArray		// a specific number of ASCII bytes
} Disass68kDataType;

typedef struct {
	char	*name;
	char	*comment;
	Disass68kDataType	type;
	int		size;
} disStructElement;

typedef struct {
	char	*name;				// name of the structure
	int		size;				// size of structure
	int		count;				// number of lines
	disStructElement	*elements;	// array of all elements of the struct
} disStructEntry;

static int				disStructCounts;
static disStructEntry	*disStructEntries;

typedef struct {
	long	addr;				// address of the label
	Disass68kDataType	type;	// type of the data on the address
	int		size;				// size of the label, references inside it are addressed via base address + offset
	int		count;				// number of elements at this address with the given size
	int		structIndex;		// -1 no struct to describe the element
	char	*name;				// name of the label
	char	*comment;			// optional comment
} disSymbolEntry;

static int				disSymbolCounts;
static disSymbolEntry	*disSymbolEntries;


static inline unsigned short	Disass68kGetWord(long addr)
{
	uae_u8 *p = (uae_u8 *)addr;
	return (p[0] << 8) | p[1];
}

static Disass68kDataType	Disass68kType(long addr, char *addressLabel, char *commentBuffer, int *count)
{
	int	i,j;

	addressLabel[0] = 0;
	commentBuffer[0] = 0;
	for(i=0; i<disSymbolCounts; ++i)
	{
		const disStructEntry	*se;
		const disSymbolEntry	*dse = &disSymbolEntries[i];
		int		offset = addr - dse->addr;

		if(offset < 0 || offset >= dse->count * dse->size)
			continue;

		// no special struct that devices this value?
		if(dse->structIndex < 0)
		{
			offset = (offset + dse->size - 1) / dse->size;
			*count = dse->count - offset;
			if(offset == 0)	// only in the first line
			{
				strcpy(addressLabel, dse->name);
				if(dse->comment)
					strcpy(commentBuffer, dse->comment);
			}
			return dse->type;
		}

		*count = 1;
		se = &disStructEntries[dse->structIndex];
		for(j=0; j<se->count; ++j)
		{
			const disStructElement	*e = &se->elements[j];
			if(offset < e->size)
			{
				if(e->type == dtStringArray)
					*count = e->size;
				if(j == 0)
					strcpy(addressLabel, dse->name);

				sprintf(commentBuffer, "[%s]", e->name);
				if(e->comment)
					strcat(commentBuffer, e->comment);
				return e->type;
			}
			offset -= e->size;
		}
		return dse->size;
	}
	return dtNone;
}

/***
 *	Lookup a symbol name
 ***/
static const char	*Disass68kSymbolName(long addr, int size)
{
	int	i;

	for(i=0; i<disSymbolCounts; ++i)
	{
		static char	symbolName[128];
		const disSymbolEntry	*dse = &disSymbolEntries[i];
		int	offset = addr - dse->addr;
		int	reminder;

		if(offset < 0 || offset >= dse->count * dse->size)
			continue;

		if(dse->name[0] == 0)
			return NULL;

		reminder = offset % dse->size;
		offset /= dse->size;

		strcpy(symbolName, dse->name);
		if(offset)
			sprintf(symbolName+strlen(symbolName), "+%d*%d", dse->size, offset);
		if(reminder)
			sprintf(symbolName+strlen(symbolName), "+%d", reminder);
		return symbolName;
	}
	return NULL;
}

/***
 *	return a string pointer to display a register name
 ***/
static const char	*Disass68kRegname(int reg)
{
	static char		regName[3];
	switch(reg)
	{
	case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
		sprintf(regName, "%c%d", (options & doptRegisterSmall ? 'd' : 'D'), reg);
		break;

	case 0x0F:
		if(options & doptStackSP)	// display A7 as SP
		{
			if(options & doptRegisterSmall)
				return "sp";
			return "SP";
		}
	case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E:
		sprintf(regName, "%c%d", (options & doptRegisterSmall ? 'a' : 'A'), reg & 7);
		break;
	}
	return regName;
}

/***
 *	return a string pointer to display a register name
 ***/
static const char	*Disass68kNumber(int val)
{
	static char		numString[32];
	if(val >= -9 && val <= 9)
	{
		sprintf(numString, "%d", val);
	} else {
		// 4 characters/numbers or underscore (e.g. for cookies)
		unsigned char c0 = (val >> 24) & 0xFF;
		unsigned char c1 = (val >> 16) & 0xFF;
		unsigned char c2 = (val >>  8) & 0xFF;
		unsigned char c3 = (val >>  0) & 0xFF;
		if((isalnum(c0) || c0 == '_') && (isalnum(c1) || c1 == '_') && (isalnum(c2) || c2 == '_') && (isalnum(c3) || c3 == '_'))
		{
			sprintf(numString, "'%c%c%c%c'", c0, c1, c2, c3);
		} else {
			sprintf(numString, "$%x", val);
		}
	}
	return numString;
}

/***
 *	Supported registers for e.g. MOVEC
 ***/
#define REG_CCR			-1
#define REG_SR			-2
#define REG_PC			-3
#define REG_ZPC			-4
#define REG_TT0			-8
#define REG_TT1			-9
#define REG_MMUSR		-10
#define REG_USP			0x800
#define REG_SFC			0x000
#define REG_DFC			0x001
#define REG_TC			0x10000
#define REG_SRP			0x10002
#define REG_CRP			0x10003
#define REG_VAL			0x20000
#define REG_CACHES_NONE	0x20010
#define REG_CACHES_IC	0x20011
#define REG_CACHES_DC	0x20012
#define REG_CACHES_ICDC	0x20013
#define REG_FPU_FPCR	0x30004
#define REG_FPU_FPSR	0x30002
#define REG_FPU_FPIAR	0x30001

static const char *Disass68kSpecialRegister(int reg)
{
	static char	buf[8];
	const char	*sp = NULL;
	switch (reg)
	{
	case 0x000:		sp = "SFC"; break;
	case 0x001:		sp = "DFC"; break;
	case 0x002:		sp = "CACR"; break;
	case 0x003:		sp = "TC"; break;
	case 0x004:		sp = "ITT0"; break;	// IACR0 on an 68EC040 only
	case 0x005:		sp = "ITT1"; break;	// IACR1 on an 68EC040 only
	case 0x006:		sp = "DTT0"; break;	// DACR0 on an 68EC040 only
	case 0x007:		sp = "DTT1"; break;	// DACR1 on an 68EC040 only
	case 0x008:		sp = "BUSCR"; break;

	case 0x800:		sp = "USP"; break;
	case 0x801:		sp = "VBR"; break;
	case 0x802:		sp = "CAAR"; break;
	case 0x803:		sp = "MSP"; break;
	case 0x804:		sp = "ISP"; break;
	case 0x805:		sp = "MMUSR"; break;
	case 0x806:		sp = "URP"; break;
	case 0x807:		sp = "SRP"; break;
	case 0x808:		sp = "PCR"; break;

	// MMU register
	case 0x10000:	sp = "TC"; break;
	case 0x10001:	sp = "DRP"; break;
	case 0x10002:	sp = "SRP"; break;
	case 0x10003:	sp = "CRP"; break;
	case 0x10004:	sp = "CAL"; break;
	case 0x10005:	sp = "VAL"; break;
	case 0x10006:	sp = "SCCR"; break;
	case 0x10007:	sp = "ACR"; break;

	case REG_CCR:	sp = "CCR"; break;
	case REG_SR:	sp = "SR"; break;
	case REG_PC:	sp = "PC"; break;
	case REG_ZPC:	sp = "ZPC"; break;
	case REG_TT0:	sp = "TT0"; break;
	case REG_TT1:	sp = "TT1"; break;
	case REG_MMUSR:	sp = "MMUSR"; break;

	case REG_VAL:	sp = "VAL"; break;

	case REG_CACHES_NONE:	sp = "NC"; break;
	case REG_CACHES_IC:		sp = "IC"; break;
	case REG_CACHES_DC:		sp = "DC"; break;
	case REG_CACHES_ICDC:	sp = "IC/DC"; break;	// GCC lists this as "BC"

	case REG_FPU_FPCR:		sp = "FPCR"; break;
	case REG_FPU_FPSR:		sp = "FPSR"; break;
	case REG_FPU_FPIAR:		sp = "FPIAR"; break;

	// unknown register => unknown opcode!
	default:		return NULL;
	}

	if(options & doptRegisterSmall)
	{
		char	*bp;
		strcpy(buf, sp);
		for (bp = buf; *bp; ++bp)
			*bp = tolower((unsigned char)*bp);
		return buf;
	}
	return sp;
}

/***
 *	680x0 EA disassembly, supports all address modes
 *
 *	disassbuf = output buffer for the EA, empty string in case of an illegal EA
 *	addr = pointer to the address, which Disass68kGetWord() will allow to read memory.
 *		   Incremented by the function to point behind the opcode, when done
 *	ea = 6-bit ea from the opcode
 *  size = addressed size of the opcode in bytes (e.g. 1,2,4 for MOVE.B, MOVE.W, MOVE.L), only used for immediate addressing
 ***/

#define EA_Dn				0x00001	// Dn
#define EA_An				0x00002	// An
#define EA_Ani				0x00004	// (An)
#define EA_Anip				0x00008	// (An)+
#define EA_piAn				0x00010	// -(An)
#define EA_dAn				0x00020	// d(An), d(An,Dn), etc.
#define EA_PCRel			0x00040	// d(PC), d(PC,Dn), etc.
#define EA_Abs				0x00080	// abs.w, abs.l
#define EA_Immed			0x00100	// #<val>

#define EA_ImmedParameter	0x0200	// an immediate value as a parameter
#define EA_ValueParameter	0x0400	// an immediate value as a parameter without the "#"
#define EA_SpecialRegister	0x0800	// any special register e.g. SR,CCR,USP,etc
#define EA_PCDisplacement	0x1000	// PC relative jump, like for BRA and friends

#define EA_All				(EA_Dn | EA_An | EA_Ani | EA_Anip | EA_piAn | EA_dAn | EA_Abs | EA_Immed | EA_PCRel)
#define EA_Dest				(EA_Dn | EA_An | EA_Ani | EA_Anip | EA_piAn | EA_dAn | EA_Abs)

static char		*Disass68kEA(char *disassbuf, char *commentBuffer, long *addr, long opcodeAddr, int ea, int size, int allowedEAs, int parameterValue, int disassFlag)
{
	unsigned short	eWord1;
	unsigned short	eWord2;
	int				xn,c,scale;
	int				reg = ea & 7;
	const char		*sp;
	long			val;
	char	regName[3];
	signed long	pcoffset;

	disassbuf[0] = 0;
	switch(ea)
	{
	// M=000 = 0	Dn
	// Data Register Direct Mode
	// Dn
	// M=001 = 1	An
	// Address Register Direct Mode
	// An
	case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
		if((allowedEAs & EA_Dn) != EA_Dn)
			break;
		sprintf(disassbuf, "%s", Disass68kRegname(ea & 0x0F));
		break;
	case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F:
		if((allowedEAs & EA_An) != EA_An)
			break;
		sprintf(disassbuf, "%s", Disass68kRegname(ea & 0x0F));
		break;

	// M=010 = 2
	// Address Register Indirect Mode
	// (An)
	case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
		if((allowedEAs & EA_Ani) != EA_Ani)
			break;
		sprintf(disassbuf, "(%s)", Disass68kRegname(reg | 8));
		break;

	// M=011 = 3
	// Address Register Indirect with Postincrement Mode
	// (An) +
	case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
		if((allowedEAs & EA_Anip) != EA_Anip)
			break;
		sprintf(disassbuf, "(%s)+", Disass68kRegname(reg | 8));
		break;

	// M=100 = 4
	// Address Register Indirect with Predecrement Mode
	// – (An)
	case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
		if((allowedEAs & EA_piAn) != EA_piAn)
			break;
		sprintf(disassbuf, "-(%s)", Disass68kRegname(reg | 8));
		break;

	// M=101 = 5
	// Address Register Indirect with Displacement Mode
	// (d16,An)
	case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
		if((allowedEAs & EA_dAn) != EA_dAn)
			break;
		eWord1 = Disass68kGetWord(*addr); *addr += 2;
		sprintf(disassbuf, "%s(%s)", Disass68kNumber(eWord1), Disass68kRegname(reg | 8));
		break;

	// M=111 = 7, Xn/reg = 011 = 3
	// Program Counter Indirect with Index (Base Displacement) Mode
	// (bd, PC, Xn. SIZE*SCALE)
	// Program Counter Memory Indirect Postindexed Mode
	// ([bd,PC],Xn.SIZE*SCALE,od)
	// Program Counter Memory Indirect Preindexed Mode
	// ([bd,PC,Xn.SIZE*SCALE],od)
	case 0x3B:
		// This is equal to the following, except that instead of An, it is PC relative

	// M=110 = 6
	// Address Register Indirect with Index (Base Displacement) Mode
	// (bd,An,Xn.SIZE*SCALE)
	// Memory Indirect Postindexed Mode
	// ([bd,An],Xn.SIZE*SCALE,od)
	// Memory Indirect Preindexed Mode
	// ([bd, An, Xn.SIZE*SCALE], od)
	case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
		eWord1 = Disass68kGetWord(*addr); *addr += 2;
		xn = (eWord1 >> 12) & 0x0F;				// Register D0..D7/A0..A7
		c = ((eWord1 >> 11) & 1) ? 'l' : 'w';	// Word/Long-Word Index Size 0 = Sign-Extended Word 1 = Long Word
		scale = (eWord1 >> 9) & 3;				// Scale Factor 00 = 1 01 = 2 10 = 4 11 = 8

		if(ea == 0x3B)
		{
			sp = Disass68kSpecialRegister(REG_PC);
			if(!sp) return NULL;
			strcpy(regName, sp);
		} else {
			sprintf(regName, "%s", Disass68kRegname(reg | 8));
		}

		if((eWord1 & 0x0100) == 0)
		{
			const char	*numStr;

			// BRIEF EXTENSION WORD FORMAT
			if(ea == 0x3B)
			{
				if((allowedEAs & EA_PCRel) != EA_PCRel)
					break;
			} else {
				if((allowedEAs & EA_dAn) != EA_dAn)
					break;
			}

			// Address Register Indirect with Index (8-Bit Displacement) Mode
			// (d8 ,An, Xn.SIZE*SCALE)
			numStr = Disass68kNumber(eWord1 & 0xFF);
			if(numStr[0] == '0' && numStr[1] == 0)
				numStr = "";

			// scale is only on 68020 and later supported
			if(scale != 0 && (optionCPUTypeMask & MC_020) == 0)
				return NULL;

			if(scale == 0)
			{
#if ADDRESS_ON_PC
				if(ea == 0x3B)
					sprintf(disassbuf, "$%lx(%s,%s.%c)", (signed char)(eWord1 & 0xFF) + opcodeAddr + 2, Disass68kSpecialRegister(REG_PC), Disass68kRegname(xn), c);
				else
#endif
					sprintf(disassbuf, "%s(%s,%s.%c)", numStr, regName, Disass68kRegname(xn), c);
			} else
			{
#if ADDRESS_ON_PC
				if(ea == 0x3B)
					sprintf(disassbuf, "$%lx(%s,%s.%c*%d)", (signed char)(eWord1 & 0xFF) + opcodeAddr + 2, Disass68kSpecialRegister(REG_PC), Disass68kRegname(xn), c, 1 << scale);
				else
#endif
					sprintf(disassbuf, "%s(%s,%s.%c*%d)", numStr, regName, Disass68kRegname(xn), c, 1 << scale);
			}
#if USE_SYMBOLS
			if(ea == 0x3B)
			{
				const char	*symStr = Disass68kSymbolName((signed char)(eWord1 & 0xFF) + opcodeAddr + 2, size);
				if(symStr)
				{
					commentBuffer += strlen(commentBuffer);
					sprintf(commentBuffer+strlen(commentBuffer), "%s", symStr);
				}
			}
#endif
#if !ADDRESS_ON_PC
			if(ea == 0x3B)
			{
				commentBuffer += strlen(commentBuffer);
				sprintf(commentBuffer+strlen(commentBuffer), "$%lx", (signed char)(eWord1 & 0xFF) + opcodeAddr + 2);
			}
#endif
		} else {
			// FULL EXTENSION WORD FORMAT

			int	bs = (eWord1 >> 7) & 1;		// Base Register Suppress 0 = Base Register Added 1 = Base Register Suppressed
			int	is = (eWord1 >> 6) & 1;		// Index Suppress 0 = Evaluate and Add Index Operand 1 = Suppress Index Operand
			int	bdSize = (eWord1 >> 4) & 3;	// Base Displacement Size 00 = Reserved 01 = Null Displacement 10 = Word Displacement 11 = Long Displacement
			int	iis = eWord1 & 7;		// Index/Indirect Selection Indirect and Indexing Operand Determined in Conjunction with Bit 6, Index Suppress
			bool	prefixComma;
			long	bd, od;

			// reserved, has to be 0
			if((eWord1 & 8) != 0 || bdSize == 0 || (is && iis > 3) || iis == 4)
				break;

			// full extension format is only supported on 68020 or later
			if((optionCPUTypeMask & MC_020) == 0)
				return NULL;

			if(ea == 0x3B)
			{
				if((allowedEAs & EA_PCRel) != EA_PCRel)
					break;
			} else {
				if((allowedEAs & EA_dAn) != EA_dAn)
					break;
			}

			bd = 0;
			switch(bdSize)
			{
			case 3: 
				bd = Disass68kGetWord(*addr); *addr += 2;
				bd <<= 16;
			case 2:
				bd |= Disass68kGetWord(*addr); *addr += 2;
				break;
			default:
				break;
			}

			prefixComma = false;
			if(bdSize >= 2 && iis == 0)
				sprintf(disassbuf, "%s", Disass68kNumber(bd));
			strcat(disassbuf, "(");
			if(iis != 0)
			{
				// the CPU32 doesn't support the memory indirect mode
				if(optionCPUTypeMask & MC_CPU32)
					return NULL;

				strcat(disassbuf, "[");
			}
			if(bdSize >= 2 && iis != 0)
			{
				sprintf(disassbuf+strlen(disassbuf), "%s", Disass68kNumber(bd));
				prefixComma = true;
			}
			if(bdSize == 1 && ((bs && is && iis > 0) || (bs && iis >= 5)))
			{
				if(ea == 0x3B)
				{
					sp = Disass68kSpecialRegister(REG_ZPC);
					if(!sp) return NULL;
					strcat(disassbuf, sp);
				} else {
					strcat(disassbuf, "0");
				}
			}
			if(!bs)
			{
				if(prefixComma)
					strcat(disassbuf, ",");
				strcat(disassbuf, regName);
				prefixComma = true;
			}
			if(iis >= 5 && iis <= 7)
			{
				strcat(disassbuf, "]");
				prefixComma = true;
			}
			if(!is)
			{
				if(prefixComma)
					strcat(disassbuf, ",");
				if(scale == 0)
				{
					sprintf(disassbuf+strlen(disassbuf), "%s.%c", Disass68kRegname(xn), c);
				} else
				{
					sprintf(disassbuf+strlen(disassbuf), "%s.%c*%d", Disass68kRegname(xn), c, 1 << scale);
				}
			}
			if(iis >= 1 && iis <= 3)
			{
				strcat(disassbuf, "]");
				prefixComma = true;
			}
			od = 0;
			switch(iis & 3)
			{
			case 3:
				od = Disass68kGetWord(*addr); *addr += 2;
				od <<= 16;
			case 2:
				od |= Disass68kGetWord(*addr); *addr += 2;
				if(prefixComma)
					strcat(disassbuf, ",");
				sprintf(disassbuf+strlen(disassbuf), "%s", Disass68kNumber(od));
				break;
			default:
				break;
			}
			strcat(disassbuf, ")");
		}
		break;

	// M=111 = 7, Xn/reg = 000 = 0
	// Absolute Short Addressing Mode
	// (xxx).W
	case 0x38:
		if((allowedEAs & EA_Abs) != EA_Abs)
			break;
		eWord1 = Disass68kGetWord(*addr); *addr += 2;
		val = eWord1;
		if(eWord1 & 0x8000)
			val |= 0xFFFF0000;
#if USE_SYMBOLS
		sp = Disass68kSymbolName(val, size);
		if(sp)
		{
			if(options & doptNoBrackets)
				sprintf(disassbuf, "%s.w", sp);
			else
				sprintf(disassbuf, "(%s).w", sp);
			break;
		}
#endif
		if(options & doptNoBrackets)
		{
			if(val & 0x80000000)
				sprintf(disassbuf, "$%8.8lx.w", val);
			else
				sprintf(disassbuf, "$%4.4lx.w", val);
		} else {
			if(val & 0x80000000)
				sprintf(disassbuf, "($%8.8lx).w", val);
			else
				sprintf(disassbuf, "($%4.4lx).w", val);
		}
		break;

	// M=111 = 7, Xn/reg = 001 = 1
	// Absolute Long Addressing Mode
	// (xxx).L
	case 0x39:
		if((allowedEAs & EA_Abs) != EA_Abs)
			break;
		eWord1 = Disass68kGetWord(*addr); *addr += 2;
		eWord2 = Disass68kGetWord(*addr); *addr += 2;
#if USE_SYMBOLS
		val = (eWord1 << 16) | eWord2;
		sp = Disass68kSymbolName(val, size);
		if(sp)
		{
			if(options & doptNoBrackets)
				sprintf(disassbuf, "%s", sp);
			else
				sprintf(disassbuf, "(%s).l", sp);
			break;
		}
#endif
		if(options & doptNoBrackets)
			sprintf(disassbuf, "%s", Disass68kNumber((eWord1 << 16) | eWord2));
		else
			sprintf(disassbuf, "(%s).l", Disass68kNumber((eWord1 << 16) | eWord2));
		break;

	// M=111 = 7, Xn/reg = 010 = 2
	// Program Counter Indirect with Displacement Mode
	// (d16,PC)
	case 0x3A:
		if((allowedEAs & EA_PCRel) != EA_PCRel)
			break;
		eWord1 = Disass68kGetWord(*addr); *addr += 2;
		sp = Disass68kSpecialRegister(REG_PC);
		if(!sp) return NULL;
#if ADDRESS_ON_PC
	#if USE_SYMBOLS
		sp = Disass68kSymbolName(((signed short)eWord1 + *addr - 2), size);
		if(sp)
		{
			sprintf(disassbuf, "%s(%s)", sp, Disass68kSpecialRegister(REG_PC));
		} else {
			sprintf(disassbuf, "$%lx(%s)", (signed short)eWord1 + *addr - 2, Disass68kSpecialRegister(REG_PC));
		}
	#else
		sprintf(disassbuf, "$%lx(%s)", (signed short)eWord1 + *addr - 2, Disass68kSpecialRegister(REG_PC));
	#endif
#else
		sprintf(disassbuf, "%s(%s)", Disass68kNumber(eWord1),sp);
		sprintf(commentBuffer+strlen(commentBuffer), "$%lx", (signed short)eWord1 + *addr - 2);
#endif
		break;

	// M=111 = 7, Xn/reg = 100 = 4
	// Immediate Data
	// #<xxx>
	case 0x3C:
		if((allowedEAs & EA_Immed) != EA_Immed)
			break;
		eWord1 = Disass68kGetWord(*addr); *addr += 2;
		goto immed;

	case 0x0100:	// Immediate Value as a parameter
		if((allowedEAs & EA_ImmedParameter) != EA_ImmedParameter)
			break;
		eWord1 = parameterValue;
	immed:
		switch(size)
		{
		case 1: eWord1 &= 0xFF;
		case 2:
#if USE_SYMBOLS
				if(disassFlag)
				{
					val = eWord1;
					if(eWord1 & 0x8000)
						val |= 0xFFFF0000;
					sp = Disass68kSymbolName(val, size);
					if(sp)
					{
						sprintf(disassbuf, "#%s", sp);
						break;
					}
				}
#endif
				sprintf(disassbuf, "#%s", Disass68kNumber(eWord1));
				break;
		case 4: eWord2 = Disass68kGetWord(*addr); *addr += 2;
#if USE_SYMBOLS
				if(disassFlag)
				{
					val = (eWord1 << 16) | eWord2;
					sp = Disass68kSymbolName(val, size);
					if(sp)
					{
						sprintf(disassbuf, "#%s", sp);
						break;
					}
				}
#endif
				sprintf(disassbuf, "#%s", Disass68kNumber((eWord1 << 16) | eWord2));
				break;
		}
		break;

	case 0x0103:
		if((allowedEAs & EA_ValueParameter) != EA_ValueParameter)
			break;
		sprintf(disassbuf, "%d", parameterValue);
		break;

	case 0x0101:	// Special Registers as in the parameter
		if((allowedEAs & EA_SpecialRegister) != EA_SpecialRegister)
			break;
		sp = Disass68kSpecialRegister(parameterValue);
		if(!sp) return NULL;
		strcpy(disassbuf, sp);
		break;

	case 0x0102:	// PC relative jump, like for BRA and friends
		if((allowedEAs & EA_PCDisplacement) != EA_PCDisplacement)
			break;
		pcoffset = 0;
		switch(size)
		{
		case 1: pcoffset = (signed char)parameterValue;
				break;
		case 2: eWord1 = Disass68kGetWord(*addr); *addr += 2;
				pcoffset = (signed short)eWord1;
				pcoffset -= 2;
				break;
		case 4: eWord1 = Disass68kGetWord(*addr); *addr += 2;
				eWord2 = Disass68kGetWord(*addr); *addr += 2;
				pcoffset = (signed int)((eWord1 << 16) | eWord2);
				pcoffset -= 4;
				break;
		}
#if ADDRESS_ON_PC
	#if USE_SYMBOLS
		sp = Disass68kSymbolName((*addr + pcoffset), size);
		if(sp)
		{
			strcat(disassbuf, sp);
		} else {
			sprintf(disassbuf, "$%lx", *addr + pcoffset);
		}
	#else
		sprintf(disassbuf, "$%lx", *addr + pcoffset);
	#endif
#else
		if(pcoffset < 0)
		{
			sprintf(disassbuf, "*-$%lx", -pcoffset - 2);
		} else {
			sprintf(disassbuf, "*+$%lx", pcoffset + 2);
		}
		sprintf(commentBuffer+strlen(commentBuffer), "$%lx", *addr + pcoffset);
#endif
		break;

	default:	// 0x3D..0x3F are reserved
		break;

	}
	if(disassbuf[0] == 0)
		return NULL;
	return disassbuf + strlen(disassbuf);
}

/***
 *	Create a register list for the MOVEM opcode
 ***/
static char	*Disass68kReglist(char *buf, unsigned short reglist)
{
	int bit;
	int lastBit = -99;
	int lastBitStart = -99;
	char	regD = options & doptRegisterSmall ? 'd' : 'D';
	char	regA = options & doptRegisterSmall ? 'a' : 'A';
	for(bit=0; bit<=15; ++bit)
	{
		// bit clear?
		if((reglist & (1 << bit)) == 0)
		{
			// do we have a run? => close it!
			if(lastBitStart >= 0 && lastBitStart != (bit - 1))
			{
				*buf++ = '-';
				*buf++ = ((bit-1) >= 8) ? regA : regD;
				*buf++ = '0' + ((bit-1) & 7);
			}
			lastBitStart = -1;
			continue;
		}
		// reset when switching from D to A
		if(bit == 8 && lastBitStart >= 0)
		{
			*buf++ = '-';
			*buf++ = regD;
			*buf++ = '7';
			lastBit = 0;
			lastBitStart = -99;
		}
		// separate bits, skip runs of bits to merge them later
		if(lastBit >= 0)
		{
			if(lastBit == bit - 1)
			{
				lastBit = bit;
				continue;
			}
			*buf++ = '/';
		}
		*buf++ = (bit >= 8) ? regA : regD;
		*buf++ = '0' + (bit & 7);
		lastBit = bit;
		lastBitStart = bit;
	}
	if(lastBitStart >= 0 && lastBitStart != (bit - 1))
	{
		*buf++ = '-';
		*buf++ = regA;
		*buf++ = '7';
	}
	if(lastBit < 0)
	{
		*buf++ = '0';
	}
	*buf = 0;
	return buf;
}

/***
 *	Flip the bits in an unsigned short, for MOVEM RegList,-(An)
 ***/
static unsigned short	Disass68kFlipBits(unsigned short mask)
{
	unsigned short	retMask = 0;
	int	i;

	for(i=0; i<=15; ++i)
		if(mask & (1 << i))
			retMask |= (1 << (15-i));
	return retMask;
}

/***
 *	Create a register list for the MOVEM opcode
 ***/
static char	*Disass68kFPUReglist(char *buf, unsigned char reglist)
{
	int bit;
	int lastBit = -99;
	int lastBitStart = -99;
	char	regFP1 = options & doptRegisterSmall ? 'f' : 'F';
	char	regFP2 = options & doptRegisterSmall ? 'p' : 'P';
	for(bit=0; bit<=7; ++bit)
	{
		// bit clear?
		if((reglist & (1 << bit)) == 0)
		{
			// do we have a run? => close it!
			if(lastBitStart >= 0 && lastBitStart != (bit - 1))
			{
				*buf++ = '-';
				*buf++ = regFP1;
				*buf++ = regFP2;
				*buf++ = '0' + ((bit-1) & 7);
			}
			lastBitStart = -1;
			continue;
		}
		// separate bits, skip runs of bits to merge them later
		if(lastBit >= 0)
		{
			if(lastBit == bit - 1)
			{
				lastBit = bit;
				continue;
			}
			*buf++ = '/';
		}
		*buf++ = regFP1;
		*buf++ = regFP2;
		*buf++ = '0' + (bit & 7);
		lastBit = bit;
		lastBitStart = bit;
	}
	if(lastBitStart >= 0 && lastBitStart != (bit - 1))
	{
		*buf++ = '-';
		*buf++ = regFP1;
		*buf++ = regFP2;
		*buf++ = '7';
	}
	if(lastBit < 0)
	{
		*buf++ = '0';
	}
	*buf = 0;
	return buf;
}


/***
 *	List of special cases for the operands
 ***/
typedef enum {
	ofNone,
	ofEa,
	ofDn,
	ofAn,
	ofAni,
	ofI,
	ofSpecReg,
	ofSpecExtReg,
	ofD16An,
	ofDestDn,
	ofDestAn,
	ofExtReg,
	ofExtAnip,
	ofExtReg0,
	ofExtRegA0,
	ofExtRegD04,
	ofExtRegA05,
	ofFPUReglist,
	ofFPUSRRegList,
	ofDestEa6,
	ofDestAbsL,
	ofIOpcode,
	ofCAS,
	ofCAS2,
	ofI3,
	ofExtIm,
	ofExtIm32,
	ofExtIm4,
	ofExtIm10,
	ofDisp,
	ofPiAn,
	ofDestPiAn,
	ofAnip,
	ofDestAnip,
	ofBFEa,
	ofRegList,
	ofExt4Dn,
	ofFPU,
	ofFPUMOVE,
	ofFMOVECR,
	ofFPU3Reg,
	ofLineA
} Disass68kOpcodeFormat;


/***
 *	The order of the table is not important (with the exception of some FPU opcodes, which are commented further down),
 *	as each opcode should decline if it doesn't match 100%. The 68k CPU also doesn't do guessing based on the context!
 ***/
typedef const struct {
	int				cpuMask;
	unsigned long	opcodeMask[2*5];
	signed char		operationSize[4];
	char			op[5];
	const char		*opcodeName;
	int				parameter[5];
	int				disassFlag;
} OpcodeTableStruct;

static const OpcodeTableStruct	OpcodeTable[] = {
	{ MC_ALL, {0xff00, 0x0000}, {-1,6,2,0}, {ofI,ofEa}, "ORI.?",{0,EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xf1c0, 0x0100}, {4}, {ofDestDn,ofEa}, "BTST",{0,EA_An|EA_Immed} },
	{ MC_ALL, {0xf1c0, 0x0140}, {4}, {ofDestDn,ofEa}, "BCHG",{0,EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xf1c0, 0x0180}, {4}, {ofDestDn,ofEa}, "BCLR",{0,EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xf1c0, 0x01C0}, {4}, {ofDestDn,ofEa}, "BSET",{0,EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL-MC68060, {0xf1f8, 0x0108}, {2}, {ofD16An,ofDestDn}, "MOVEP.W"},
	{ MC_ALL-MC68060, {0xf1f8, 0x0148}, {4}, {ofD16An,ofDestDn}, "MOVEP.L"},
	{ MC_ALL-MC68060, {0xf1f8, 0x0188}, {2}, {ofDestDn,ofD16An}, "MOVEP.W"},
	{ MC_ALL-MC68060, {0xf1f8, 0x01C8}, {4}, {ofDestDn,ofD16An}, "MOVEP.L"},
	{ MC_ALL, {0xff00, 0x0200}, {-1,6,2,0}, {ofI,ofEa}, "ANDI.?",{0,EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xff00, 0x0400}, {-1,6,2,0}, {ofI,ofEa}, "SUBI.?",{0,EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xff00, 0x0600}, {-1,6,2,0}, {ofI,ofEa}, "ADDI.?",{0,EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xffc0, 0x0800}, {1}, {ofI,ofEa}, "BTST",{0,EA_An|EA_Immed} },
	{ MC_ALL, {0xffc0, 0x0840}, {1}, {ofI,ofEa}, "BCHG",{0,EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xffc0, 0x0880}, {1}, {ofI,ofEa}, "BCLR",{0,EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xffc0, 0x08C0}, {1}, {ofI,ofEa}, "BSET",{0,EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xff00, 0x0A00}, {-1,6,2,0}, {ofI,ofEa}, "EORI.?",{0,EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xff00, 0x0C00}, {-1,6,2,0}, {ofI,ofEa}, "CMPI.?",{0,EA_Immed|EA_An}},
	{ MC_ALL, {0xffff, 0x003C}, {1}, {ofEa,ofSpecReg}, "ORI",{0,REG_CCR} },
	{ MC_ALL, {0xffff, 0x007C}, {2}, {ofEa,ofSpecReg}, "ORI",{0,REG_SR} },
	{ MC_ALL, {0xffff, 0x023C}, {1}, {ofEa,ofSpecReg}, "ANDI",{0,REG_CCR} },
	{ MC_ALL, {0xffff, 0x027C}, {2}, {ofEa,ofSpecReg}, "ANDI",{0,REG_SR} },
	{ MC_ALL, {0xffff, 0x0A3C}, {1}, {ofEa,ofSpecReg}, "EORI",{0,REG_CCR} },
	{ MC_ALL, {0xffff, 0x0A7C}, {2}, {ofEa,ofSpecReg}, "EORI",{0,REG_SR} },
	{ MC68020, {0xffc0, 0x06C0}, {1}, {ofEa}, "CALLM",{EA_Dn|EA_An|EA_Immed|EA_Anip|EA_piAn} },
	{ MC68020, {0xfff0, 0x06C0}, {1}, {ofEa}, "RTM"},
	{ MC_020, {0xf9c0, 0x00C0, 0x0fff,0x0000}, {-1,9,2,0}, {ofEa,ofExtReg}, "CMP2.?",{EA_Dn|EA_An|EA_Immed|EA_Anip|EA_piAn} },
	{ MC_020, {0xf9c0, 0x00C0, 0x0fff,0x0800}, {-1,9,2,0}, {ofEa,ofExtReg}, "CHK2.?",{EA_Dn|EA_An|EA_Immed|EA_Anip|EA_piAn} },
	{ MC_020&~MC_CPU32, {0xffc0, 0x0AC0, 0xFE38,0x0000}, {1}, {ofCAS,ofEa}, "CAS.B",{0,EA_Immed|EA_PCRel|EA_An|EA_Dn}},
	{ MC_020&~MC_CPU32, {0xffc0, 0x0CC0, 0xFE38,0x0000}, {2}, {ofCAS,ofEa}, "CAS.W",{0,EA_Immed|EA_PCRel|EA_An|EA_Dn}},
	{ MC_020&~MC_CPU32, {0xffc0, 0x0EC0, 0xFE38,0x0000}, {4}, {ofCAS,ofEa}, "CAS.L",{0,EA_Immed|EA_PCRel|EA_An|EA_Dn}},
	{ MC_020&~MC_CPU32, {0xffff, 0x0CFC, 0x0E38,0x0000, 0x0E38,0x0000}, {2}, {ofCAS2}, "CAS2.W"},
	{ MC_020&~MC_CPU32, {0xffff, 0x0EFC, 0x0E38,0x0000, 0x0E38,0x0000}, {4}, {ofCAS2}, "CAS2.L"},
	{ MC68010|MC_020, {0xff00, 0x0e00, 0x0fff,0x0000}, {-1,6,2,0}, {ofEa,ofExtReg}, "MOVES.?",{EA_Immed|EA_PCRel|EA_An|EA_Dn,0}},
	{ MC68010|MC_020, {0xff00, 0x0e00, 0x0fff,0x0800}, {-1,6,2,0}, {ofExtReg,ofEa}, "MOVES.?",{0,EA_Immed|EA_PCRel|EA_An|EA_Dn}},

	{ MC_ALL, {0xf000, 0x1000}, {1}, {ofEa,ofDestEa6}, "MOVE.B"},

	{ MC_ALL, {0xf000, 0x2000}, {4}, {ofEa,ofDestEa6}, "MOVE.L"},
	{ MC_ALL, {0xf1c0, 0x2040}, {4}, {ofEa,ofDestAn}, "MOVEA.L",{0},1},

	{ MC_ALL, {0xf000, 0x3000}, {2}, {ofEa,ofDestEa6}, "MOVE.W"},
	{ MC_ALL, {0xf1c0, 0x3040}, {2}, {ofEa,ofDestAn}, "MOVEA.W",{0},1},

	{ MC_ALL, {0xff00, 0x4000}, {-1,6,2,0}, {ofEa}, "NEGX.?",{EA_Immed|EA_PCRel|EA_An}},
	{ MC_020, {0xf1c0, 0x4100}, {4}, {ofEa,ofDestDn}, "CHK.L", {EA_An,0}},
	{ MC_ALL, {0xf1c0, 0x4180}, {2}, {ofEa,ofDestDn}, "CHK.W", {EA_An,0}},
	{ MC_ALL, {0xf1c0, 0x41c0}, {4}, {ofEa,ofDestAn}, "LEA",{EA_Dn|EA_An|EA_Immed|EA_Anip|EA_piAn,0},1 },
	{ MC_ALL, {0xff00, 0x4200}, {-1,6,2,0}, {ofEa}, "CLR.?",{EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xff00, 0x4400}, {-1,6,2,0}, {ofEa}, "NEG.?",{EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xff00, 0x4600}, {-1,6,2,0}, {ofEa}, "NOT.?",{EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xffc0, 0x40c0}, {2}, {ofSpecReg,ofEa}, "MOVE",{REG_SR,EA_Immed|EA_PCRel|EA_An} },
	{ MC_ALL, {0xffc0, 0x42c0}, {1}, {ofSpecReg,ofEa}, "MOVE",{REG_CCR,EA_Immed|EA_PCRel|EA_An} },
	{ MC_ALL, {0xffc0, 0x44c0}, {1}, {ofEa,ofSpecReg}, "MOVE",{EA_An,REG_CCR} },
	{ MC_ALL, {0xffc0, 0x46c0}, {2}, {ofEa,ofSpecReg}, "MOVE",{EA_An,REG_SR} },
	{ MC_ALL, {0xffc0, 0x4800}, {1}, {ofEa}, "NBCD",{EA_Immed|EA_PCRel|EA_An}},
	{ MC_020, {0xfff8, 0x4808}, {4}, {ofEa,ofI}, "LINK.L"},
	{ MC_ALL, {0xffc0, 0x4840}, {0}, {ofEa}, "PEA",{EA_Dn|EA_An|EA_Immed|EA_Anip|EA_piAn},1 },
	{ MC_ALL, {0xfff8, 0x4840}, {4}, {ofEa}, "SWAP"},
	{ MC68010|MC_020, {0xfff8, 0x4848}, {0}, {ofIOpcode}, "BKPT",{0x07} },
	{ MC_ALL, {0xffc0, 0x4880, 0x10000}, {2}, {ofRegList,ofEa}, "MOVEM.W",{0,EA_Dn|EA_An|EA_Immed|EA_Anip|EA_PCRel} },
	{ MC_ALL, {0xffc0, 0x48c0, 0x10000}, {4}, {ofRegList,ofEa}, "MOVEM.L",{0,EA_Dn|EA_An|EA_Immed|EA_Anip|EA_PCRel} },
	{ MC_ALL, {0xfff8, 0x4880}, {2}, {ofEa}, "EXT.W"},
	{ MC_ALL, {0xfff8, 0x48c0}, {4}, {ofEa}, "EXT.L"},
	{ MC_020, {0xfff8, 0x49c0}, {4}, {ofEa}, "EXTB.L"},
	{ MC_ALL, {0xff00, 0x4a00}, {-1,6,2,0}, {ofEa}, "TST.?"},
	{ MC_ALL, {0xffc0, 0x4ac0}, {1}, {ofEa}, "TAS",{EA_Immed|EA_PCRel|EA_An}},
	{ MC_CPU32, {0xffff, 0x4afa}, {0}, {ofNone}, "BGND"},
	{ MC_ALL, {0xffff, 0x4afc}, {0}, {ofNone}, "ILLEGAL"},
	{ MC_020, {0xffc0, 0x4c00, 0x8ff8, 0x0000}, {4}, {ofEa,ofExtReg}, "MULU.L", {EA_An,0}},
	{ MC_020, {0xffc0, 0x4c00, 0x8ff8, 0x0800}, {4}, {ofEa,ofExtReg}, "MULS.L", {EA_An,0}},
	{ MC_020, {0xffc0, 0x4c40, 0x8ff8, 0x0000}, {4}, {ofEa,ofExtReg}, "DIVU.L", {EA_An,0}},
	{ MC_020, {0xffc0, 0x4c40, 0x8ff8, 0x0800}, {4}, {ofEa,ofExtReg}, "DIVS.L", {EA_An,0}},
	{ MC_020, {0xffc0, 0x4c00, 0x8ff8, 0x0400}, {4}, {ofEa,ofExtReg,ofExtReg0}, "MULU.L", {EA_An,0,0}},
	{ MC_020, {0xffc0, 0x4c00, 0x8ff8, 0x0c00}, {4}, {ofEa,ofExtReg,ofExtReg0}, "MULS.L", {EA_An,0,0}},
	{ MC_020, {0xffc0, 0x4c40, 0x8ff8, 0x0400}, {4}, {ofEa,ofExtReg,ofExtReg0}, "DIVU.L", {EA_An,0,0}},
	{ MC_020, {0xffc0, 0x4c40, 0x8ff8, 0x0c00}, {4}, {ofEa,ofExtReg,ofExtReg0}, "DIVS.L", {EA_An,0,0}},
	{ MC_ALL, {0xffc0, 0x4c80, 0x10000}, {2}, {ofEa,ofRegList}, "MOVEM.W",{EA_Dn|EA_An|EA_Immed|EA_piAn,0} },
	{ MC_ALL, {0xffc0, 0x4cc0, 0x10000}, {4}, {ofEa,ofRegList}, "MOVEM.L",{EA_Dn|EA_An|EA_Immed|EA_piAn,0} },
	{ MC_ALL, {0xfff0, 0x4e40}, {0}, {ofIOpcode}, "TRAP",{0x0f} },
	{ MC_ALL, {0xfff8, 0x4e50}, {2}, {ofAn,ofI}, "LINK"},
	{ MC_ALL, {0xfff8, 0x4e58}, {4}, {ofAn}, "UNLK"},
	{ MC_ALL, {0xfff8, 0x4e60}, {4}, {ofAn,ofSpecReg}, "MOVE",{0,REG_USP} },
	{ MC_ALL, {0xfff8, 0x4e68}, {4}, {ofSpecReg,ofAn}, "MOVE",{REG_USP,0} },
	{ MC_ALL, {0xffff, 0x4e70}, {0}, {ofNone}, "RESET"},
	{ MC_ALL, {0xffff, 0x4e71}, {0}, {ofNone}, "NOP"},
	{ MC_ALL, {0xffff, 0x4e72}, {2}, {ofI}, "STOP"},
	{ MC_ALL, {0xffff, 0x4e73}, {0}, {ofNone}, "RTE"},
	{ MC68010|MC_020, {0xffff, 0x4e74}, {2}, {ofI}, "RTD"},
	{ MC_ALL, {0xffff, 0x4e75}, {0}, {ofNone}, "RTS"},
	{ MC_ALL, {0xffff, 0x4e76}, {0}, {ofNone}, "TRAPV"},
	{ MC_ALL, {0xffff, 0x4e77}, {0}, {ofNone}, "RTR"},
	{ MC68010|MC_020, {0xffff, 0x4e7a, 0x10000}, {4}, {ofSpecExtReg,ofExtReg}, "MOVEC"},
	{ MC68010|MC_020, {0xffff, 0x4e7b, 0x10000}, {4}, {ofExtReg,ofSpecExtReg}, "MOVEC"},
	{ MC_ALL, {0xffc0, 0x4e80}, {0}, {ofEa}, "JSR",{EA_Dn|EA_An|EA_Immed|EA_Anip|EA_piAn} },
	{ MC_ALL, {0xffc0, 0x4ec0}, {0}, {ofEa}, "JMP",{EA_Dn|EA_An|EA_Immed|EA_Anip|EA_piAn} },

	{ MC_ALL, {0xf1c0, 0x5000}, {1}, {ofI3,ofEa}, "ADDQ.B",{0,EA_An|EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xf1c0, 0x5040}, {2}, {ofI3,ofEa}, "ADDQ.W",{0,EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xf1c0, 0x5080}, {4}, {ofI3,ofEa}, "ADDQ.L",{0,EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xf0c0, 0x50C0}, {1}, {ofEa}, "Sci",{EA_Immed|EA_PCRel|EA_An}},
	{ MC_ALL, {0xf0f8, 0x50C8}, {2}, {ofDn,ofDisp}, "DBcd"},
	{ MC_020, {0xf0ff, 0x50fa}, {2}, {ofI}, "TRAPci.W"},
	{ MC_020, {0xf0ff, 0x50fb}, {4}, {ofI}, "TRAPci.L"},
	{ MC_020, {0xf0ff, 0x50fc}, {0}, {ofNone}, "TRAPci"},
	{ MC_ALL, {0xf1c0, 0x5100}, {1}, {ofI3,ofEa}, "SUBQ.B",{0,EA_An|EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xf1c0, 0x5140}, {2}, {ofI3,ofEa}, "SUBQ.W",{0,EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xf1c0, 0x5180}, {4}, {ofI3,ofEa}, "SUBQ.L",{0,EA_Immed|EA_PCRel} },

	{ MC_ALL, {0xf0ff, 0x6000}, {2}, {ofDisp}, "Bcb"},
	{ MC_ALL, {0xf000, 0x6000}, {1}, {ofDisp}, "Bcb.S"},
	{ MC_020, {0xf0ff, 0x60FF}, {4}, {ofDisp}, "Bcb.L"},

	{ MC_ALL, {0xf100, 0x7000}, {0}, {ofIOpcode,ofDestDn}, "MOVEQ", {0xFF,0}},

	{ MC_ALL, {0xf100, 0x8000}, {-1,6,2,0}, {ofEa,ofDestDn}, "OR.?", {EA_An,0}},
	{ MC_ALL, {0xf100, 0x8100}, {-1,6,2,0}, {ofDestDn,ofEa}, "OR.?",{0,EA_Immed|EA_PCRel|EA_An|EA_Dn}},
	{ MC_ALL, {0xf1f8, 0x8100}, {1}, {ofDn,ofDestDn}, "SBCD"},
	{ MC_ALL, {0xf1f8, 0x8108}, {1}, {ofPiAn,ofDestPiAn}, "SBCD"},
	{ MC_020&~MC_CPU32, {0xf1f8, 0x8140, 0x10000}, {0}, {ofDn,ofDestDn,ofExtIm}, "PACK"},
	{ MC_020&~MC_CPU32, {0xf1f8, 0x8148, 0x10000}, {0}, {ofPiAn,ofDestPiAn,ofExtIm}, "PACK"},
	{ MC_020&~MC_CPU32, {0xf1f8, 0x8180, 0x10000}, {0}, {ofDn,ofDestDn,ofExtIm}, "UNPK"},
	{ MC_020&~MC_CPU32, {0xf1f8, 0x8188, 0x10000}, {0}, {ofPiAn,ofDestPiAn,ofExtIm}, "UNPK"},
	{ MC_ALL, {0xf1c0, 0x80c0}, {2}, {ofEa,ofDestDn}, "DIVU.W", {EA_An,0}},
	{ MC_ALL, {0xf1c0, 0x81c0}, {2}, {ofEa,ofDestDn}, "DIVS.W", {EA_An,0}},

	{ MC_ALL, {0xf1c0, 0x9000}, {1}, {ofEa,ofDestDn}, "SUB.B", {EA_An,0}},
	{ MC_ALL, {0xf1c0, 0x9040}, {2}, {ofEa,ofDestDn}, "SUB.W"},
	{ MC_ALL, {0xf1c0, 0x9080}, {4}, {ofEa,ofDestDn}, "SUB.L"},
	{ MC_ALL, {0xf1c0, 0x90c0}, {2}, {ofEa,ofDestAn}, "SUBA.W"},
	{ MC_ALL, {0xf1c0, 0x91c0}, {4}, {ofEa,ofDestAn}, "SUBA.L"},
	{ MC_ALL, {0xf100, 0x9100}, {-1,6,2,0}, {ofDestDn,ofEa}, "SUB.?",{0,EA_Immed|EA_PCRel|EA_An|EA_Dn}},
	{ MC_ALL, {0xf138, 0x9100}, {-1,6,2,0}, {ofDn,ofDestDn}, "SUBX.?"},
	{ MC_ALL, {0xf138, 0x9108}, {-1,6,2,0}, {ofPiAn,ofDestPiAn}, "SUBX.?"},

	{ MC_ALL, {0xf000, 0xa000}, {0}, {ofLineA}, "LINEA"},

	{ MC_ALL, {0xf1c0, 0xb000}, {1}, {ofEa,ofDestDn}, "CMP.B", {EA_An,0}},
	{ MC_ALL, {0xf1c0, 0xb040}, {2}, {ofEa,ofDestDn}, "CMP.W"},
	{ MC_ALL, {0xf1c0, 0xb080}, {4}, {ofEa,ofDestDn}, "CMP.L"},
	{ MC_ALL, {0xf1c0, 0xb0c0}, {2}, {ofEa,ofDestAn}, "CMPA.W"},
	{ MC_ALL, {0xf1c0, 0xb1c0}, {4}, {ofEa,ofDestAn}, "CMPA.L"},
	{ MC_ALL, {0xf100, 0xb100}, {-1,6,2,0}, {ofDestDn,ofEa}, "EOR.?",{0,EA_An|EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xf138, 0xb108}, {-1,6,2,0}, {ofAnip,ofDestAnip}, "CMPM.?"},

	{ MC_ALL, {0xf100, 0xc000}, {-1,6,2,0}, {ofEa,ofDestDn}, "AND.?", {EA_An,0}},
	{ MC_ALL, {0xf100, 0xc100}, {-1,6,2,0}, {ofDestDn,ofEa}, "AND.?",{0,EA_Immed|EA_PCRel|EA_An|EA_Dn}},
	{ MC_ALL, {0xf1f8, 0xc100}, {1}, {ofDn,ofDestDn}, "ABCD"},
	{ MC_ALL, {0xf1f8, 0xc108}, {1}, {ofPiAn,ofDestPiAn}, "ABCD"},
	{ MC_ALL, {0xf1f8, 0xc140}, {1}, {ofDestDn,ofDn}, "EXG"},
	{ MC_ALL, {0xf1f8, 0xc148}, {1}, {ofDestAn,ofAn}, "EXG"},
	{ MC_ALL, {0xf1f8, 0xc188}, {1}, {ofDestDn,ofAn}, "EXG"},
	{ MC_ALL, {0xf1c0, 0xc0c0}, {2}, {ofEa,ofDestDn}, "MULU.W", {EA_An,0}},
	{ MC_ALL, {0xf1c0, 0xc1c0}, {2}, {ofEa,ofDestDn}, "MULS.W", {EA_An,0}},

	{ MC_ALL, {0xf1c0, 0xd000}, {1}, {ofEa,ofDestDn}, "ADD.B", {EA_An,0}},
	{ MC_ALL, {0xf1c0, 0xd040}, {2}, {ofEa,ofDestDn}, "ADD.W"},
	{ MC_ALL, {0xf1c0, 0xd080}, {4}, {ofEa,ofDestDn}, "ADD.L"},
	{ MC_ALL, {0xf1c0, 0xd0c0}, {2}, {ofEa,ofDestAn}, "ADDA.W"},
	{ MC_ALL, {0xf1c0, 0xd1c0}, {4}, {ofEa,ofDestAn}, "ADDA.L"},
	{ MC_ALL, {0xf100, 0xd100}, {-1,6,2,0}, {ofDestDn,ofEa}, "ADD.?",{0,EA_Immed|EA_PCRel|EA_An|EA_Dn}},
	{ MC_ALL, {0xf138, 0xd100}, {-1,6,2,0}, {ofDn,ofDestDn}, "ADDX.?"},
	{ MC_ALL, {0xf138, 0xd108}, {-1,6,2,0}, {ofPiAn,ofDestPiAn}, "ADDX.?"},

	{ MC_ALL, {0xf138, 0xe000}, {-1,6,2,0}, {ofI3,ofDn}, "ASR.?"},
	{ MC_ALL, {0xf138, 0xe008}, {-1,6,2,0}, {ofI3,ofDn}, "LSR.?"},
	{ MC_ALL, {0xf138, 0xe010}, {-1,6,2,0}, {ofI3,ofDn}, "ROXR.?"},
	{ MC_ALL, {0xf138, 0xe018}, {-1,6,2,0}, {ofI3,ofDn}, "ROR.?"},
	{ MC_ALL, {0xf138, 0xe020}, {-1,6,2,0}, {ofDestDn,ofDn}, "ASR.?"},
	{ MC_ALL, {0xf138, 0xe028}, {-1,6,2,0}, {ofDestDn,ofDn}, "LSR.?"},
	{ MC_ALL, {0xf138, 0xe030}, {-1,6,2,0}, {ofDestDn,ofDn}, "ROXR.?"},
	{ MC_ALL, {0xf138, 0xe038}, {-1,6,2,0}, {ofDestDn,ofDn}, "ROR.?"},
	{ MC_ALL, {0xf138, 0xe100}, {-1,6,2,0}, {ofI3,ofDn}, "ASL.?"},
	{ MC_ALL, {0xf138, 0xe108}, {-1,6,2,0}, {ofI3,ofDn}, "LSL.?"},
	{ MC_ALL, {0xf138, 0xe110}, {-1,6,2,0}, {ofI3,ofDn}, "ROXL.?"},
	{ MC_ALL, {0xf138, 0xe118}, {-1,6,2,0}, {ofI3,ofDn}, "ROL.?"},
	{ MC_ALL, {0xf138, 0xe120}, {-1,6,2,0}, {ofDestDn,ofDn}, "ASL.?"},
	{ MC_ALL, {0xf138, 0xe128}, {-1,6,2,0}, {ofDestDn,ofDn}, "LSL.?"},
	{ MC_ALL, {0xf138, 0xe130}, {-1,6,2,0}, {ofDestDn,ofDn}, "ROXL.?"},
	{ MC_ALL, {0xf138, 0xe138}, {-1,6,2,0}, {ofDestDn,ofDn}, "ROL.?"},
	{ MC_ALL, {0xffc0, 0xe0c0}, {1}, {ofEa}, "ASR",{EA_Dn|EA_An|EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xffc0, 0xe1c0}, {1}, {ofEa}, "ASL",{EA_Dn|EA_An|EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xffc0, 0xe2c0}, {1}, {ofEa}, "LSR",{EA_Dn|EA_An|EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xffc0, 0xe3c0}, {1}, {ofEa}, "LSL",{EA_Dn|EA_An|EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xffc0, 0xe4c0}, {1}, {ofEa}, "ROXR",{EA_Dn|EA_An|EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xffc0, 0xe5c0}, {1}, {ofEa}, "ROXL",{EA_Dn|EA_An|EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xffc0, 0xe6c0}, {1}, {ofEa}, "ROR",{EA_Dn|EA_An|EA_Immed|EA_PCRel} },
	{ MC_ALL, {0xffc0, 0xe7c0}, {1}, {ofEa}, "ROL",{EA_Dn|EA_An|EA_Immed|EA_PCRel} },
	{ MC_020&~MC_CPU32, {0xffc0, 0xe8c0, 0xf000, 0x0000}, {1}, {ofBFEa}, "BFTST",{EA_An|EA_piAn|EA_Anip|EA_Immed}},
	{ MC_020&~MC_CPU32, {0xffc0, 0xe9c0, 0x8000, 0x0000}, {1}, {ofBFEa,ofExtReg}, "BFEXTU",{EA_An|EA_piAn|EA_Anip|EA_Immed}},
	{ MC_020&~MC_CPU32, {0xffc0, 0xeac0, 0xf000, 0x0000}, {1}, {ofBFEa}, "BFCHG",{EA_An|EA_piAn|EA_Anip|EA_Immed|EA_PCRel} },
	{ MC_020&~MC_CPU32, {0xffc0, 0xebc0, 0x8000, 0x0000}, {1}, {ofBFEa,ofExtReg}, "BFEXTS",{EA_An|EA_piAn|EA_Anip|EA_Immed}},
	{ MC_020&~MC_CPU32, {0xffc0, 0xecc0, 0xf000, 0x0000}, {1}, {ofBFEa}, "BFCLR",{EA_An|EA_piAn|EA_Anip|EA_Immed|EA_PCRel} },
	{ MC_020&~MC_CPU32, {0xffc0, 0xedc0, 0x8000, 0x0000}, {1}, {ofBFEa,ofExtReg}, "BFFFO",{EA_An|EA_piAn|EA_Anip|EA_Immed}},
	{ MC_020&~MC_CPU32, {0xffc0, 0xeec0, 0xf000, 0x0000}, {1}, {ofBFEa}, "BFSET",{EA_An|EA_piAn|EA_Anip|EA_Immed}},
	{ MC_020&~MC_CPU32, {0xffc0, 0xefc0, 0x8000, 0x0000}, {1}, {ofExtReg,ofBFEa}, "BFINS",{0,EA_An|EA_piAn|EA_Anip|EA_Immed|EA_PCRel} },


	#define PMMU_COPROC_ID		0	// 0 is the standard PMMU

	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x2000}, {0}, {ofSpecReg,ofEa}, "PLOADW",{REG_SFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x2001}, {0}, {ofSpecReg,ofEa}, "PLOADW",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xfff8, 0x2008}, {0}, {ofExtReg0,ofEa}, "PLOADW",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xfff0, 0x2010}, {0}, {ofExtIm4,ofEa}, "PLOADW",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },

	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x2200}, {0}, {ofSpecReg,ofEa}, "PLOADR",{REG_SFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x2201}, {0}, {ofSpecReg,ofEa}, "PLOADR",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xfff8, 0x2208}, {0}, {ofExtReg0,ofEa}, "PLOADR",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xfff0, 0x2210}, {0}, {ofExtIm4,ofEa}, "PLOADR",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },

	{ MC_PMMU, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0xa000}, {0}, {ofEa}, "PFLUSHR",{EA_Dn|EA_An} },

	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x0800}, {0}, {ofEa,ofSpecReg}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TT0} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x0900}, {0}, {ofEa,ofSpecReg}, "PMOVEFD",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TT0} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x0B00}, {0}, {ofSpecReg,ofEa}, "PMOVEFD",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TT0} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x0C00}, {0}, {ofEa,ofSpecReg}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TT1} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x0C00}, {0}, {ofSpecReg,ofEa}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TT0} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x0D00}, {0}, {ofEa,ofSpecReg}, "PMOVEFD",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TT1} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x0E00}, {0}, {ofSpecReg,ofEa}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TT1} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x0F00}, {0}, {ofSpecReg,ofEa}, "PMOVEFD",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TT1} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4000}, {0}, {ofEa,ofSpecReg}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TC} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4100}, {0}, {ofEa,ofSpecReg}, "PMOVEFD",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TC} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4200}, {0}, {ofSpecReg,ofEa}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TC} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4300}, {0}, {ofSpecReg,ofEa}, "PMOVEFD",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_TC} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4800}, {0}, {ofEa,ofSpecReg}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_SRP} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4900}, {0}, {ofEa,ofSpecReg}, "PMOVEFD",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_SRP} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4A00}, {0}, {ofSpecReg,ofEa}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_SRP} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4B00}, {0}, {ofSpecReg,ofEa}, "PMOVEFD",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_SRP} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4C00}, {0}, {ofEa,ofSpecReg}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_CRP} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4D00}, {0}, {ofEa,ofSpecReg}, "PMOVEFD",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_CRP} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4e00}, {0}, {ofSpecReg,ofEa}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_CRP} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x4f00}, {0}, {ofSpecReg,ofEa}, "PMOVEFD",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_CRP} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x6000}, {0}, {ofEa,ofSpecReg}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_MMUSR} },
	{ MC_PMMU|MC68030, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x6200}, {0}, {ofSpecReg,ofEa}, "PMOVE",{EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel,REG_MMUSR} },

	{ MC_PMMU, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xffff, 0x2800}, {0}, {ofSpecReg,ofEa}, "PVALID",{REG_VAL,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xfff8, 0x2C00}, {0}, {ofExtRegA0,ofEa}, "PVALID",{0,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },

	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe3ff, 0x8000}, {0}, {ofSpecReg,ofEa,ofExtIm10}, "PTESTW",{REG_SFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe3ff, 0x8001}, {0}, {ofSpecReg,ofEa,ofExtIm10}, "PTESTW",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe3f8, 0x8008}, {0}, {ofExtReg0,ofEa,ofExtIm10}, "PTESTW",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe3f0, 0x8010}, {0}, {ofExtIm4,ofEa,ofExtIm10}, "PTESTW",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },

	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe3ff, 0x8200}, {0}, {ofSpecReg,ofEa,ofExtIm10}, "PTESTR",{REG_SFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe3ff, 0x8201}, {0}, {ofSpecReg,ofEa,ofExtIm10}, "PTESTR",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe3f8, 0x8208}, {0}, {ofExtReg0,ofEa,ofExtIm10}, "PTESTR",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe3f0, 0x8210}, {0}, {ofExtIm4,ofEa,ofExtIm10}, "PTESTR",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },

	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe31f, 0x8100}, {0}, {ofSpecReg,ofEa,ofExtIm10,ofExtRegA05}, "PTESTW",{REG_SFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe31f, 0x8101}, {0}, {ofSpecReg,ofEa,ofExtIm10,ofExtRegA05}, "PTESTW",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe318, 0x8108}, {0}, {ofExtReg0,ofEa,ofExtIm10,ofExtRegA05}, "PTESTW",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe310, 0x8110}, {0}, {ofExtIm4,ofEa,ofExtIm10,ofExtRegA05}, "PTESTW",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },

	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe31f, 0x8300}, {0}, {ofSpecReg,ofEa,ofExtIm10,ofExtRegA05}, "PTESTR",{REG_SFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe31f, 0x8301}, {0}, {ofSpecReg,ofEa,ofExtIm10,ofExtRegA05}, "PTESTR",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe318, 0x8308}, {0}, {ofExtReg0,ofEa,ofExtIm10,ofExtRegA05}, "PTESTR",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },
	{ MC_PMMU|MC68030|MC68040|MC68LC040, {0xffc0, 0xf000|(PMMU_COPROC_ID<<9), 0xe310, 0x8310}, {0}, {ofExtIm4,ofEa,ofExtIm10,ofExtRegA05}, "PTESTR",{REG_DFC,EA_Dn|EA_An|EA_Anip|EA_piAn|EA_Immed|EA_PCRel} },

	{ MC_PMMU, {0xffc0, 0xf040|(PMMU_COPROC_ID<<9), 0xfff0, 0x8310}, {0}, {ofEa}, "PScp",{EA_An|EA_Immed|EA_PCRel} },
	{ MC_PMMU, {0xfff8, 0xf048|(PMMU_COPROC_ID<<9), 0xfff0, 0x0000}, {2}, {ofDn,ofDisp}, "PDBcp"},
	{ MC_PMMU, {0xffff, 0xf07A|(PMMU_COPROC_ID<<9), 0xfff0, 0x0000, 0x10000,0x0000}, {2}, {ofExtIm32}, "PTRAPcp.W" },
	{ MC_PMMU, {0xffff, 0xf07B|(PMMU_COPROC_ID<<9), 0xfff0, 0x0000, 0x10000,0x0000}, {4}, {ofExtIm32}, "PTRAPcp.L" },
	{ MC_PMMU, {0xffff, 0xf07C|(PMMU_COPROC_ID<<9), 0xfff0, 0x0000}, {0}, {ofNone}, "PTRAPcp" },
	{ MC_PMMU, {0xfff0, 0xf080|(PMMU_COPROC_ID<<9)}, {2}, {ofDisp}, "PBcp.W"},
	{ MC_PMMU, {0xfff0, 0xf0C0|(PMMU_COPROC_ID<<9)}, {4}, {ofDisp}, "PBcp.L"},
	{ MC_PMMU, {0xffc0, 0xf100|(PMMU_COPROC_ID<<9)}, {0}, {ofEa}, "PSAVE",{EA_Dn|EA_An|EA_Anip|EA_Immed} },
	{ MC_PMMU, {0xffc0, 0xf140|(PMMU_COPROC_ID<<9)}, {0}, {ofEa}, "PRESTORE",{EA_Dn|EA_An|EA_piAn|EA_Immed} },


	#define MC040_COPROC_ID		3	// 3 is the code for some 68040/68060 opcodes

	{ MC68040|MC68060, {0xfff8, 0xf000|(MC040_COPROC_ID<<9), 0x8fff, 0x8000}, {0}, {ofAnip,ofDestAbsL}, "MOVE16"},
	{ MC68040|MC68060, {0xfff8, 0xf008|(MC040_COPROC_ID<<9), 0x8fff, 0x8000}, {0}, {ofDestAbsL,ofAnip}, "MOVE16"},
	{ MC68040|MC68060, {0xfff8, 0xf010|(MC040_COPROC_ID<<9), 0x8fff, 0x8000}, {0}, {ofAni,ofDestAbsL}, "MOVE16"},
	{ MC68040|MC68060, {0xfff8, 0xf018|(MC040_COPROC_ID<<9), 0x8fff, 0x8000}, {0}, {ofDestAbsL,ofAni}, "MOVE16"},
	{ MC68040|MC68060, {0xfff8, 0xf020|(MC040_COPROC_ID<<9), 0x8fff, 0x8000}, {0}, {ofAnip,ofExtAnip}, "MOVE16"},


	#define CPU32_COPROC_ID		4	// 4 is the code for some CPU32 opcodes

	{ MC68040|MC68060, {0xfff8, 0xf008|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVL",{REG_CACHES_NONE} },
	{ MC68040|MC68060, {0xfff8, 0xf048|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVL",{REG_CACHES_DC} },
	{ MC68040|MC68060, {0xfff8, 0xf088|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVL",{REG_CACHES_IC} },
	{ MC68040|MC68060, {0xfff8, 0xf0C8|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVL",{REG_CACHES_ICDC} },

	{ MC68040|MC68060, {0xfff8, 0xf010|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVP",{REG_CACHES_NONE} },
	{ MC68040|MC68060, {0xfff8, 0xf050|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVP",{REG_CACHES_DC} },
	{ MC68040|MC68060, {0xfff8, 0xf090|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVP",{REG_CACHES_IC} },
	{ MC68040|MC68060, {0xfff8, 0xf0D0|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVP",{REG_CACHES_ICDC} },

	{ MC68040|MC68060, {0xfff8, 0xf018|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVA",{REG_CACHES_NONE} },
	{ MC68040|MC68060, {0xfff8, 0xf058|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVA",{REG_CACHES_DC} },
	{ MC68040|MC68060, {0xfff8, 0xf098|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVA",{REG_CACHES_IC} },
	{ MC68040|MC68060, {0xfff8, 0xf0D8|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CINVA",{REG_CACHES_ICDC} },

	{ MC68040|MC68060, {0xfff8, 0xf028|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHL",{REG_CACHES_NONE} },
	{ MC68040|MC68060, {0xfff8, 0xf068|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHL",{REG_CACHES_DC} },
	{ MC68040|MC68060, {0xfff8, 0xf0A8|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHL",{REG_CACHES_IC} },
	{ MC68040|MC68060, {0xfff8, 0xf0E8|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHL",{REG_CACHES_ICDC} },

	{ MC68040|MC68060, {0xfff8, 0xf030|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHP",{REG_CACHES_NONE} },
	{ MC68040|MC68060, {0xfff8, 0xf070|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHP",{REG_CACHES_DC} },
	{ MC68040|MC68060, {0xfff8, 0xf0B0|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHP",{REG_CACHES_IC} },
	{ MC68040|MC68060, {0xfff8, 0xf0F0|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHP",{REG_CACHES_ICDC} },

	{ MC68040|MC68060, {0xfff8, 0xf038|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHA",{REG_CACHES_NONE} },
	{ MC68040|MC68060, {0xfff8, 0xf078|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHA",{REG_CACHES_DC} },
	{ MC68040|MC68060, {0xfff8, 0xf0B8|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHA",{REG_CACHES_IC} },
	{ MC68040|MC68060, {0xfff8, 0xf0F8|(CPU32_COPROC_ID<<9)}, {0}, {ofSpecReg,ofAn}, "CPUSHA",{REG_CACHES_ICDC} },

	{ MC_CPU32, {0xffc0, 0xf000|(CPU32_COPROC_ID<<9), 0x8f08, 0x0100}, {-1,16+6,2,0}, {ofExt4Dn}, "TBLU.?" },
	{ MC_CPU32, {0xffc0, 0xf000|(CPU32_COPROC_ID<<9), 0x8f3f, 0x0100}, {-1,16+6,2,0}, {ofExtReg,ofEa}, "TBLU.?",{EA_An|EA_An|EA_Anip|EA_Immed|EA_PCRel} },
	{ MC_CPU32, {0xffc0, 0xf000|(CPU32_COPROC_ID<<9), 0x8f28, 0x0500}, {-1,16+6,2,0}, {ofExt4Dn}, "TBLUN.?" },
	{ MC_CPU32, {0xffc0, 0xf000|(CPU32_COPROC_ID<<9), 0x8f3f, 0x0500}, {-1,16+6,2,0}, {ofExtReg,ofEa}, "TBLUN.?",{EA_An|EA_An|EA_Anip|EA_Immed|EA_PCRel} },

	{ MC_CPU32, {0xffc0, 0xf000|(CPU32_COPROC_ID<<9), 0x8f08, 0x0900}, {-1,16+6,2,0}, {ofExt4Dn}, "TBLS.?" },
	{ MC_CPU32, {0xffc0, 0xf000|(CPU32_COPROC_ID<<9), 0x8f3f, 0x0900}, {-1,16+6,2,0}, {ofExtReg,ofEa}, "TBLS.?",{EA_An|EA_An|EA_Anip|EA_Immed|EA_PCRel} },
	{ MC_CPU32, {0xffc0, 0xf000|(CPU32_COPROC_ID<<9), 0x8f28, 0x0D00}, {-1,16+6,2,0}, {ofExt4Dn}, "TBLSN.?" },
	{ MC_CPU32, {0xffc0, 0xf000|(CPU32_COPROC_ID<<9), 0x8f3f, 0x0D00}, {-1,16+6,2,0}, {ofExtReg,ofEa}, "TBLSN.?",{EA_An|EA_An|EA_Anip|EA_Immed|EA_PCRel} },

	{ MC_CPU32, {0xffff, 0xf000|(CPU32_COPROC_ID<<9), 0xffff, 0x01C0}, {2}, {ofI}, "LPSTOP" },


	#define FPU_COPROC_ID		1	// 1 is the standard FPU, required to be 1 for the 68040 anyway

	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0000}, {-1,16+10,3,1}, {ofFPU}, "FMOVE.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0001}, {-1,16+10,3,1}, {ofFPU}, "FINT.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0002}, {-1,16+10,3,1}, {ofFPU}, "FSINH.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0003}, {-1,16+10,3,1}, {ofFPU}, "FINTRZ.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0004}, {-1,16+10,3,1}, {ofFPU}, "FSQRT.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0006}, {-1,16+10,3,1}, {ofFPU}, "FLOGNP1.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0008}, {-1,16+10,3,1}, {ofFPU}, "FETOXM1.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0009}, {-1,16+10,3,1}, {ofFPU}, "FTANH.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x000A}, {-1,16+10,3,1}, {ofFPU}, "FATAN.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x000C}, {-1,16+10,3,1}, {ofFPU}, "FASIN.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x000D}, {-1,16+10,3,1}, {ofFPU}, "FATANH.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x000E}, {-1,16+10,3,1}, {ofFPU}, "FSIN.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x000F}, {-1,16+10,3,1}, {ofFPU}, "FTAN.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0010}, {-1,16+10,3,1}, {ofFPU}, "FETOX.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0011}, {-1,16+10,3,1}, {ofFPU}, "FTWOTOX.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0012}, {-1,16+10,3,1}, {ofFPU}, "FTENTOX.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0014}, {-1,16+10,3,1}, {ofFPU}, "FLOGN.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0015}, {-1,16+10,3,1}, {ofFPU}, "FLOG10.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0016}, {-1,16+10,3,1}, {ofFPU}, "FLOG2.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0018}, {-1,16+10,3,1}, {ofFPU}, "FABS.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0019}, {-1,16+10,3,1}, {ofFPU}, "FCOSH.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x001A}, {-1,16+10,3,1}, {ofFPU}, "FNEG.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x001C}, {-1,16+10,3,1}, {ofFPU}, "FACOS.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x001D}, {-1,16+10,3,1}, {ofFPU}, "FCOS.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x001E}, {-1,16+10,3,1}, {ofFPU}, "FGETEXP.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x001F}, {-1,16+10,3,1}, {ofFPU}, "FGETMAN.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0020}, {-1,16+10,3,1}, {ofFPU}, "FDIV.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0021}, {-1,16+10,3,1}, {ofFPU}, "FMOD.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0022}, {-1,16+10,3,1}, {ofFPU}, "FADD.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0023}, {-1,16+10,3,1}, {ofFPU}, "FMUL.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0024}, {-1,16+10,3,1}, {ofFPU}, "FSGLDIV.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0025}, {-1,16+10,3,1}, {ofFPU}, "FREM.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0026}, {-1,16+10,3,1}, {ofFPU}, "FSCALE.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0027}, {-1,16+10,3,1}, {ofFPU}, "FSGLMUL.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0028}, {-1,16+10,3,1}, {ofFPU}, "FSUB.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA078,0x0030}, {-1,16+10,3,1}, {ofFPU3Reg}, "FSINCOS.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0038}, {-1,16+10,3,1}, {ofFPU}, "FCMP.?" },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x003A}, {-1,16+10,3,1}, {ofFPU}, "FTST.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0040}, {-1,16+10,3,1}, {ofFPU}, "FSMOVE.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0041}, {-1,16+10,3,1}, {ofFPU}, "FSSQRT.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0042}, {-1,16+10,3,1}, {ofFPU}, "FSADD.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0044}, {-1,16+10,3,1}, {ofFPU}, "FDMOVE.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0045}, {-1,16+10,3,1}, {ofFPU}, "FDSQRT.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0046}, {-1,16+10,3,1}, {ofFPU}, "FDADD.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0058}, {-1,16+10,3,1}, {ofFPU}, "FSABS.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x005A}, {-1,16+10,3,1}, {ofFPU}, "FSNEG.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x005C}, {-1,16+10,3,1}, {ofFPU}, "FDABS.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x005E}, {-1,16+10,3,1}, {ofFPU}, "FDNEG.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0060}, {-1,16+10,3,1}, {ofFPU}, "FSDIV.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0063}, {-1,16+10,3,1}, {ofFPU}, "FSMUL.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0064}, {-1,16+10,3,1}, {ofFPU}, "FDDIV.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0067}, {-1,16+10,3,1}, {ofFPU}, "FDMUL.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x0068}, {-1,16+10,3,1}, {ofFPU}, "FSSUB.?" },
	{ MC68040,        {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xA07F,0x006C}, {-1,16+10,3,1}, {ofFPU}, "FDSUB.?" },
	{ MC68040|MC_FPU, {0xffff, 0xf000|(FPU_COPROC_ID<<9),0xFC00,0x5C00}, {0}, {ofFMOVECR}, "FMOVECR" },

	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xE000,0x6000}, {-1,16+10,3,1}, {ofFPUMOVE}, "FMOVE.?" },

	// these 3 are special versions of MOVEM with just one register, they have to be before the FMOVEM version
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xFFFF,0x8400}, {0}, {ofEa,ofSpecReg}, "FMOVE", {0,REG_FPU_FPIAR} },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xFFFF,0x8800}, {0}, {ofEa,ofSpecReg}, "FMOVE", {EA_An,REG_FPU_FPSR} },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xFFFF,0x9000}, {0}, {ofEa,ofSpecReg}, "FMOVE", {EA_An,REG_FPU_FPCR} },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xE3FF,0x8000}, {0}, {ofEa,ofFPUSRRegList}, "FMOVEM", {EA_Dn|EA_An,0} },
	// these 3 are special versions of MOVEM with just one register, they have to be before the FMOVEM version
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xFFFF,0xA400}, {0}, {ofSpecReg,ofEa}, "FMOVE", {REG_FPU_FPIAR,EA_Immed|EA_PCRel} },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xFFFF,0xA800}, {0}, {ofSpecReg,ofEa}, "FMOVE", {REG_FPU_FPSR,EA_An|EA_Immed|EA_PCRel} },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xFFFF,0xB000}, {0}, {ofSpecReg,ofEa}, "FMOVE", {REG_FPU_FPCR,EA_An|EA_Immed|EA_PCRel} },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xE3FF,0xA000}, {0}, {ofFPUSRRegList,ofEa}, "FMOVEM", {0,EA_Dn|EA_An|EA_Immed|EA_PCRel} },

	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xFE00,0xC000}, {0}, {ofFPUReglist,ofEa}, "FMOVEM.X",{0,EA_Dn|EA_An|EA_Anip|EA_Immed} },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xFE8F,0xC800}, {0}, {ofExtRegD04,ofEa}, "FMOVEM.X",{0,EA_Dn|EA_An|EA_piAn|EA_Immed} },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xFE00,0xE000}, {0}, {ofEa,ofFPUReglist}, "FMOVEM.X",{EA_Dn|EA_An|EA_piAn|EA_Immed,0} },
	{ MC68040|MC_FPU, {0xffc0, 0xf000|(FPU_COPROC_ID<<9),0xFE8F,0xE800}, {0}, {ofEa,ofExtRegD04}, "FMOVEM.X",{EA_Dn|EA_An|EA_Anip|EA_Immed|EA_PCRel} },

	{ MC68040|MC_FPU, {0xffc0, 0xf040|(FPU_COPROC_ID<<9),0xFFC0,0x0000}, {0}, {ofEa}, "FScf.B",{EA_An|EA_Immed|EA_PCRel} },
	{ MC68040|MC_FPU, {0xfff8, 0xf048|(FPU_COPROC_ID<<9),0xFFC0,0x0000}, {2}, {ofDn,ofDisp}, "FDBcf" },
	{ MC68040|MC_FPU, {0xffff, 0xf07A|(FPU_COPROC_ID<<9), 0xfff0, 0x0000, 0x10000,0x0000}, {2}, {ofExtIm32}, "FTRAPcf.W" },
	{ MC68040|MC_FPU, {0xffff, 0xf07B|(FPU_COPROC_ID<<9), 0xfff0, 0x0000, 0x10000,0x0000}, {4}, {ofExtIm32}, "FTRAPcf.L" },
	{ MC68040|MC_FPU, {0xffff, 0xf07C|(FPU_COPROC_ID<<9), 0xfff0, 0x0000}, {0}, {ofNone}, "FTRAPcf" },

	// FNOP _has_ to be before FBcf.W, not worth to have a special case for that one
	{ MC68040|MC_FPU, {0xffff, 0xf080|(FPU_COPROC_ID<<9),0xFFFF,0x0000}, {0}, {ofNone}, "FNOP" },
	{ MC68040|MC_FPU, {0xffc0, 0xf080|(FPU_COPROC_ID<<9),0xFFFF,0x0000}, {2}, {ofDisp}, "FBcF.W" },
	{ MC68040|MC_FPU, {0xffc0, 0xf0c0|(FPU_COPROC_ID<<9),0xFFFF,0x0000}, {4}, {ofDisp}, "FBcF.L" },
	{ MC68040|MC68060|MC_FPU, {0xffc0, 0xf100|(FPU_COPROC_ID<<9)}, {0}, {ofEa}, "FSAVE", {EA_Dn|EA_An|EA_piAn|EA_Immed} },
	{ MC68040|MC68060|MC_FPU, {0xffc0, 0xf140|(FPU_COPROC_ID<<9)}, {0}, {ofEa}, "FRESTORE", {EA_Dn|EA_An|EA_piAn|EA_Immed} },

	{ 0 }
};

int	Disass68k(long addr, char *labelBuffer, char *opcodeBuffer, char *operandBuffer, char *commentBuffer)
{
	long	baseAddr = addr;
	int		val;
	int		i;
	int		count = 0;
	char	addressLabel[256];
	char	cmtBuffer[256];
	Disass68kDataType	type;
	int	index;
	long	opcodeAddr;

	labelBuffer[0] = 0;
	opcodeBuffer[0] = 0;
	operandBuffer[0] = 0;
	commentBuffer[0] = 0;

	type = Disass68kType(baseAddr, addressLabel, cmtBuffer, &count);
	if(addressLabel[0])
		sprintf(labelBuffer, "%s:", addressLabel);
	sprintf(commentBuffer, "%s", cmtBuffer);
	switch(type)
	{
	case dtByte:
		if(count > 8)
			count = 8;
		strcpy(opcodeBuffer,"DC.B");
		for (i = 0; i < count; ++i)
		{
			char	hbuf[16];
			unsigned short	val;

			if((i & 7) > 0)
				strcat(operandBuffer, ",");
			val = Disass68kGetWord(addr+(i & ~1));
			if(i & 1)
				val &= 0xFF;
			else
				val = val >> 8;
			sprintf(hbuf,"$%2.2x", val);
			strcat(operandBuffer, hbuf);
		}
		return count;

	case dtWord:
		if(count > 4)
			count = 4;
		strcpy(opcodeBuffer,"DC.W");
		for (i = 0; i < count; ++i)
		{
			char	hbuf[16];
			if((i & 3) > 0)
				strcat(operandBuffer, ",");
			sprintf(hbuf,"$%4.4x", Disass68kGetWord(addr+i*2));
			strcat(operandBuffer, hbuf);
		}
		return count * 2;

	case dtLong:
		if(count > 2)
			count = 2;
		strcpy(opcodeBuffer,"DC.L");
		for (i = 0; i < count; ++i)
		{
			char	hbuf[16];
			if((i & 1) > 0)
				strcat(operandBuffer, ",");
			sprintf(hbuf,"$%8.8x", (Disass68kGetWord(addr+i*4) << 16) | Disass68kGetWord(addr+i*4+2));
			strcat(operandBuffer, hbuf);
		}
		return count * 4;

	case dtStringArray:
	{
		char	*sp;
		strcpy(opcodeBuffer,"DC.B");
		strcat(operandBuffer, "'");
		sp = operandBuffer + strlen(operandBuffer);
		for (i = 0; i < count; ++i)
		{
			unsigned short	val = Disass68kGetWord(addr+(i & ~1));
			if(i & 1)
				val &= 0xFF;
			else
				val = val >> 8;
			if(val == 0)
				break;
			switch(val)
			{
			case 9: *sp++ = '\\'; *sp++ = 't'; break;
			case 10: *sp++ = '\\'; *sp++ = 'n'; break;
			case 13: *sp++ = '\\'; *sp++ = 'r'; break;
			default:
				if(val >= 0x20 && val <= 0x7E)
					*sp++ = val;
			}
		}
		*sp = 0;
		strcat(sp, "'");
		return count;
	}

	case dtASCString:
	{
		int	count = 1;
		unsigned short	val = Disass68kGetWord(addr+0);
		strcpy(opcodeBuffer,"DC.B");
		if((val >> 8) == 0)
		{
			strcat(operandBuffer, "0");
		} else {
			char *sp;
			strcat(operandBuffer, "'");
			sp = operandBuffer + strlen(operandBuffer);
			for(i=0; ; ++i)
			{
				unsigned short	val = Disass68kGetWord(addr+(i & ~1));
				if(i & 1)
					val &= 0xFF;
				else
					val = val >> 8;
				if(val == 0)
					break;
				switch(val)
				{
				case 9: *sp++ = '\\'; *sp++ = 't'; break;
				case 10: *sp++ = '\\'; *sp++ = 'n'; break;
				case 13: *sp++ = '\\'; *sp++ = 'r'; break;
				default:
					if(val >= 0x20 && val <= 0x7E)
						*sp++ = val;
				}
				++count;
			}
			*sp = 0;
			strcat(sp, "',0");
		}
		return (count + 1) & ~1;
	}

	case dtPointer:
	case dtFunctionPointer:
	{
		const char	*sp;
		val = (Disass68kGetWord(addr) << 16) | Disass68kGetWord(addr+2);
		sp = Disass68kSymbolName(val, 2);
		strcpy(opcodeBuffer,"DC.L");
		if(sp)
			sprintf(operandBuffer,"%s", sp);
		else
			sprintf(operandBuffer,"$%6.6x", val);
		return 4;
	}

	default:	break;
	}

	index = 0;
	opcodeAddr = addr;
more:
	addr = opcodeAddr;

	opcodeBuffer[0] = 0;
	operandBuffer[0] = 0;

	commentBuffer[0] = 0;
	if(cmtBuffer[0])
		sprintf(commentBuffer, "%s ", cmtBuffer);

	while(1)
	{
		unsigned short	opcode[5];
		unsigned int	i;
		OpcodeTableStruct	*ots = &OpcodeTable[index++];
		int	size;
		char	sizeChar = 0;
		char	*dbuf;
		int	ea;
		unsigned int	maxop;

		if(ots->opcodeName == NULL)
			break;
		if((ots->cpuMask & optionCPUTypeMask) == 0)	// CPU doesn't match?
			continue;

		// search for the opcode plus up to 2 extension words
		for(i=0; i<5; ++i)
		{
			if(!ots->opcodeMask[i*2])
			{
				opcode[i] = 0;
				break;
			}
			opcode[i] = Disass68kGetWord(addr);
			if(((ots->opcodeMask[i*2] & 0xFFFF) & opcode[i]) != ots->opcodeMask[i*2+1])
				goto more;
			addr += 2;
		}

		// find out the size of the opcode operand
		size = ots->operationSize[0];
		if(size < 0)	// custom size?
		{
			int	opcodeOffset = ots->operationSize[1] >> 4;
			int	bitShiftOffset = ots->operationSize[1] & 0x0F;
			int	sizeBitMask = (opcode[opcodeOffset] >> bitShiftOffset) & ((1 << ots->operationSize[2]) - 1);
			switch(ots->operationSize[3])
			{
			case 0:	// 2 Bit Size
					switch(sizeBitMask)
					{
					case 0:	size = 1; sizeChar = 'B'; break;
					case 1:	size = 2; sizeChar = 'W'; break;
					case 2:	size = 4; sizeChar = 'L'; break;
					case 3: goto more;	// illegal size mask
					}
					break;
			case 1:	// 3 Bit FPU Size
					if((opcode[1] & 0x4000) == 0x0000)	// Register => Register?
						sizeBitMask = 2;		// => 'X' Format
					switch(sizeBitMask)
					{
					case 0:	size = 4; sizeChar = 'L'; break;
					case 1:	size = 4; sizeChar = 'S'; break;
					case 2:	size = 12; sizeChar = 'X'; break;
					case 7: if((opcode[1] & 0xE000) != 0x6000)	// MOVE.P <ea>,FPn{Dn-Factor}
								goto more;	// illegal size mask
					case 3:	size = 12; sizeChar = 'P'; break;
					case 4:	size = 2; sizeChar = 'W'; break;
					case 5:	size = 8; sizeChar = 'D'; break;
					case 6:	size = 1; sizeChar = 'B'; break;
					}
					break;
			}
		}

		// copy the opcode plus a necessary TAB for the operand
		dbuf = opcodeBuffer;
		for(i=0; ots->opcodeName[i]; ++i)
		{
			char	c = ots->opcodeName[i];
			if(c == 'c')	// condition code
			{
				static const char	*pmmuCond[16] = { "BS", "BC", "LS", "LC",  "SS", "SC", "AS", "AC",  "WS", "WC", "IS", "IC",  "GS", "GC", "CS", "CC" };
				static const char	*braCond[16]  = { "RA", "SR", "HI", "LS",  "CC", "CS", "NE", "EQ",  "VC", "VS", "PL", "MI",  "GE", "LT", "GT", "LE" };
				static const char	*sccCond[16]  = {  "T",  "F", "HI", "LS",  "CC", "CS", "NE", "EQ",  "VC", "VS", "PL", "MI",  "GE", "LT", "GT", "LE" };
				static const char	*dbCond[16]   = {  "T", "RA", "HI", "LS",  "CC", "CS", "NE", "EQ",  "VC", "VS", "PL", "MI",  "GE", "LT", "GT", "LE" };
				static const char	*fpuCond[64]  = { "F", "EQ", "OGT", "OGE", "OLT", "OLE", "OGL", "OR", "UN", "UEQ", "UGT", "UGE", "ULT", "ULE", "NE", "T", "SF", "SEQ", "GT", "GE", "LT", "LE", "GL", "GLE", "NGLE", "NGL", "NLE", "NLT", "NGE", "NGT", "SNE", "ST" };
				char	buf[8];

				const char	*sp = NULL;
				switch(ots->opcodeName[++i])
				{
				case 'p':	// PMMU conditions
					sp = pmmuCond[opcode[1] & 0xF];
					break;
				case 'b':	// BRA conditions
					sp = braCond[(opcode[0] >> 8) & 0xF];
					break;
				case 'i':	// Scc,TRAPcc conditions
					sp = sccCond[(opcode[0] >> 8) & 0xF];
					break;
				case 'd':	// DBcc conditions
					sp = dbCond[(opcode[0] >> 8) & 0xF];
					break;
				case 'F':	// FPU conditions (first word)
					sp = fpuCond[opcode[0] & 0x3F];
					break;
				case 'f':	// FPU conditions (second word)
					sp = fpuCond[opcode[1] & 0x3F];
					break;
				}
				if(sp)
				{
					if(options & doptOpcodesSmall)
					{
						char	*bp;
						strcpy(buf, sp);
						sp = buf;
						for (bp = buf; *bp; ++bp)
							*bp = tolower((unsigned char)*bp);
					}
					strcpy(dbuf, sp);
					dbuf += strlen(sp);
					continue;
				}
				goto more;
			}
			if(c == '?')	// size mask
				c = sizeChar;
			if(options & doptOpcodesSmall)
				c = tolower((unsigned char)c);
			*dbuf++ = c;
		}
		*dbuf = 0;

		// Parse the EAs for all operands
		ea = opcode[0] & 0x3F;
		dbuf = operandBuffer;

		maxop=(sizeof(ots->op)/sizeof(ots->op[0]));
		for(i=0; i<maxop; ++i)
		{
			int reg;

			switch(ots->op[i])
			{
			case ofNone:		// nothing
					break;

			case ofEa:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ea, size, EA_All & ~(ots->parameter[i]), 0, ots->disassFlag);
					break;

			case ofDn:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (ea & 7) | 0x00, size, EA_Dn, 0, ots->disassFlag);
					break;
			case ofAn:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (ea & 7) | 0x08, size, EA_An, 0, ots->disassFlag);
					break;
			case ofAni:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (ea & 7) | 0x10, size, EA_Ani, 0, ots->disassFlag);
					break;
			case ofAnip:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (ea & 7) | 0x18, size, EA_Anip, 0, ots->disassFlag);
					break;
			case ofPiAn:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (ea & 7) | 0x20, size, EA_piAn, 0, ots->disassFlag);
					break;
			case ofD16An:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (ea & 7) | 0x28, size, EA_dAn, 0, ots->disassFlag);
					break;

			case ofI:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x3C, size, EA_Immed, 0, ots->disassFlag);
					break;

			case ofDestDn:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[0] >> 9) & 7) | 0x00, size, EA_Dn, 0, ots->disassFlag);
					break;
			case ofDestAn:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[0] >> 9) & 7) | 0x08, size, EA_An, 0, ots->disassFlag);
					break;
			case ofDestAnip:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[0] >> 9) & 7) | 0x18, size, EA_Anip, 0, ots->disassFlag);
					break;
			case ofDestPiAn:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[0] >> 9) & 7) | 0x20, size, EA_piAn, 0, ots->disassFlag);
					break;
			case ofDestEa6:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[0] >> 9) & 7) | (((opcode[0] >> 6) & 0x7) << 3), size, EA_Dest-EA_An, 0, ots->disassFlag);
					break;
			case ofDestAbsL:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x39, size, EA_Abs, 0, ots->disassFlag);
					break;

			case ofIOpcode:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0100, 1, EA_ImmedParameter, opcode[0] & ots->parameter[i], ots->disassFlag);
					break;
			case ofI3:
					val = ((opcode[0] >> 9) & 7);
					if(!val) val = 8;
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0100, 1, EA_ImmedParameter, val, ots->disassFlag);
					break;
			case ofExtIm:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0100, 2, EA_ImmedParameter, opcode[1], ots->disassFlag);
					break;
			case ofExtIm32:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0100, size, EA_ImmedParameter, opcode[2], ots->disassFlag);
					break;
			case ofExtIm4:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0100, 2, EA_ImmedParameter, opcode[1] & 0x0F, ots->disassFlag);
					break;
			case ofExtIm10:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0100, 2, EA_ImmedParameter, (opcode[1] >> 10) & 0x07, ots->disassFlag);
					break;
			case ofSpecReg:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0101, size, EA_SpecialRegister, ots->parameter[i], ots->disassFlag);
					break;
			case ofSpecExtReg:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0101, size, EA_SpecialRegister, opcode[1] & 0xFFF, ots->disassFlag);
					break;
			case ofExtReg0:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (opcode[1] & 0x07), size, EA_Dn, 0, ots->disassFlag);
					break;
			case ofExtRegA0:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (opcode[1] & 0x07) | 0x08, size, EA_An, 0, ots->disassFlag);
					break;
			case ofExtRegD04:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[1] >> 4) & 0x07) | 0x00, size, EA_Dn, 0, ots->disassFlag);
					break;
			case ofExtRegA05:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[1] >> 5) & 0x07) | 0x08, size, EA_An, 0, ots->disassFlag);
					break;
			case ofExtReg:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[1] >> 12) & 0x0F), size, EA_Dn|EA_An, 0, ots->disassFlag);
					break;
			case ofExtAnip:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[1] >> 12) & 7) | 0x18, size, EA_Anip, 0, ots->disassFlag);
					break;

			case ofDisp:
					// branch treats the displacement 0x00 and 0xFF as an indicator how many words follow
					// This test will decline a displacement with the wrong word offset
					if((opcode[0] & 0xF000) == 0x6000)
					{
						val = opcode[0] & 0xFF;
						if(val == 0x00 && size != 2) goto more;
						if(val == 0xFF && size != 4) goto more;
					}
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0102, size, EA_PCDisplacement, opcode[0] & 0xFF, ots->disassFlag);
					break;

			case ofRegList:
					val = opcode[1];
					if((ea & 0x38) == 0x20)				// -(An) has a flipped bitmask
						val = Disass68kFlipBits(val);
					dbuf = Disass68kReglist(dbuf, val);
					break;

			case ofFPU:
					{	// default FPU opcode modes
					int	src = (opcode[1] >> 10) & 7;
					int	dest = (opcode[1] >> 7) & 7;
					char	regFP1 = options & doptRegisterSmall ? 'f' : 'F';
					char	regFP2 = options & doptRegisterSmall ? 'p' : 'P';
					if(opcode[1] & 0x4000)
					{
						// <ea>,FPn
						int	mask = EA_All - EA_An;
						if(src != 0 && src != 4 && src != 6)	// only .B,.W and .L allow Dn as a source
							mask -= EA_Dn;
						dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ea, size, mask, 0, 0);
						if(!dbuf) goto more;
						*dbuf++ = ',';
						*dbuf++ = regFP1; *dbuf++ = regFP2; *dbuf++ = '0'+dest;
						*dbuf = 0;
					} else {
						// FPn,FPn or FPn

						// <ea> has to be 0
						if((opcode[0] & 0x3F) != 0) goto more;

						*dbuf++ = regFP1; *dbuf++ = regFP2; *dbuf++ = '0'+src;
						if(src != dest)
						{
							*dbuf++ = ',';
							*dbuf++ = regFP1; *dbuf++ = regFP2; *dbuf++ = '0'+dest;
						}
						*dbuf = 0;
					}
					}
					break;
			case ofFPUMOVE:
					{	// MOVE <ea>,FPn{k-Factor}
					int	src = (opcode[1] >> 10) & 7;
					// <ea>,FPn
					int	mask = EA_All - EA_An;
					char	regFP1 = options & doptRegisterSmall ? 'f' : 'F';
					char	regFP2 = options & doptRegisterSmall ? 'p' : 'P';
					if(src != 0 && src != 4 && src != 6)	// only .B,.W and .L allow Dn as a source
						mask -= EA_Dn;
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ea, size, mask, 0, 0);
					if(!dbuf) goto more;
					*dbuf++ = ',';
					*dbuf++ = regFP1; *dbuf++ = regFP2; *dbuf++ = '0'+((opcode[1] >> 7) & 7);
					if(src == 3)
					{
						int	kFactor = opcode[1] & 0x7F;
						if(kFactor & 0x40)
							kFactor |= 0x80;
						*dbuf++ = '{';
						sprintf(dbuf, "%d", (signed char)kFactor);
						dbuf += strlen(dbuf);
						*dbuf++ = '}';
					} else if(src == 7)
					{
						if((opcode[1] & 0x0F) != 0) goto more;
						*dbuf++ = '{';
						*dbuf++ = options & doptRegisterSmall ? 'd' : 'D';
						*dbuf++ = '0' + ((opcode[1] >> 4) & 7);
						*dbuf++ = '}';
					} else {
						if((opcode[1] & 0x7F) != 0) goto more;
					}
					*dbuf = 0;
					}
					break;
			case ofFMOVECR:
					{	// MOVECR #const,FPn
					char	regFP1 = options & doptRegisterSmall ? 'f' : 'F';
					char	regFP2 = options & doptRegisterSmall ? 'p' : 'P';
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0100, 1, EA_ImmedParameter, opcode[1] & 0x7F, ots->disassFlag);
					if(!dbuf) goto more;
					reg = (opcode[1] >> 7) & 7;
					*dbuf++ = ',';
					*dbuf++ = regFP1; *dbuf++ = regFP2; *dbuf++ = '0'+reg;
					*dbuf = 0;
					switch(opcode[1] & 0x7F)	// document the well-known constants
					{
					case 0x00:	strcat(commentBuffer, "PI"); break;
					case 0x0B:	strcat(commentBuffer, "Log10(2)"); break;
					case 0x0C:	strcat(commentBuffer, "e"); break;
					case 0x0D:	strcat(commentBuffer, "Log2(e)"); break;
					case 0x0E:	strcat(commentBuffer, "Log10(e)"); break;
					case 0x0F:	strcat(commentBuffer, "0.0"); break;
					case 0x30:	strcat(commentBuffer, "1n(2)"); break;
					case 0x31:	strcat(commentBuffer, "1n(10)"); break;
					case 0x32:	strcat(commentBuffer, "100"); break;
					case 0x33:	strcat(commentBuffer, "10^1"); break;
					case 0x34:	strcat(commentBuffer, "10^2"); break;
					case 0x35:	strcat(commentBuffer, "10^4"); break;
					case 0x36:	strcat(commentBuffer, "10^8"); break;
					case 0x37:	strcat(commentBuffer, "10^16"); break;
					case 0x38:	strcat(commentBuffer, "10^32"); break;
					case 0x39:	strcat(commentBuffer, "10^64"); break;
					case 0x3A:	strcat(commentBuffer, "10^128"); break;
					case 0x3B:	strcat(commentBuffer, "10^256"); break;
					case 0x3C:	strcat(commentBuffer, "10^512"); break;
					case 0x3D:	strcat(commentBuffer, "10^1024"); break;
					case 0x3E:	strcat(commentBuffer, "10^2048"); break;
					case 0x3F:	strcat(commentBuffer, "10^4096"); break;
					}
					}
					break;
			case ofFPUSRRegList:
					{
					int	hasReg = 0;
					*dbuf = 0;
					if(opcode[1] & 0x0400)
					{
						strcat(dbuf, Disass68kSpecialRegister(REG_FPU_FPIAR));
						hasReg = 1;
					}
					if(opcode[1] & 0x0800)
					{
						if(hasReg) strcat(dbuf, "/");
						strcat(dbuf, Disass68kSpecialRegister(REG_FPU_FPSR));
						hasReg = 1;
					}
					if(opcode[1] & 0x1000)
					{
						if(hasReg) strcat(dbuf, "/");
						strcat(dbuf, Disass68kSpecialRegister(REG_FPU_FPCR));
						hasReg = 1;
					}
					if(!hasReg)
						strcat(dbuf, "0");
					dbuf += strlen(dbuf);
					}
					break;
			case ofFPUReglist:	// FMOVEM
					{
					int	mask = opcode[1] & 0xFF;
					if(opcode[1] & 0x0100)
						mask = Disass68kFlipBits(mask) >> 8;
					dbuf = Disass68kFPUReglist(dbuf, mask);
					}
					break;
			case ofFPU3Reg:
					{	// FSINCOS
					int	src = (opcode[1] >> 10) & 7;
					int	dest = (opcode[1] >> 7) & 7;
					char	regFP1 = options & doptRegisterSmall ? 'f' : 'F';
					char	regFP2 = options & doptRegisterSmall ? 'p' : 'P';
					if(opcode[1] & 0x4000)
					{
						// <ea>,FPn
						int	mask = EA_All - EA_An;
						if(src != 0 && src != 4 && src != 6)	// only .B,.W and .L allow Dn as a source
							mask -= EA_Dn;
						dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ea, size, mask, 0, 0);
						if(!dbuf) goto more;
						*dbuf++ = ',';
						*dbuf++ = regFP1; *dbuf++ = regFP2; *dbuf++ = '0'+(opcode[1] & 7);
						*dbuf++ = ',';
						*dbuf++ = regFP1; *dbuf++ = regFP2; *dbuf++ = '0'+dest;
						*dbuf = 0;
					} else {
						// FPn,FPn or FPn

						// <ea> has to be 0
						if((opcode[0] & 0x3F) != 0) goto more;

						*dbuf++ = regFP1; *dbuf++ = regFP2; *dbuf++ = '0'+src;
						*dbuf++ = ',';
						*dbuf++ = regFP1; *dbuf++ = regFP2; *dbuf++ = '0'+(opcode[1] & 7);
						*dbuf++ = ',';
						*dbuf++ = regFP1; *dbuf++ = regFP2; *dbuf++ = '0'+dest;
						*dbuf = 0;
					}
					}
					break;

			case ofCAS:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (opcode[1] & 0x07), size, EA_Dn, 0, ots->disassFlag);
					if(!dbuf) goto more;
					*dbuf++ = ',';
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[1] >> 6) & 0x07), size, EA_Dn, 0, ots->disassFlag);
					break;
			case ofCAS2:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (opcode[1] & 0x07), size, EA_Dn, 0, ots->disassFlag);
					if(!dbuf) goto more;
					*dbuf++ = ':';
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (opcode[2] & 0x07), size, EA_Dn, 0, ots->disassFlag);
					if(!dbuf) goto more;
					*dbuf++ = ',';
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[1] >> 6) & 0x07), size, EA_Dn, 0, ots->disassFlag);
					if(!dbuf) goto more;
					*dbuf++ = ':';
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[2] >> 6) & 0x07), size, EA_Dn, 0, ots->disassFlag);
					if(!dbuf) goto more;
					*dbuf++ = ',';
					*dbuf++ = '(';
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[1] >> 12) & 0x0F), size, EA_Dn|EA_An, 0, ots->disassFlag);
					if(!dbuf) goto more;
					*dbuf++ = ')';
					*dbuf++ = ':';
					*dbuf++ = '(';
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[2] >> 12) & 0x0F), size, EA_Dn|EA_An, 0, ots->disassFlag);
					if(!dbuf) goto more;
					*dbuf++ = ')';
					*dbuf = 0;
					break;
			case ofExt4Dn:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (opcode[0] & 0x07), size, EA_Dn, 0, ots->disassFlag);
					if(!dbuf) goto more;
					*dbuf++ = ':';
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, (opcode[1] & 0x07), size, EA_Dn, 0, ots->disassFlag);
					if(!dbuf) goto more;
					*dbuf++ = ',';
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ((opcode[1] >> 12) & 0x07), size, EA_Dn, 0, ots->disassFlag);
					if(!dbuf) goto more;
					*dbuf = 0;
					break;
			case ofBFEa:
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, ea, size, EA_All & ~(ots->parameter[i]), 0, ots->disassFlag);
					if(!dbuf) goto more;
					*dbuf++ = '{';
					val = (opcode[1] >> 6) & 0x1F;
					if(opcode[1] & 0x0800)
					{
						if(val & 0x18) goto more;
						dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, val & 0x07, 1, EA_Dn, val, ots->disassFlag);
					} else {
						dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0103, 1, EA_ValueParameter, val, ots->disassFlag);
					}
					*dbuf++ = ':';
					val = opcode[1] & 0x1F;
					if(opcode[1] & 0x0020)
					{
						if(val & 0x18) goto more;
						dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, val & 0x07, 1, EA_Dn, val, ots->disassFlag);
					} else {
						if(val == 0) val = 32;
						dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0103, 1, EA_ValueParameter, val, ots->disassFlag);
					}
					*dbuf++ = '}';
					*dbuf = 0;
					break;
			case ofLineA:
					{
					int	lineAVal = opcode[0] & 0xFFF;
					const char	*lineAStr[16] = { "Line-A Initialization",
												  "Put pixel",
												  "Get pixel",
												  "Arbitrary line",
												  "Horizontal line",
												  "Filled rectangle",
												  "Filled polygon",
												  "Bit block transfer",
												  "Text block transfer",
												  "Show mouse",
												  "Hide mouse",
												  "Transform mouse",
												  "Undraw sprite",
												  "Draw sprite",
												  "Copy raster form",
												  "Seedfill"
												  };
					dbuf = Disass68kEA(dbuf, commentBuffer, &addr, opcodeAddr, 0x0100, 2, EA_ImmedParameter, lineAVal, ots->disassFlag);
					if(lineAVal < 16)
						strcat(commentBuffer, lineAStr[lineAVal]);
					}
					break;

			default:
					goto more;
			}
			if(!dbuf) goto more;

			// does another operand follow => add separator
			if ( (i+1<maxop) && ( ots->op[i+1] != ofNone) )
				*dbuf++ = ',';
		}
		return addr-baseAddr;
	}

	// unknown opcode
	strcpy(opcodeBuffer, "DC.W");
	sprintf(operandBuffer,"$%4.4x", Disass68kGetWord(addr));
	return 2;
}

static void		Disass68kComposeStr(char *dbuf, const char *str, int position, int maxPos)
{
	int		i;
	int		len = strlen(dbuf);
	while(len < position) {
		dbuf[len++] = ' ';		/* Will give harmless warning from GCC */
	}
	for(i=0; str[i] && (!maxPos || len+i<maxPos); ++i)
		dbuf[len+i] = str[i];
	if(str[i])
		dbuf[len+i-1] = '+';
	dbuf[len+i] = 0;
}

static void Disass68k_loop (FILE *f, uaecptr addr, uaecptr *nextpc, int cnt)
{
	while (cnt-- > 0) {
		const int	addrWidth = 6;		// 6 on an ST, 8 on a TT
		char	lineBuffer[1024];

		char	addressBuffer[32];
		char	hexdumpBuffer[256];
		char	labelBuffer[256];
		char	opcodeBuffer[64];
		char	operandBuffer[256];
		char	commentBuffer[256];
		int	plen, len, j;

		len = Disass68k(addr, labelBuffer, opcodeBuffer, operandBuffer, commentBuffer);
		if(!len) break;

		sprintf(addressBuffer, "$%*.*x :", addrWidth,addrWidth, addr);

		hexdumpBuffer[0] = 0;
		plen = len;
		if(plen > 80 && (!strncmp(opcodeBuffer, "DC.", 3) || !strncmp(opcodeBuffer, "dc.", 3)))
			plen = ((optionPosLabel - optionPosHexdump) / 5) * 2;

		for(j=0; j<plen; j += 2)
		{
			if(j > 0)
				strcat(hexdumpBuffer, " ");
			if(j + 2 > plen)
			{
				sprintf(hexdumpBuffer+strlen(hexdumpBuffer), "%2.2x", Disass68kGetWord(addr+j) >> 8);
			} else {
				sprintf(hexdumpBuffer+strlen(hexdumpBuffer), "%4.4x", Disass68kGetWord(addr+j));
			}
		}

		lineBuffer[0] = 0;
		if(optionPosAddress >= 0)
			Disass68kComposeStr(lineBuffer, addressBuffer, optionPosAddress, 0);
		if(optionPosHexdump >= 0)
			Disass68kComposeStr(lineBuffer, hexdumpBuffer, optionPosHexdump, optionPosLabel);
		if(optionPosLabel >= 0)
			Disass68kComposeStr(lineBuffer, labelBuffer, optionPosLabel, 0);
		if(optionPosOpcode >= 0)
			Disass68kComposeStr(lineBuffer, opcodeBuffer, optionPosOpcode, 0);
		if(optionPosOperand >= 0)
		{
			size_t	l = strlen(lineBuffer);
			if(lineBuffer[l-1] != ' ')		// force at least one space between opcode and operand
			{
				lineBuffer[l++] = ' ';
				lineBuffer[l] = 0;
			}
			Disass68kComposeStr(lineBuffer, operandBuffer, optionPosOperand, 0);
		}
		if (optionPosComment >= 0)
		{
			/* show comments only if profile data is missing */
			if (commentBuffer[0])
			{
				Disass68kComposeStr(lineBuffer, " ;", optionPosComment, 0);
				Disass68kComposeStr(lineBuffer, commentBuffer, optionPosComment+3, 0);
			}
		}
		addr += len;
		if (f)
			fprintf(f, "%s\n", lineBuffer);
//		if(strstr(opcodeBuffer, "RTS") || strstr(opcodeBuffer, "RTE") || strstr(opcodeBuffer, "JMP")
//		|| strstr(opcodeBuffer, "rts") || strstr(opcodeBuffer, "rte") || strstr(opcodeBuffer, "jmp"))
//			fprintf(f, "\n");
    }
    if (nextpc)
		*nextpc = addr;
}


/**
 * Calculate next PC address from given one, without output
 * @return	next PC address
 */
Uint32 Disasm_GetNextPC(Uint32 pc)
{
	uaecptr nextpc;
	Disass68k_loop (NULL, pc, &nextpc, 1);
	return nextpc;
}

/**
 * Set CPU and FPU mask used for disassembly (when changed from the UI or the options)
 */
void Disasm_SetCPUType ( int CPU , int FPU )
{
	optionCPUTypeMask = 0;

	if ( ( FPU == 68881 ) || ( FPU == 68882 ) )
		optionCPUTypeMask |= MC_FPU;

	switch ( CPU )
	{
		case 0 :	optionCPUTypeMask |= MC68000 ; break;
		case 1 :	optionCPUTypeMask |= MC68010 ; break;
		case 2 :	optionCPUTypeMask |= MC68020 ; break;
		case 3 :	optionCPUTypeMask |= MC68030 ; break;
		case 4 :	optionCPUTypeMask |= MC68040 ; break;
		default :	optionCPUTypeMask |= MC68000 ; break;
	}	
}
