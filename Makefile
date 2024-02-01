.PHONY: all clean

all: winuae

# build68k

build68k: FORCE
	$(MAKE) -f Makefile.build68k

cpudefs.cpp: build68k table68k
	./build68k < table68k > $@

generated += cpudefs.cpp

# gencomp

gencomp: FORCE $(generated)
	$(MAKE) -f Makefile.gencomp

jit/comptbl.h: gencomp
	./gencomp

generated += jit/comptbl.h

# gencpu

gencpu: FORCE $(generated)
	$(MAKE) -f Makefile.gencpu

cputbl.h: gencpu
	./gencpu

generated += cputbl.h

# genblitter

genblitter: FORCE
	$(MAKE) -f Makefile.genblitter

blit.h: genblitter
	./genblitter i > $@
blitfunc.cpp: genblitter
	./genblitter f > $@
blitfunc.h: genblitter
	./genblitter h > $@
blittable.cpp: genblitter
	./genblitter t > $@

generated += blit.h blitfunc.cpp blitfunc.h blittable.cpp

# genlinetoscr

genlinetoscr: FORCE
	$(MAKE) -f Makefile.genlinetoscr

linetoscr.cpp: genlinetoscr
	./genlinetoscr > linetoscr.cpp

generated += linetoscr.cpp

# winuae

winuae: FORCE $(generated)
	$(MAKE) -f Makefile.winuae

# clean

clean:
	make -f Makefile.build68k clean
	make -f Makefile.genblitter clean
	make -f Makefile.gencomp clean
	make -f Makefile.gencpu clean
	make -f Makefile.genlinetoscr clean
	make -f Makefile.winuae clean
	rm -rf out

FORCE: ;
