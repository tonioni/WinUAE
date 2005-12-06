# Makefile for Pro-Wizard 1 (for PeeCee)
# copyright 1997-2003 Sylvain "asle" Chipaux

CFLAGS = -O6 -Wall -mpentiumpro -g2 -I/usr/local/include -DDMALLOC \
	-Wtraditional -Wshadow -Wcast-align -Wstrict-prototypes \
	-Wmissing-prototypes -Wredundant-decls -Wnested-externs \
	-save-temps
CC = gcc

# 

OFILES = prowiz.o \
	tests/ac1d.o tests/bp.o tests/di.o tests/eureka.o \
	tests/fc_m.o tests/fuzzac.o tests/gmc.o tests/heatseek.o \
	tests/hrt.o tests/kris.o tests/ksm.o tests/mp.o \
	tests/noiserun.o tests/npd1.o tests/npd2.o tests/npd3.o \
	tests/p40a.o tests/p41a.o tests/p50a.o tests/p60a.o  tests/p61a.o \
	tests/pha.o tests/pm.o tests/pm01.o tests/pm10c.o \
	tests/pm20.o tests/pm40.o tests/pmz.o tests/pp10.o \
	tests/pp21.o tests/pp30.o \
	tests/prun1.o tests/prun2.o tests/ptk.o tests/skyt.o \
	tests/soundtk.o tests/starpack.o tests/stim.o \
	tests/tp1.o tests/tp2.o tests/tp3.o \
	tests/unic.o tests/unic2.o tests/wn.o \
	tests/xann.o tests/zen.o \
	tests/defjam32.o tests/tpack22.o tests/tpack21.o tests/defja32p.o \
	tests/bytek13.o tests/dblatn.o tests/powerp30.o tests/powerp40.o \
	tests/powerp23.o tests/bytek20.o tests/spike.o tests/tpack102.o \
	tests/time17.o tests/mc30addr.o tests/jamcrack.o tests/bsifc.o \
	tests/digibst.o tests/mcrun10.o tests/mcrun12.o tests/qc.o \
	tests/tdd.o tests/fuchs.o tests/bytek30.o tests/syncro.o \
	tests/stc270.o tests/tnmc11.o tests/superc27.o tests/ppbk.o \
	tests/relokit.o tests/maxpac12.o tests/soundfx.o tests/arcd.o \
	tests/crnd.o tests/stk26.o tests/bytekp10.o tests/hqc2.o \
	tests/tryit101.o tests/fc13.o tests/fc14.o tests/crmaddr.o tests/mmd0.o \
	tests/powerp4l.o tests/newtron.o tests/gpmo.o tests/polka.o \
	tests/gnpl.o tests/ambk.o tests/xm.o tests/mcobj.o tests/turbosqueezer61.o \
	tests/stc299d.o tests/stc310.o tests/stc299b.o tests/stc299.o tests/stc300.o \
	tests/nfh.o \
        rips/ac1d.o rips/bp.o rips/di.o rips/eureka.o rips/fc_m.o \
	rips/fuzzac.o rips/hrt.o rips/kris.o rips/mp.o rips/pha.o \
	rips/pm.o rips/pm01.o rips/pm10c.o rips/pm20.o rips/pm18a.o \
	rips/pp21.o rips/pp30.o rips/prun1.o rips/prun2.o rips/ptk.o \
	rips/skyt.o rips/starpack.o rips/unic.o rips/unic2.o rips/wn.o \
	rips/xann.o rips/gmc.o rips/heatseek.o rips/ksm.o \
	rips/noiserun.o rips/npd1.o rips/npd2.o rips/npd3.o \
	rips/p40a.o rips/p40b.o rips/p41a.o rips/pm40.o rips/pp10.o \
	rips/tp1.o rips/tp2.o rips/tp3.o rips/zen.o rips/p50a.o \
	rips/p60a.o rips/startrek.o rips/p61a.o rips/stim.o \
	rips/soundtk.o rips/defjam32.o rips/tpack22.o rips/tpack21.o \
	rips/mcrun.o rips/bytekil.o rips/dblatn.o rips/powerp30.o \
	rips/powerp40.o rips/powerp23.o rips/spike.o rips/tpack102.o \
	rips/digibst.o rips/time17.o rips/mc30addr.o rips/jamcrack.o \
	rips/bsifc.o rips/qc.o rips/tdd.o rips/fuchs.o rips/bytek30.o \
	rips/syncro.o rips/stc270.o rips/tnmc11.o rips/superc27.o \
	rips/ppbk.o rips/relokit.o rips/maxpac12.o rips/soundfx.o \
	rips/stk26.o rips/bytekp10.o rips/hqc2.o rips/tryit101.o \
	rips/fc13.o rips/fc14.o rips/crmaddr.o rips/med.o rips/powerp4l.o \
	rips/newtron.o rips/gpmo.o rips/polka.o rips/gnpl.o rips/ambk.o \
	rips/xm.o rips/mcobj.o rips/turbosqueezer61.o rips/stc299d.o \
	rips/stc310.o rips/stc299b.o rips/stc299.o rips/stc300.o rips/p22a.o \
	rips/p30a.o rips/nfh.o \
	depack/ac1d.o depack/di.o depack/eureka.o depack/fc-m.o \
	depack/fuzzac.o depack/gmc.o depack/heatseek.o depack/hrt.o \
	depack/kris.o depack/ksm.o depack/mp.o depack/noiserun.o \
	depack/np1.o depack/np2.o depack/np3.o depack/p40.o depack/p41a.o \
	depack/p50a.o depack/p60a.o depack/p61a.o depack/pha.o \
	depack/pm01.o depack/pm10c.o depack/pm18a.o depack/pm20.o \
	depack/pm40.o depack/pm.o depack/pp10.o depack/pp21.o \
	depack/pp30.o depack/prun1.o depack/prun2.o depack/skyt.o \
	depack/starpack.o depack/tp1.o depack/tp2.o depack/tp3.o \
	depack/unic.o depack/unic2.o depack/wn.o depack/xann.o \
	depack/zen.o depack/stim.o depack/qc.o depack/tdd.o \
	depack/fuchs.o depack/soundfx.o depack/stk26.o depack/newtron.o \
	depack/gpmo.o depack/polka.o depack/gnpl.o depack/ambk.o \
	depack/p30a.o depack/p22a.o depack/nfh.o \
	misc/misc.o misc/testbag.o

EXEFILE = prowiz.exe

.c.o:
	$(CC) -DUNIX $(CFLAGS) -c $< -o $@

prowiz:	$(OFILES)
	$(CC) -o $(EXEFILE) -L/usr/local/lib -ldmalloc $(OFILES) 

# 
clean:	clear
	rm -f *.o
	rm -f tests/*.o
	rm -f rips/*.o
	rm -f depack/*.o
	rm -f misc/*.o
	rm -f core *.core

#
clear:
	rm -f *~ *.*~ tests/*~ tests/*.*~ rips/*~ rips/*.*~ misc/*~ misc/*.*~ include/*~ include/*.*~ depack/*~ depack/*.*~ *.i *.s
