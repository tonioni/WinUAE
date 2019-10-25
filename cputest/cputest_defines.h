
#define DATA_VERSION 5

#define CT_FPREG 0
#define CT_DREG 0
#define CT_AREG 8
#define CT_SSP 16
#define CT_MSP 17
#define CT_SR 18
#define CT_PC 19
#define CT_FPIAR 20
#define CT_FPSR 21
#define CT_FPCR 22
#define CT_SRCADDR 28
#define CT_DSTADDR 29
#define CT_MEMWRITE 30
#define CT_MEMWRITES 31
#define CT_DATA_MASK 31
#define CT_EXCEPTION_MASK 63

#define CT_SIZE_BYTE (0 << 5)
#define CT_SIZE_WORD (1 << 5)
#define CT_SIZE_LONG (2 << 5)
#define CT_SIZE_FPU (3 << 5) // CT_DREG -> CT_FPREG
#define CT_SIZE_MASK (3 << 5)

// if MEMWRITE or PC
#define CT_RELATIVE_START_WORD (0 << 5) // word
#define CT_ABSOLUTE_WORD (1 << 5)
#define CT_ABSOLUTE_LONG (2 << 5)
// if MEMWRITES
#define CT_PC_BYTES (3 << 5)
// if PC
#define CT_RELATIVE_START_BYTE (3 << 5)

#define CT_END 0x80
#define CT_END_FINISH 0xff
#define CT_END_INIT (0x80 | 0x40)
#define CT_END_SKIP (0x80 | 0x40 | 0x01)
#define CT_SKIP_REGS (0x80 | 0x40 | 0x02)
#define CT_EMPTY CT_END_INIT
