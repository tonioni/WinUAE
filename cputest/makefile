
NOWDATE := "\"$(shell date "+%-d.%-m.%Y")\""
NOWTIME := "\"$(shell date "+%T")\""

CC=/opt/amiga/bin/m68k-amigaos-gcc
AS=/opt/amiga/bin/m68k-amigaos-as

CFLAGS = -mcrt=nix13 -O2 -m68000 -fomit-frame-pointer -msmall-code -msoft-float -DREVDATE=$(NOWDATE) -DREVTIME=$(NOWTIME) -DAMIGA
LINK_CFLAGS = -mcrt=nix13 -lm -s

OBJS = main.o 68kDisass.o asm.o amiga.o

all: $(OBJS)
	$(CC) $(LINK_CFLAGS) -o cputest $^

main.o: main.c
	$(CC) $(CFLAGS) -I. -c -o $@ main.c

68kDisass.o: main.c
	$(CC) $(CFLAGS) -I. -c -o $@ 68kDisass.c

asm.o: asm.S
	$(AS) -m68020  -o $@ asm.S

amiga.o: amiga.S
	$(AS) -m68020  -o $@ amiga.S
