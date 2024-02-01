.PHONY: all clean

all: quaesar

# build68k

build68k: FORCE
	$(MAKE) -f build/Makefile.build68k

src/cpudefs.cpp: build68k src/table68k
	./build68k < src/table68k > $@

generated += src/cpudefs.cpp

# gencomp

gencomp: FORCE $(generated)
	$(MAKE) -f build/Makefile.gencomp

src/jit/comptbl.h: gencomp
	./gencomp

generated += src/jit/comptbl.h

# gencpu

gencpu: FORCE $(generated)
	$(MAKE) -f build/Makefile.gencpu

src/cputbl.h: gencpu
	cd src && ../gencpu && cd ..

generated += src/cputbl.h

# genblitter

genblitter: FORCE
	$(MAKE) -f Makefile.genblitter

blit.h: genblitter
	./genblitter i > src/$@
blitfunc.cpp: genblitter
	./genblitter f > src/$@
blitfunc.h: genblitter
	./genblitter h > src/$@
blittable.cpp: genblitter
	./genblitter t > src/$@

generated += src/blit.h src/blitfunc.cpp src/blitfunc.h src/blittable.cpp

# genlinetoscr

genlinetoscr: FORCE
	$(MAKE) -f build/Makefile.genlinetoscr

linetoscr.cpp: genlinetoscr
	./genlinetoscr > src/linetoscr.cpp

generated += src/linetoscr.cpp

# quasar

quaesar: FORCE $(generated)
	$(MAKE) -f build/Makefile.quaesar

# clean

clean:
	make -f build/Makefile.build68k clean
	make -f build/Makefile.genblitter clean
	make -f build/Makefile.gencomp clean
	make -f build/Makefile.gencpu clean
	make -f build/Makefile.genlinetoscr clean
	make -f build/Makefile.quaesar clean
	rm -rf out

FORCE: ;
