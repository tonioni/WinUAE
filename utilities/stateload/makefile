
NOWDATE := "\"$(shell date "+%-d.%-m.%Y")\""
NOWTIME := "\"$(shell date "+%T")\""

CC=/opt/amiga/bin/m68k-amigaos-gcc
AS=/opt/amiga/bin/m68k-amigaos-as

CFLAGS = -mcrt=nix13 -Os -m68000 -fomit-frame-pointer -msmall-code -DREVDATE=$(NOWDATE) -DREVTIME=$(NOWTIME)
LINK_CFLAGS = -mcrt=nix13 -s

OBJS = main.o asm.o inflate.o mmu.o

all: $(OBJS)
	$(CC) $(LINK_CFLAGS) -o ussload $^

main.o: main.c
	$(CC) $(CFLAGS) -I. -c -o $@ main.c

mmu.o: mmu.c
	$(CC) $(CFLAGS) -I. -c -o $@ mmu.c

asm.o: asm.S
	$(AS) -m68040  -o $@ asm.S

inflate.o: inflate.S
	$(CC) $(CFLAGS) -I. -c -o $@ inflate.S
