/*
  Hatari - 68kDisass.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_68KDISASS_H
#define HATARI_68KDISASS_H

extern Uint32 Disasm_GetNextPC(Uint32 pc);
extern void Disasm (FILE *f, uaecptr addr, uaecptr *nextpc, int cnt);

enum {
	DISASM_COLUMN_ADDRESS = 0,
	DISASM_COLUMN_HEXDUMP,
	DISASM_COLUMN_LABEL,
	DISASM_COLUMN_OPCODE,
	DISASM_COLUMN_OPERAND,
	DISASM_COLUMN_COMMENT,
	DISASM_COLUMNS			/* number of columns in disassembly output */
};

#define DISASM_COLUMN_DISABLE -1

void Disasm_SetCPUType ( int CPU , int FPU );

#endif		/* HATARI_68KDISASS_H */
