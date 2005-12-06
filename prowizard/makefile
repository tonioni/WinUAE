# Makefile for Pro-Wizard 1 (for PeeCee)
# copyright 1997-2005 Sylvain "asle" Chipaux
#
# add -DINCLUDEALL in the '.c.o' case for a compilation with the binaries rippers
# and type 'make' or 'make prowiz'
#
# 'make simple' to compile w/o binaries rippers.

CFLAGS = -O6 -Wall -mpentiumpro -I/usr/local/include \
	-Wshadow -Wcast-align -Wstrict-prototypes \
	-Wmissing-prototypes -Wredundant-decls -Wnested-externs

CC = gcc

OFILESWOPACKER = prowiz.o \
	rippers/AC1D_packer.o rippers/AMOS-MusicBank.o rippers/BP-Soundmonitor.o rippers/BSI-FutureComposer.o rippers/ChipTracker.o rippers/DigiBooster17.o rippers/DigitalIllusion.o rippers/EurekaPacker.o rippers/FC-M-Packer.o rippers/FastTracker2.o rippers/FuchsTracker.o rippers/FutureComposer13.o rippers/FutureComposer14.o rippers/FuzzacPacker.o rippers/GPMO.o rippers/GameMusicCreator.o rippers/GnuPlayer.o rippers/HeatseekerMC10.o rippers/HornetPacker.o rippers/JamCracker.o rippers/KefrensSoundMachine.o rippers/MED-Octamed.o rippers/MOD-compatible.o rippers/ModuleProtector.o rippers/Newtron.o rippers/NoiseFromHeaven.o rippers/NoisePacker1.o rippers/NoisePacker2.o rippers/NoisePacker3.o rippers/NoiseRunner.o rippers/PhaPacker.o rippers/PolkaPacker.o rippers/PowerMusic.o rippers/ProPacker10.o rippers/ProPacker21.o rippers/ProPacker30.o rippers/Promizer01.o rippers/Promizer10c.o rippers/Promizer18a.o rippers/Promizer20.o rippers/Promizer40.o rippers/Prorunner10.o rippers/Prorunner20.o rippers/QuadraComposer.o rippers/SkytPacker.o rippers/SoundFX.o rippers/SoundTracker.o rippers/SoundTracker26.o rippers/StartrekkerPack.o rippers/StimPacker.o rippers/TMK.o rippers/TheDarkDemon.o rippers/ThePlayer22a.o rippers/ThePlayer30a.o rippers/ThePlayer40.o rippers/ThePlayer41a.o rippers/ThePlayer50a.o rippers/ThePlayer60a.o rippers/ThePlayer61a.o rippers/TrackerPacker1.o rippers/TrackerPacker2.o rippers/TrackerPacker3.o rippers/UnicTracker.o rippers/UnicTracker2.o rippers/WantonPacker.o rippers/XannPlayer.o rippers/ZenPacker.o \
	misc/misc.o misc/testbag.o
OFILESALL = prowiz.o \
	rippers/AC1D_packer.o rippers/AMOS-MusicBank.o rippers/BP-Soundmonitor.o rippers/BSI-FutureComposer.o rippers/ChipTracker.o rippers/DigiBooster17.o rippers/DigitalIllusion.o rippers/EurekaPacker.o rippers/FC-M-Packer.o rippers/FastTracker2.o rippers/FuchsTracker.o rippers/FutureComposer13.o rippers/FutureComposer14.o rippers/FuzzacPacker.o rippers/GPMO.o rippers/GameMusicCreator.o rippers/GnuPlayer.o rippers/HeatseekerMC10.o rippers/HornetPacker.o rippers/JamCracker.o rippers/KefrensSoundMachine.o rippers/MED-Octamed.o rippers/MOD-compatible.o rippers/ModuleProtector.o rippers/Newtron.o rippers/NoiseFromHeaven.o rippers/NoisePacker1.o rippers/NoisePacker2.o rippers/NoisePacker3.o rippers/NoiseRunner.o rippers/PhaPacker.o rippers/PolkaPacker.o rippers/PowerMusic.o rippers/ProPacker10.o rippers/ProPacker21.o rippers/ProPacker30.o rippers/Promizer01.o rippers/Promizer10c.o rippers/Promizer18a.o rippers/Promizer20.o rippers/Promizer40.o rippers/Prorunner10.o rippers/Prorunner20.o rippers/QuadraComposer.o rippers/SkytPacker.o rippers/SoundFX.o rippers/SoundTracker.o rippers/SoundTracker26.o rippers/StartrekkerPack.o rippers/StimPacker.o rippers/TMK.o rippers/TheDarkDemon.o rippers/ThePlayer22a.o rippers/ThePlayer30a.o rippers/ThePlayer40.o rippers/ThePlayer41a.o rippers/ThePlayer50a.o rippers/ThePlayer60a.o rippers/ThePlayer61a.o rippers/TrackerPacker1.o rippers/TrackerPacker2.o rippers/TrackerPacker3.o rippers/UnicTracker.o rippers/UnicTracker2.o rippers/WantonPacker.o rippers/XannPlayer.o rippers/ZenPacker.o \
	rippers/AMOS-PowerPackerBank.o rippers/Bytekiller.o rippers/Crunchmania-Address.o rippers/DefjamCruncher32-pro.o rippers/DoubleAction.o rippers/Dragpack100.o rippers/Dragpack252.o rippers/HQC2.o rippers/MasterCruncher30-Address.o rippers/MaxPacker12.o rippers/MegaCruncher-Object.o rippers/MegaCruncher.o rippers/PowerPacker23.o rippers/PowerPacker30.o rippers/PowerPacker40-library.o rippers/PowerPacker40.o rippers/RelokIt10.o rippers/SpikeCruncher.o rippers/StoneCracker270.o rippers/StoneCracker299.o rippers/StoneCracker299b.o rippers/StoneCracker299d.o rippers/StoneCracker300.o rippers/StoneCracker310.o rippers/SuperCruncher27.o rippers/SyncroPacker46.o rippers/TNMCruncher.o rippers/TetraPack102.o rippers/TetraPack21.o rippers/TetraPack22.o rippers/TimeCruncher17.o rippers/TryItCruncher101.o rippers/TurboSqueezer61.o rippers/datacrunchers.o \
	misc/misc.o misc/testbag.o

EXEFILE = prowiz.exe

# add -DINCLUDEALL here for a compilation with the binaries rippers
.c.o:
	$(CC) -DUNIX -DINCLUDEALL $(CFLAGS) -c $< -o $@

all:	prowiz

simple:	prowiz_wo_packer

prowiz:	$(OFILESALL)
	$(CC) -o $(EXEFILE) -L/usr/local/lib -ldmalloc $(OFILESALL) 

prowiz_wo_packer:	$(OFILESWOPACKER)
	$(CC) -o $(EXEFILE) -L/usr/local/lib -ldmalloc $(OFILESWOPACKER)

# 
clean:	clear
	rm -f *.o
	rm -f rippers/*.o
	rm -f misc/*.o
	rm -f core *.core

#
clear:
	rm -f *~ *.*~ rippers/*~ rippers/*.*~ misc/*~ misc/*.*~ include/*~ include/*.*~ *.i *.s
